#include "game/tp/TwilightPrincessGame.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include <nlohmann/json.hpp>

#include "core/CheckPlacements.h"
#include "core/CheckSaveBindings.h"
#include "core/Items.h"
#include "core/QuestState.h"
#include "core/Region.h"
#include "core/SaveOffsets.h"
#include "core/SeedHeader.h"
#include "core/SettingsString.h"
#include "core/Stages.h"
#include "core/logic/ContextBuilder.h"
#include "core/logic/Reach.h"
#include "core/logic/WorldData.h"
#include "memory/SourceFactory.h"
#include "ui/UIState.h"

namespace tpt::game::tp {

// ===========================================================================
// Anonymous namespace — file-local helpers and tables
// ===========================================================================

namespace {

// Per-flag tab definitions. Categories use the web-gen vocabulary
// (Check::checkCategory). The "All Checks" / "Reachable" defaults are
// rendered separately by the columns and don't appear here.
//
// Mini-bosses has no web-gen equivalent — the 7 entries are pinned by name.
// "Story" maps to web-gen "Quest" (the apworld "Story" tag had no entries).
// "NPC"/"Shop" both include "Npc - Shop" so shop NPCs appear in both tabs.
const std::vector<tpt::ui::FilterSpec> kFilterSpecs{
    // Progression: reachable checks whose *type* can hold a progression item
    // under the active settings (settings-adaptive, see computeProgressionCats).
    // Middle column only — it's a "what's worth doing next" view.
    {"Progression", {}, {}, /*progression=*/true, /*reachableOnly=*/true},
    {"Poes",        {"Poe"},                       {}},
    {"Bugs",        {"Golden Bug", "Bug Reward"},  {}},
    {"Hearts",      {"Heart Container"},           {}},
    {"Skills",      {"Hidden Skill"},              {}},
    {"Bosses",      {"Boss"},                      {}},
    {"Mini-bosses", {},                            {
        "Arbiters Grounds Death Sword Chest",
        "City in The Sky Aeralfos Chest",
        "Forest Temple Gale Boomerang",
        "Goron Mines Dangoro Chest",
        "Lakebed Temple Deku Toad Chest",
        "Snowpeak Ruins Ball and Chain",
        "Temple of Time Darknut Chest",
    }},
    {"Sky Book",    {"Sky Book"},                  {}},
    {"Story",       {"Quest"},                     {}},
    {"NPC",         {"Npc", "Npc - Shop"},         {}},
    {"Shop",        {"Shop", "Npc - Shop"},        {}},
    // Rupees: 88 entries. Visibility is gated by the same rupee-shuffle
    // rules the All Checks pane uses — see rebuildFlagViews. When the
    // active seed has no rupee shuffle, the tab renders empty.
    {"Rupees",      {"Rupee - Freestanding", "Rupee - Hidden"}, {}},
};

// Item-type categories that represent a randomizable item slot. Location /
// structural tags (Overworld, Dungeon, region names, ARC, DZX, Dungeon Items,
// and the lone "Mirror Chamber" area tag) are excluded so they don't make
// every check in a region look "capable"; every reward-slot tag — including
// singletons like "Npc - Shop" (Barnes) — is included so no progression
// check is ever missed.
const std::unordered_set<std::string>& progressionTypeCategories() {
    static const std::unordered_set<std::string> kTypeCats{
        "Chest", "Poe", "Npc", "Npc - Shop", "Shop", "Rupee - Freestanding",
        "Rupee - Hidden", "Small Key", "Big Key", "Compass", "Dungeon Map",
        "Golden Bug", "Bug Reward", "Boss", "Dungeon Reward", "Heart Container",
        "Hidden Skill", "Sky Book", "Quest", "Fishing Hole", "Ordon Pumpkin",
        "Cutscene",
    };
    return kTypeCats;
}

// The set of type-categories that can hold a progression item in this seed.
// Empirical (mirrors detectRupeeShuffle): a type is capable if some check of
// that type actually holds a progression item. Chests always count (they're
// definitionally a randomized slot). With no seed loaded we fall back to a
// sensible static default — the slots that hold progression in a default seed.
std::unordered_set<std::string> computeProgressionCats(const tpt::ui::State& s) {
    if (s.placements.empty()) {
        return {"Chest", "Heart Container", "Quest", "Small Key", "Big Key"};
    }
    const auto& typeCats = progressionTypeCategories();
    std::unordered_set<std::string> out{"Chest"};
    for (const auto& [name, chk] : s.checks) {
        const auto it = s.placements.find(name);
        if (it == s.placements.end() || !tpt::core::isProgressionItemId(it->second))
            continue;
        for (const auto& cat : chk.categories)
            if (typeCats.count(cat)) out.insert(cat);
    }
    return out;
}

bool checkMatchesFilter(const std::string& name,
                        const tpt::core::logic::Check& chk,
                        const tpt::ui::FilterSpec& f) {
    for (const auto& want : f.matchAnyName) {
        if (name == want) return true;
    }
    for (const auto& tag : chk.categories) {
        for (const auto& want : f.matchAnyCategory) {
            if (tag == want) return true;
        }
    }
    return false;
}

// Web-gen consistently names rupee checks with "Rupee" in the name (e.g.
// "Ordon Bo Window Rupee 1", "Faron Woods Coro Boulder Rupee 4"). No
// non-rupee check matches this substring in the current world graph.
bool nameLooksLikeRupee(std::string_view name) {
    return name.find("Rupee") != std::string_view::npos;
}

// "Rupee shuffle" is active in this seed iff at least one rupee slot holds a
// progression item. We detect dynamically rather than from a seed setting,
// because the web-gen exposes rupee-shuffle via multiple sub-settings.
bool detectRupeeShuffle(const tpt::core::SeedPlacements& placements) {
    for (const auto& [name, itemId] : placements) {
        if (nameLooksLikeRupee(name) && tpt::core::isProgressionItemId(itemId)) {
            return true;
        }
    }
    return false;
}

// Non-stage web-gen category strings. A check's stage is the first
// `checkCategory` entry that isn't one of these structural / type tags.
const std::unordered_set<std::string>& nonStageCategories() {
    static const std::unordered_set<std::string> kStructural{
        "ARC", "Big Key", "Boss", "Bug Reward", "Chest", "Compass",
        "Cutscene", "DZX", "Dungeon", "Dungeon Items", "Dungeon Map",
        "Dungeon Reward", "Golden Bug", "Heart Container", "Hidden Skill",
        "Npc", "Npc - Shop", "Ordon Pumpkin", "Overworld", "Poe", "Quest",
        "Rupee - Freestanding", "Rupee - Hidden", "Shop", "Sky Book",
        "Small Key",
    };
    return kStructural;
}

std::string stageFromCategories(const std::vector<std::string>& cats) {
    const auto& structural = nonStageCategories();
    for (const auto& cat : cats) {
        if (!structural.count(cat)) return cat;
    }
    return {};
}

std::filesystem::path findExeDataDir() {
    namespace fs = std::filesystem;
    const auto candidates = {
        fs::current_path() / "data",
    };
    for (const auto& p : candidates) {
        if (fs::exists(p / "world" / "Rooms")) return p;
    }
    return fs::current_path() / "data";
}

std::filesystem::path findDataDirRelativeToExe() {
    namespace fs = std::filesystem;
#ifdef _WIN32
    char buf[260] = {};
    const DWORD n = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf)) {
        return fs::path(buf).parent_path() / "data";
    }
#endif
    return findExeDataDir();
}

// --- Filter / status rebuilders -------------------------------------------
//
// poll() runs these against the State after decoding the save block. Kept
// in the anon namespace as free functions rather than promoted to private
// methods because they don't need access to anything that isn't in State.

void rebuildAllByStage(tpt::ui::State& s) {
    s.allByStage.clear();
    s.totalCompleted = 0;
    s.totalResolvable = 0;
    for (const auto& [stage, names] : s.masterByStage) {
        std::vector<tpt::ui::CheckEntry> entries;
        entries.reserve(names.size());
        for (const auto& name : names) {
            const bool isRupee = nameLooksLikeRupee(name);

            // Rupee filtering. We hide rupees in two cases:
            //   1) No rupee shuffle in this seed — they're not meaningful checks.
            //   2) "Progression-only" filter is on AND this rupee doesn't hold
            //      a progression item.
            // Otherwise we always show them and treat them as Pending until
            // collected (see status block below). This prevents Pending vs
            // Unknown markers from leaking which rupees ARE progression — a
            // spoiler we'd otherwise introduce by tracking only the
            // progression ones.
            if (isRupee) {
                if (!s.rupeeShuffleActive) continue;
                if (s.progressionRupeesOnly
                    && !tpt::core::isCheckProgressionInSeed(name, s.placements)) {
                    continue;
                }
            }

            tpt::ui::CheckStatus status = tpt::ui::CheckStatus::Unknown;
            if (s.saveLoaded) {
                if (s.completed.count(name)) {
                    status = tpt::ui::CheckStatus::Done;
                } else if (isRupee) {
                    // Anti-spoiler: every visible rupee (progression or not)
                    // is rendered as Pending. Real completion still flows
                    // through s.completed for the progression ones.
                    status = tpt::ui::CheckStatus::Pending;
                } else if (auto it = s.saveBindings.find(name); it != s.saveBindings.end()) {
                    const bool resolvable = it->second.offset && it->second.bit
                        && (it->second.type == tpt::core::SaveBindingType::Region
                            || it->second.type == tpt::core::SaveBindingType::Flag
                            || name == "Hyrule Castle Ganondorf");
                    status = resolvable ? tpt::ui::CheckStatus::Pending
                                        : tpt::ui::CheckStatus::Unknown;
                }
            }

            if (status == tpt::ui::CheckStatus::Done) ++s.totalCompleted;
            if (status == tpt::ui::CheckStatus::Done ||
                status == tpt::ui::CheckStatus::Pending) ++s.totalResolvable;
            entries.push_back({name, status});
        }
        if (!entries.empty()) {
            s.allByStage.emplace(stage, std::move(entries));
        }
    }
}

void rebuildReachableByStage(tpt::ui::State& s) {
    s.reachableByStage.clear();
    s.totalReachablePending = 0;
    std::unordered_map<std::string, std::string> nameToStage;
    nameToStage.reserve(s.checks.size());
    for (const auto& [stage, names] : s.masterByStage)
        for (const auto& n : names) nameToStage.emplace(n, stage);

    for (const auto& name : s.pendingSet) {
        // Hide non-progression rupees from the Reachable pane regardless of
        // the All-Checks filter — non-progression rupees aren't useful
        // "next steps" and clutter the list.
        if (nameLooksLikeRupee(name) &&
            !tpt::core::isCheckProgressionInSeed(name, s.placements)) {
            continue;
        }
        auto it = nameToStage.find(name);
        const std::string stage = (it != nameToStage.end()) ? it->second : "(other)";
        s.reachableByStage[stage].push_back({name, tpt::ui::CheckStatus::Pending});
    }
    for (auto& [_, v] : s.reachableByStage) {
        std::sort(v.begin(), v.end(),
            [](const tpt::ui::CheckEntry& a, const tpt::ui::CheckEntry& b){
                return a.name < b.name;
            });
        s.totalReachablePending += static_cast<int>(v.size());
    }
}

void rebuildFlagViews(tpt::ui::State& s) {
    s.flagAllByStage.clear();
    s.flagReachableByStage.clear();

    std::unordered_map<std::string, tpt::ui::CheckStatus> statusByName;
    for (const auto& [_, entries] : s.allByStage)
        for (const auto& e : entries) statusByName.emplace(e.name, e.status);

    std::unordered_map<std::string, std::string> nameToStage;
    nameToStage.reserve(s.checks.size());
    for (const auto& [stage, names] : s.masterByStage)
        for (const auto& n : names) nameToStage.emplace(n, stage);

    const std::unordered_set<std::string> progCats = computeProgressionCats(s);
    auto matchesSpec = [&](const std::string& name,
                           const tpt::core::logic::Check& chk,
                           const tpt::ui::FilterSpec& spec) {
        if (spec.progression) {
            for (const auto& cat : chk.categories)
                if (progCats.count(cat)) return true;
            return false;
        }
        return checkMatchesFilter(name, chk, spec);
    };

    for (const auto& spec : kFilterSpecs) {
        std::map<std::string, std::vector<tpt::ui::CheckEntry>> all, reach;
        for (const auto& [name, chk] : s.checks) {
            if (!matchesSpec(name, chk, spec)) continue;

            // Rupee visibility, applied uniformly across all tabs so the
            // Rupees tab (and any future tab that overlaps) tracks the
            // master columns' rules:
            //   All  side: hide rupees when the seed has no rupee shuffle,
            //              OR when progressionRupeesOnly is on AND this
            //              rupee isn't a progression placement.
            //   Reach side: always hide non-progression rupees — they're
            //              never useful "next steps".
            const bool isRupee = nameLooksLikeRupee(name);
            const bool rupeeIsProg = isRupee &&
                tpt::core::isCheckProgressionInSeed(name, s.placements);

            bool includeInAll = true;
            if (isRupee) {
                if (!s.rupeeShuffleActive) includeInAll = false;
                else if (s.progressionRupeesOnly && !rupeeIsProg) includeInAll = false;
            }
            const bool includeInReach = !(isRupee && !rupeeIsProg);

            auto stIt = nameToStage.find(name);
            const std::string stage = (stIt != nameToStage.end()) ? stIt->second : "(other)";

            if (includeInAll) {
                const auto sIt = statusByName.find(name);
                const tpt::ui::CheckStatus st = (sIt == statusByName.end())
                    ? tpt::ui::CheckStatus::Unknown : sIt->second;
                all[stage].push_back({name, st});
            }
            if (includeInReach && s.pendingSet.count(name)) {
                reach[stage].push_back({name, tpt::ui::CheckStatus::Pending});
            }
        }
        auto sortAll = [](auto& m) {
            for (auto& [_, v] : m)
                std::sort(v.begin(), v.end(),
                    [](const tpt::ui::CheckEntry& a, const tpt::ui::CheckEntry& b){
                        return a.name < b.name;
                    });
        };
        sortAll(all);
        sortAll(reach);
        if (!all.empty())   s.flagAllByStage[spec.label]       = std::move(all);
        if (!reach.empty()) s.flagReachableByStage[spec.label] = std::move(reach);
    }
}

// ===========================================================================
// settings:: — local helpers for the rando-settings collapsing pane
// ===========================================================================

namespace settings {

bool anyProgressionInCategory(
    const tpt::ui::State& s,
    std::initializer_list<std::string_view> wantCategories) {
    for (const auto& [name, chk] : s.checks) {
        bool match = false;
        for (const auto& cat : chk.categories) {
            for (auto want : wantCategories) {
                if (cat == want) { match = true; break; }
            }
            if (match) break;
        }
        if (!match) continue;
        auto it = s.placements.find(name);
        if (it != s.placements.end() && tpt::core::isProgressionItemId(it->second)) {
            return true;
        }
    }
    return false;
}

constexpr ImVec4 kLabelColor   {0.65f, 0.66f, 0.67f, 1.0f};
constexpr ImVec4 kValueColor   {0.92f, 0.92f, 0.92f, 1.0f};
constexpr ImVec4 kPositiveColor{0.55f, 0.95f, 0.55f, 1.0f};
constexpr ImVec4 kNeutralColor {0.78f, 0.86f, 0.95f, 1.0f};
constexpr ImVec4 kUnknownColor {0.55f, 0.57f, 0.58f, 1.0f};

constexpr const char* kUnknownTooltip =
    "Import settings string to enable auto-tracking for this setting";

void labelText(const char* label) {
    ImGui::TextColored(kLabelColor, "%s:", label);
    ImGui::SameLine();
}

void value(const char* label, const char* val,
           ImVec4 color = kValueColor) {
    labelText(label);
    ImGui::TextColored(color, "%s", val);
}

void boolValue(const char* label, bool on) {
    value(label, on ? "yes" : "no", on ? kPositiveColor : kUnknownColor);
}

void unknown(const char* label) {
    labelText(label);
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kUnknownTooltip);
}

void stringOrUnknown(const char* label, const std::string& v) {
    if (v.empty()) unknown(label);
    else           value(label, v.c_str(), kNeutralColor);
}

void boolOrUnknown(const char* label, const std::optional<bool>& v) {
    if (!v) unknown(label);
    else    boolValue(label, *v);
}

void shuffleAutoDetected(const char* label, bool shuffled) {
    value(label, shuffled ? "shuffled" : "vanilla",
          shuffled ? kPositiveColor : kNeutralColor);
}

void section(const char* title) {
    ImGui::Spacing();
    ImGui::TextDisabled("— %s —", title);
}

}  // namespace settings
}  // anonymous namespace

// ===========================================================================
// GameModule interface
// ===========================================================================

// --- Lifecycle -------------------------------------------------------------

bool TwilightPrincessGame::loadWorldData(std::ostream& errlog) {
    namespace fs = std::filesystem;
    const auto dataDir  = findDataDirRelativeToExe();
    const auto worldDir = dataDir / "world";
    const auto bindPath = dataDir / "check_save_bindings.json";

    try {
        state_.rooms           = tpt::core::logic::loadRooms(worldDir);
        state_.checks          = tpt::core::logic::loadChecks(worldDir);
        state_.saveBindings    = tpt::core::loadCheckSaveBindings(bindPath);
        state_.placementsIndex = tpt::core::CheckPlacementsIndex::load(
            dataDir / "check_placements.json");
    } catch (const std::exception& e) {
        errlog << "world data load failed: " << e.what() << "\n";
        state_.error = std::string("world data load failed: ") + e.what();
        return false;
    }

    // Build the master grouping by stage from the *web-gen* check graph
    // (598 entries) — that's the authoritative list of checks the seed
    // actually shuffles. The save-bit bindings in saveBindings are joined
    // by name at use sites but don't participate in stage assignment or
    // categorization.
    for (const auto& [name, check] : state_.checks) {
        // Boss checks have two more or less equivalent checks which check
        // for their heart container reward and dungeon reward. Therefor
        // we hide the boss checks as its duplicated information in
        // practice. (Boss-defeat slots are internal to the rando's logic
        // system — the virtual `<Boss>_Defeated` item powers downstream
        // CanComplete<Dungeon> predicates — but the player never "picks
        // up" anything at the slot.)
        if (check.itemId.ends_with("_Defeated")) continue;
        std::string stage = stageFromCategories(check.categories);
        if (stage.empty()) stage = "(other)";
        state_.masterByStage[stage].push_back(name);
    }
    for (auto& [_, v] : state_.masterByStage) std::sort(v.begin(), v.end());

    state_.worldLoaded = true;
    return true;
}

void TwilightPrincessGame::poll(tpt::memory::MemorySource& mem) {
    // Give the source a chance to refresh anything it tracks live (e.g.
    // Dusk: re-evaluate which save buffer is live based on observed writes).
    mem.tick();
    state_.sourceName     = mem.sourceName();
    state_.emulatorHooked = mem.isConnected();
    if (!state_.emulatorHooked) {
        state_.region.reset();
        state_.gameId.clear();
        state_.saveLoaded = false;
        state_.inv.reset();
        state_.qs.reset();
        state_.eventFlags.clear();
        state_.getItemFlags.clear();
        state_.seed.reset();
        state_.placements.clear();
        state_.rupeeShuffleActive = false;
        state_.completed.clear();
        state_.reachedRooms.clear();
        state_.pendingSet.clear();
        rebuildAllByStage(state_);
        rebuildReachableByStage(state_);
        rebuildFlagViews(state_);
        return;
    }

    if (!state_.region) state_.region = tpt::core::detectRegion(mem);
    if (!state_.region) {
        state_.error = "connected but not a recognised TP build";
        state_.saveLoaded = false;
        return;
    }
    state_.gameId = mem.gameId();

    std::vector<std::uint8_t> save(tpt::core::kSaveBlockSize);
    if (!mem.readBytes(state_.region->saveAddr, save.data(), save.size())) {
        state_.saveLoaded = false;
        state_.error = "save block read failed";
        return;
    }
    state_.error.clear();
    state_.saveLoaded = true;
    state_.currentNode = save[tpt::core::kOffsetCurrentNode];

    state_.inv = tpt::core::readInventory(save, state_.currentNode);
    state_.qs  = tpt::core::readQuestState(save, state_.currentNode);
    state_.eventFlags   = tpt::core::logic::readAllEventFlags(save);
    state_.getItemFlags = tpt::core::logic::readAllGetItemFlags(save);

    // Seed header scan can be slow; only do it once per session unless invalid.
    if (!state_.seed) {
        state_.seed = tpt::core::readSeedSettings(mem);
        if (state_.seed) {
            state_.placements = tpt::core::readSeedPlacements(
                mem, *state_.seed, state_.placementsIndex);
            state_.rupeeShuffleActive = detectRupeeShuffle(state_.placements);
        }
    }

    // Settings overlay, weakest to strongest: web-gen defaults (so unspecified
    // settings resolve to the generator's real default, not the permissive
    // fallback), then the seed header, then the settings string.
    std::unordered_map<std::string, std::string> dslSettings =
        tpt::core::logic::dslDefaultSettings();
    if (state_.seed)
        for (const auto& [k, v] : tpt::core::logic::dslSettingsFromSeed(*state_.seed))
            dslSettings[k] = v;
    if (!state_.settingsString.empty()) {
        try {
            const auto parsed = tpt::core::decodeSettingsString(state_.settingsString);
            for (const auto& [k, v] : tpt::core::logic::dslSettingsFromParsed(parsed))
                dslSettings[k] = v;
        } catch (const std::exception&) { /* silent in UI */ }
    }

    // Completion set.
    state_.completed = tpt::core::completedCheckSet(
        state_.saveBindings, save, state_.currentNode, state_.placements);

    // Reach + pending.
    auto ctx = tpt::core::logic::buildContext(
        *state_.inv, *state_.qs, state_.eventFlags, state_.getItemFlags,
        state_.glitched, dslSettings);
    // warpRoomsFromPortals gates on wolf form (Shadow Crystal) internally.
    const auto warps = tpt::core::logic::warpRoomsFromPortals(state_.qs->portals, ctx);
    state_.reachedRooms = tpt::core::logic::reach(
        state_.rooms, ctx, {tpt::core::logic::kDefaultStartRoom}, warps);
    const auto pending = tpt::core::logic::pendingInReach(
        state_.rooms, state_.checks, state_.reachedRooms, state_.completed, ctx);
    state_.pendingSet.clear();
    for (const auto& name : pending) {
        // Skip boss-defeat slots — virtual <Boss>_Defeated items that power
        // CanComplete<Dungeon> in the logic, with no save binding because the
        // player doesn't physically "collect" them. loadWorldData already
        // excludes them from masterByStage; without this filter they'd leak
        // into the Reachable pane (and FlagViews' reach side) because
        // pendingInReach doesn't know about the convention.
        const auto it = state_.checks.find(name);
        if (it != state_.checks.end() && it->second.itemId.ends_with("_Defeated")) continue;
        state_.pendingSet.insert(name);
    }

    rebuildAllByStage(state_);
    rebuildReachableByStage(state_);
    rebuildFlagViews(state_);
}

// --- Data exposure ---------------------------------------------------------

const std::vector<tpt::ui::FilterSpec>& TwilightPrincessGame::filterSpecs() const {
    return kFilterSpecs;
}

// --- Options row -----------------------------------------------------------
//
// Top-of-window controls: settings-string input + paste button, with the
// rupee filter checkbox tucked on the right (only when meaningful).

void TwilightPrincessGame::renderOptionsRow() {
    constexpr char kSettingsLabel[] = "Randomizer settings string:";
    constexpr char kPasteLabel[]    = "Paste";
    constexpr char kRupeeLabel[]    = "Progression rupees only";

    ImGuiStyle& style = ImGui::GetStyle();
    const float charW     = ImGui::CalcTextSize("M").x;
    const float spacing   = style.ItemSpacing.x;
    const float framePadX = style.FramePadding.x;

    // Validate every frame so the red border tracks edits live (decoder
    // is O(string length) — microseconds for any plausible input).
    bool decodeError = false;
    if (!state_.settingsString.empty()) {
        try { tpt::core::decodeSettingsString(state_.settingsString); }
        catch (...) { decodeError = true; }
    }

    const float pasteW = ImGui::CalcTextSize(kPasteLabel).x + framePadX * 2.0f;
    float reserveRight = pasteW + spacing;
    if (state_.rupeeShuffleActive) {
        const float boxW   = ImGui::GetFrameHeight();
        const float labelW = ImGui::CalcTextSize(kRupeeLabel).x;
        const float helpW  = ImGui::CalcTextSize("(?)").x;
        reserveRight += boxW + style.ItemInnerSpacing.x + labelW
                      + spacing + helpW + spacing;
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(kSettingsLabel);
    ImGui::SameLine();

    const float minInputW = charW * 32.0f + framePadX * 2.0f;
    const float contentW  = ImGui::CalcTextSize(state_.settingsString.c_str()).x
                          + framePadX * 2.0f + charW;
    const float maxInputW = std::max(
        minInputW,
        ImGui::GetContentRegionAvail().x - reserveRight);
    const float inputW = std::clamp(contentW, minInputW, maxInputW);

    if (decodeError) {
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{0.95f, 0.40f, 0.40f, 1.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
    }
    ImGui::SetNextItemWidth(inputW);
    ImGui::InputText("##settings", &state_.settingsString);
    if (decodeError) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Settings string failed to decode — check format/version.");
        }
    }

    ImGui::SameLine();
    if (ImGui::Button(kPasteLabel)) {
        if (const char* clip = ImGui::GetClipboardText(); clip && *clip) {
            state_.settingsString = clip;
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Paste from clipboard");

    if (state_.rupeeShuffleActive) {
        ImGui::SameLine();
        ImGui::Checkbox(kRupeeLabel, &state_.progressionRupeesOnly);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "With rupee shuffling enabled in the TP randomizer settings, "
                "this checkbox will\nhide all rupee checks that are irrelevant "
                "for progression (i.e. only contain\nnormal rupees) from the "
                "list down below.");
        }
    }
}

// --- Right pane ------------------------------------------------------------
//
// Inventory + quest-state summary, plus the (collapsed-by-default) detailed
// rando-settings pane.

void TwilightPrincessGame::renderRightPane() {
    if (!state_.inv || !state_.qs) {
        ImGui::TextDisabled("(no save loaded)");
        return;
    }
    const auto& inv = *state_.inv;
    const auto& qs  = *state_.qs;

    // TP encodes health asymmetrically:
    //   maxHealth: 5 units per heart (one per heart-piece — collect 5 = +1 container)
    //   curHealth: 4 units per heart (damage comes in quarter-heart steps)
    // Fresh save at full HP: maxHealth=15, curHealth=12 — both mean "3 hearts".
    const double curHearts = qs.curHealth / 4.0;
    const double maxHearts = qs.maxHealth / 5.0;
    ImGui::TextWrapped("%s   Hearts %g/%g   Rupees %u   Lantern %u/%u",
                qs.playerName.empty() ? "(unset)" : qs.playerName.c_str(),
                curHearts, maxHearts, qs.rupees, qs.curLanternOil, qs.maxLanternOil);
    const auto stageLabel = qs.currentStage.empty()
        ? std::string_view{"(none)"}
        : tpt::core::friendlyStageName(qs.currentStage);
    ImGui::TextWrapped("Stage: %.*s   Form: %s",
                static_cast<int>(stageLabel.size()), stageLabel.data(),
                qs.currentForm ? "Wolf" : "Human");
    ImGui::Separator();

    auto tier = [](std::uint8_t t, std::span<const std::string_view> tiers) {
        return t < tiers.size() ? tiers[t].data() : "?";
    };
    ImGui::TextWrapped("Sword: %s   Bow: %s   Clawshot: %s",
                tier(inv.sword,    tpt::core::kSwordTiers),
                tier(inv.bow,      tpt::core::kBowTiers),
                tier(inv.clawshot, tpt::core::kClawshotTiers));

    std::string have;
    auto add = [&](bool b, const char* name) {
        if (!b) return;
        if (!have.empty()) have += " · ";
        have += name;
    };
    add(inv.lantern,        "Lantern");
    add(inv.galeBoomerang,  "Boomerang");
    add(inv.spinner,        "Spinner");
    add(inv.ballAndChain,   "Ball & Chain");
    add(inv.ironBoots,      "Iron Boots");
    add(inv.slingshot,      "Slingshot");
    add(inv.hawkeye,        "Hawkeye");
    add(inv.shadowCrystal,  "Shadow Crystal");
    add(inv.horseCall,      "Horse Call");
    add(inv.gateKeys,       "Gate Keys");
    add(inv.giantBombBag,   "Giant Bomb Bag");
    ImGui::TextWrapped("Items: %s", have.empty() ? "(none)" : have.c_str());

    ImGui::TextWrapped(
        "Bombs %u/3   Bottles %u/4   Skills %u/7   Shadows %u/3   Shards %u/4",
        inv.bombBags, inv.bottles, inv.hiddenSkills,
        inv.fusedShadows, inv.mirrorShards);
    ImGui::Separator();

    int unlocked = 0;
    for (const auto& p : qs.portals) if (p.unlocked) ++unlocked;
    if (ImGui::CollapsingHeader(("Portals " + std::to_string(unlocked) + "/" +
                                std::to_string(qs.portals.size())).c_str())) {
        for (const auto& p : qs.portals) {
            ImGui::TextColored(p.unlocked ? ImVec4{0.7f,1,0.7f,1} : ImVec4{0.5f,0.5f,0.5f,1},
                               "  %s %.*s", p.unlocked ? "[x]" : "[ ]",
                               static_cast<int>(p.name.size()), p.name.data());
        }
    }

    if (state_.seed && ImGui::CollapsingHeader("Seed")) {
        ImGui::TextWrapped("Name: %s   v%u.%u",
                    state_.seed->seedName.empty() ? "(unset)" : state_.seed->seedName.c_str(),
                    state_.seed->versionMajor, state_.seed->versionMinor);
        ImGui::TextWrapped("Castle: %s (%u)   Wallet: %s   ToT: tier %u",
                    state_.seed->castleRequirements.c_str(),
                    state_.seed->castleRequirementCount,
                    state_.seed->walletSize.c_str(),
                    state_.seed->totEntranceTier);
    }

    renderRandomizerSettingsPane();
}

// --- Settings pane (no-op for now; content lives inside renderRightPane) ---

void TwilightPrincessGame::renderSettingsPane() {}

// --- The big rando-settings collapsing block -------------------------------

void TwilightPrincessGame::renderRandomizerSettingsPane() {
    using namespace settings;

    if (!ImGui::CollapsingHeader("Randomizer settings")) return;

    std::optional<tpt::core::ParsedSettings> parsed;
    if (!state_.settingsString.empty()) {
        try { parsed = tpt::core::decodeSettingsString(state_.settingsString); }
        catch (...) { /* silent — renderOptionsRow surfaces the error */ }
    }
    auto str = [&](const std::string tpt::core::ParsedSettings::* m) -> std::string {
        return parsed ? (*parsed).*m : std::string{};
    };
    auto opt = [&](bool tpt::core::ParsedSettings::* m) -> std::optional<bool> {
        return parsed ? std::optional<bool>{(*parsed).*m} : std::nullopt;
    };

    section("Logic");
    stringOrUnknown("Logic rules",        str(&tpt::core::ParsedSettings::logicRules));
    stringOrUnknown("Faron Woods logic",  str(&tpt::core::ParsedSettings::faronWoodsLogic));
    stringOrUnknown("Ilia Quest",         str(&tpt::core::ParsedSettings::iliaQuest));

    section("Shuffles");
    if (state_.seed) {
        shuffleAutoDetected("Hidden Rupees",
            anyProgressionInCategory(state_, {"Rupee - Hidden"}));
        shuffleAutoDetected("Freestanding Rupees",
            anyProgressionInCategory(state_, {"Rupee - Freestanding"}));
        shuffleAutoDetected("Golden Bugs",
            anyProgressionInCategory(state_, {"Golden Bug"}));
        shuffleAutoDetected("Maps & Compasses",
            anyProgressionInCategory(state_, {"Dungeon Map", "Compass"}));
        shuffleAutoDetected("Shop Items",
            anyProgressionInCategory(state_, {"Shop"}));
    } else {
        unknown("Hidden Rupees");
        unknown("Freestanding Rupees");
        unknown("Golden Bugs");
        unknown("Maps & Compasses");
        unknown("Shop Items");
    }
    boolOrUnknown ("Sky Characters",      opt(&tpt::core::ParsedSettings::shuffleSkyCharacters));
    boolOrUnknown ("Hidden Skills",       opt(&tpt::core::ParsedSettings::shuffleHiddenSkills));
    boolOrUnknown ("NPC Items",           opt(&tpt::core::ParsedSettings::shuffleNpcItems));
    boolOrUnknown ("Dungeon Rewards",     opt(&tpt::core::ParsedSettings::shuffleRewards));
    stringOrUnknown("Small Keys",         str(&tpt::core::ParsedSettings::smallKeySettings));
    stringOrUnknown("Big Keys",           str(&tpt::core::ParsedSettings::bigKeySettings));
    stringOrUnknown("Map & Compass mode", str(&tpt::core::ParsedSettings::mapAndCompassSettings));
    stringOrUnknown("Poes",               str(&tpt::core::ParsedSettings::shufflePoes));

    section("Goal & requirements");
    if (state_.seed) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s (%u)",
            state_.seed->castleRequirements.c_str(), state_.seed->castleRequirementCount);
        value("Castle req",     buf, kNeutralColor);
        std::snprintf(buf, sizeof(buf), "%s (%u)",
            state_.seed->castleBkRequirements.c_str(), state_.seed->castleBkRequirementCount);
        value("Castle BK req",  buf, kNeutralColor);
        value("Palace req",     state_.seed->palaceRequirements.c_str(), kNeutralColor);
        std::snprintf(buf, sizeof(buf), "tier %u", state_.seed->totEntranceTier);
        value("ToT entrance",   buf, kNeutralColor);
        value("Mirror Chamber", state_.seed->mirrorChamberEntrance.c_str(), kNeutralColor);
    } else {
        unknown("Castle req");
        unknown("Castle BK req");
        unknown("Palace req");
        unknown("ToT entrance");
        unknown("Mirror Chamber");
    }

    section("Generation");
    stringOrUnknown("Item Scarcity",      str(&tpt::core::ParsedSettings::itemScarcity));
    stringOrUnknown("Trap Frequency",     str(&tpt::core::ParsedSettings::trapFrequency));
    boolOrUnknown ("Barren Dungeons",     opt(&tpt::core::ParsedSettings::barrenDungeons));

    section("Combat & wallet");
    if (state_.seed) {
        value("Wallet",         state_.seed->walletSize.c_str(), kNeutralColor);
        value("Damage mag",     state_.seed->damageMagnification.c_str(), kNeutralColor);
        char buf[16]; std::snprintf(buf, sizeof(buf), "%u", state_.seed->maloShopDonation);
        value("Malo donation",  buf, kNeutralColor);
    } else {
        unknown("Wallet");
        unknown("Damage mag");
        unknown("Malo donation");
    }

    section("Skips & patches");
    auto seedFlag = [&](std::string_view name) -> std::optional<bool> {
        if (!state_.seed) return std::nullopt;
        for (const auto& f : state_.seed->mapClearFlags)    if (f.name == name) return f.value;
        for (const auto& f : state_.seed->volatilePatches)  if (f.name == name) return f.value;
        return std::nullopt;
    };
    boolOrUnknown("Skip prologue",            seedFlag("skipPrologue"));
    boolOrUnknown("Faron twilight cleared",   seedFlag("faronTwilightCleared"));
    boolOrUnknown("Eldin twilight cleared",   seedFlag("eldinTwilightCleared"));
    boolOrUnknown("Lanayru twilight cleared", seedFlag("lanayruTwilightCleared"));
    boolOrUnknown("Skip MDH",                 seedFlag("skipMdh"));
    boolOrUnknown("Skip minor cutscenes",     seedFlag("skipMinorCutscenes"));
    boolOrUnknown("Open map",                 seedFlag("openMap"));
    boolOrUnknown("Skip Snowpeak entrance",   seedFlag("skipSnowpeakEntrance"));
    boolOrUnknown("Skip Bridge donation",     opt(&tpt::core::ParsedSettings::skipBridgeDonation));
    boolOrUnknown("Skip Lakebed entrance",    opt(&tpt::core::ParsedSettings::skipLakebedEntrance));
    boolOrUnknown("Skip Arbiters entrance",   opt(&tpt::core::ParsedSettings::skipArbitersEntrance));
    boolOrUnknown("Skip Grove entrance",      opt(&tpt::core::ParsedSettings::skipGroveEntrance));
    boolOrUnknown("Skip City entrance",       opt(&tpt::core::ParsedSettings::skipCityEntrance));
    boolOrUnknown("Skip major cutscenes",     opt(&tpt::core::ParsedSettings::skipMajorCutscenes));
    boolOrUnknown("GM shortcut",              opt(&tpt::core::ParsedSettings::gmShortcut));
    boolOrUnknown("HC shortcut",              opt(&tpt::core::ParsedSettings::hcShortcut));
    boolOrUnknown("No small keys on bosses",  opt(&tpt::core::ParsedSettings::noSmallKeysOnBosses));
    boolOrUnknown("Open Door of Time",        opt(&tpt::core::ParsedSettings::openDot));

    section("Convenience");
    auto seedEnabled = [&](std::string_view name) -> std::optional<bool> {
        if (!state_.seed) return std::nullopt;
        for (const auto& f : state_.seed->seedFlags) if (f.name == name) return f.value;
        return std::nullopt;
    };
    boolOrUnknown("Transform anywhere",     seedEnabled("TRANSFORM_ANYWHERE"));
    boolOrUnknown("Quick transform",        seedEnabled("QUICK_TRANSFORM"));
    boolOrUnknown("Increase spinner speed", seedEnabled("INCREASE_SPINNER_SPEED"));
    boolOrUnknown("Bonks do damage",        seedEnabled("BONKS_DO_DAMAGE"));
    boolOrUnknown("Auto-fill wallets",      seedEnabled("AUTOFILL_WALLETS"));
    boolOrUnknown("Modify shop models",     seedEnabled("MODIFY_SHOP_MODELS"));
    boolOrUnknown("Rainbow lantern",        seedEnabled("RAINBOW_LANTERN"));
    boolOrUnknown("Rainbow Midna",          seedEnabled("RAINBOW_MIDNA"));
    boolOrUnknown("Rainbow light sword",    seedEnabled("RAINBOW_LIGHT_SWORD"));
    boolOrUnknown("Light sword always on",  seedEnabled("LIGHT_SWORD_ALWAYS_ON"));
    boolOrUnknown("Instant text",           opt(&tpt::core::ParsedSettings::instantText));
    boolOrUnknown("Fast iron boots",        opt(&tpt::core::ParsedSettings::fastIronBoots));

    section("Entrance shuffle");
    stringOrUnknown("Goron Mines entrance",    str(&tpt::core::ParsedSettings::goronMinesEntrance));
    stringOrUnknown("ToT entrance mode",       str(&tpt::core::ParsedSettings::totEntrance));
    stringOrUnknown("Dungeon entrances",       str(&tpt::core::ParsedSettings::shuffleDungeonEntrances));
    boolOrUnknown ("Unpair entrances",         opt(&tpt::core::ParsedSettings::unpairEntrances));
    boolOrUnknown ("Decouple entrances",       opt(&tpt::core::ParsedSettings::decoupleEntrances));
    boolOrUnknown ("Randomize starting point", opt(&tpt::core::ParsedSettings::randomizeStartingPoint));

    section("Hints");
    stringOrUnknown("Distribution",             str(&tpt::core::ParsedSettings::hintDistribution));
    stringOrUnknown("Importance",               str(&tpt::core::ParsedSettings::hintImportance));
    boolOrUnknown ("No plando hints",           opt(&tpt::core::ParsedSettings::noPlandoHints));
    boolOrUnknown ("Adjust for completionists", opt(&tpt::core::ParsedSettings::adjustHintsForCompletionists));
    boolOrUnknown ("Hint dungeon entrances",    opt(&tpt::core::ParsedSettings::hintDungeonEntrances));
}

// --- Prefs -----------------------------------------------------------------

void TwilightPrincessGame::loadPrefs(const nlohmann::json& sub) {
    state_.settingsString        = sub.value("settingsString",        std::string{});
    state_.progressionRupeesOnly = sub.value("progressionRupeesOnly", false);
}

nlohmann::json TwilightPrincessGame::savePrefs() const {
    nlohmann::json j;
    j["settingsString"]        = state_.settingsString;
    j["progressionRupeesOnly"] = state_.progressionRupeesOnly;
    return j;
}

// --- Source selection ------------------------------------------------------

std::unique_ptr<tpt::memory::MemorySource> TwilightPrincessGame::defaultSource() const {
    return tpt::memory::makeMemorySource(tpt::memory::SourceKind::Auto);
}

}  // namespace tpt::game::tp
