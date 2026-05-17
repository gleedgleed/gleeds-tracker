#include "game/oot/OcarinaOfTimeGame.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

#include "game/oot/logic/ContextBuilder.h"
#include "game/oot/logic/Reach.h"
#include "game/oot/logic/WorldGraph.h"
#include "game/oot/save/SaveOffsets.h"
#include "memory/MemorySource.h"
#include "p64/Project64Source.h"
#include "ui/UIState.h"

namespace tpt::game::oot {

namespace {

// Find data/* next to the running executable. Mirrors
// findDataDirRelativeToExe() in src/game/tp/TwilightPrincessGame.cpp —
// will be lifted into a shared utility once a third game lands.
std::filesystem::path findExeDataDir() {
    namespace fs = std::filesystem;
#ifdef _WIN32
    char buf[260] = {};
    const DWORD n = ::GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf)) {
        return fs::path(buf).parent_path() / "data";
    }
#endif
    return fs::current_path() / "data";
}

}  // namespace

bool OcarinaOfTimeGame::loadWorldData(std::ostream& errlog) {
    const auto dataDir = findExeDataDir();
    const auto locationsPath = dataDir / "oot" / "locations.json";
    if (!loadChecks(locationsPath, checks_, errlog)) {
        state_.error = "OoT: failed to load " + locationsPath.string();
        snapshot_.worldLoaded = false;
        return false;
    }

    // Build the master-by-stage view once. Stable ordering: locations
    // preserve OoTR's source ordering (which roughly tracks region
    // discovery) so the user sees Kokiri Forest checks before Lost
    // Woods, before Hyrule Field, etc.
    state_.masterByStage.clear();
    for (const auto& c : checks_) {
        state_.masterByStage[c.area].push_back(c.name);
    }

    // World graph + aliases for reachability. Failure here demotes us
    // to "checks-only" mode rather than aborting: the user still gets
    // the all-checks tree and completion status; only the reachable
    // tab stays empty.
    const auto worldDir = dataDir / "oot-world";
    const auto helpersPath = dataDir / "oot" / "LogicHelpers.json";
    try {
        auto raw = logic::loadRegions(worldDir);
        world_   = logic::compileWorld(raw);
        aliases_ = logic::loadAliases(helpersPath, errlog);
    } catch (const std::exception& e) {
        errlog << "OoT: world graph load failed — reach disabled. "
               << e.what() << "\n";
        world_.clear();
        aliases_.clear();
    }

    // Build the location-name → region-name map. Used in poll() to
    // route the BFS-reachable-location output back into our per-area
    // `state.reachableByStage` view (keyed by Check.area, not region).
    locationToRegion_.clear();
    for (const auto& [regionName, region] : world_) {
        for (const auto& loc : region.locations) {
            locationToRegion_[loc.target] = regionName;
        }
    }

    snapshot_.worldLoaded = true;
    state_.worldLoaded    = true;
    return true;
}

void OcarinaOfTimeGame::poll(tpt::memory::MemorySource& mem) {
    state_.sourceName     = mem.sourceName();
    state_.emulatorHooked = mem.isConnected();
    state_.gameId         = mem.gameId();

    // Fetch SaveContext header (PlayerData + Inventory). When the read
    // fails or the save-loaded magic is missing, clear the decoded
    // values so stale data doesn't linger across emulator restarts.
    saveBuf_.assign(save::kHeaderReadSize, 0);
    const bool readOk = mem.isConnected() &&
        mem.readBytes(save::kSaveContextAddr, saveBuf_.data(), saveBuf_.size());
    if (!readOk) {
        playerData_  = {};
        inventory_   = {};
        saveFlags_   = {};
        xflagState_  = {};
        snapshot_.saveLoaded = false;
        state_.saveLoaded    = false;
        state_.allByStage.clear();
        state_.completed.clear();
        state_.totalCompleted = state_.totalResolvable = 0;
        return;
    }

    playerData_ = save::readPlayerData(saveBuf_);
    inventory_  = playerData_.saveLoaded ? save::readInventory(saveBuf_)
                                         : save::Inventory{};
    saveFlags_  = playerData_.saveLoaded ? save::readSaveFlags(saveBuf_)
                                         : save::SaveFlags{};
    snapshot_.saveLoaded = playerData_.saveLoaded;
    state_.saveLoaded    = playerData_.saveLoaded;

    // Fetch the OoTR xflag tables once per session (they're ROM-baked
    // and stable while a single seed is loaded). Then refresh the
    // collectible_override_flags bytes every poll — that pointer is
    // heap-allocated at game start so the *pointer address* is fixed
    // but its bytes evolve as the player collects things. Also refresh
    // the extended_savectx slice we read (collected_dungeon_rewards)
    // every poll — that's per-save state, not a static table.
    if (playerData_.saveLoaded) {
        // Resolve OoTR symbol addresses once per session via RANDO_CONTEXT
        // + per-build table lookup. Symbols are at fixed addresses for a
        // given ROM (linker output is deterministic), so once we identify
        // the build the addresses are stable. Unknown builds get
        // zero-valued symbol slots → fetch functions fail closed.
        if (!xflagState_.ootrAddrs.valid) {
            save::resolveOotrAddrs(mem, xflagState_.ootrAddrs);
        }
        if (xflagState_.ootrAddrs.valid && !xflagState_.valid) {
            save::fetchXflagTables(mem, xflagState_);
        }
        if (xflagState_.valid) {
            save::fetchCollectibleFlags(mem, xflagState_);
        }
        save::fetchExtendedSavectx(mem, xflagState_);
    } else {
        xflagState_ = {};
    }

    // Rebuild check views from current flags. Cheap (~2k entries, each
    // decode is a few bit ops). Stored in state_.allByStage so the
    // game-agnostic UI shell renders the left column directly.
    state_.allByStage.clear();
    state_.completed.clear();
    int doneCount = 0;
    int resolvableCount = 0;
    for (const auto& c : checks_) {
        if (shouldHideCheck(c)) continue;  // hidden by inactive shuffle
        const auto done = isCheckComplete(c, playerData_, inventory_, saveFlags_, xflagState_);
        tpt::ui::CheckStatus status = tpt::ui::CheckStatus::Unknown;
        if (done.has_value()) {
            ++resolvableCount;
            status = *done ? tpt::ui::CheckStatus::Done
                           : tpt::ui::CheckStatus::Pending;
            if (*done) {
                ++doneCount;
                state_.completed.insert(c.name);
            }
        }
        state_.allByStage[c.area].push_back({c.name, status});
    }
    state_.totalCompleted  = doneCount;
    state_.totalResolvable = resolvableCount;

    // Reachability: only when world graph loaded successfully.
    state_.reachableByStage.clear();
    state_.reachedRooms.clear();
    state_.pendingSet.clear();
    state_.totalReachablePending = 0;
    if (!world_.empty()) {
        auto ctx = logic::buildContext(playerData_, inventory_, saveFlags_);
        const auto reachable = logic::reach(world_, aliases_, ctx);

        // Carry the BFS region set into State so the UI shell + future
        // tooltips can surface it.
        for (const auto& r : ctx.reachedRegions) state_.reachedRooms.insert(r);

        // Project the BFS-reachable location names back into the
        // check-list area buckets, filtered to "pending and resolvable"
        // — same semantics as the TP middle pane.
        int reachablePending = 0;
        std::unordered_set<std::string> reachableNames(reachable.begin(),
                                                       reachable.end());
        for (const auto& c : checks_) {
            if (!reachableNames.count(c.name)) continue;
            if (shouldHideCheck(c)) continue;     // hidden by inactive shuffle
            const auto done = isCheckComplete(c, playerData_, inventory_, saveFlags_, xflagState_);
            if (done.value_or(false)) continue;   // already complete — skip

            // Show pending checks AND unknowns (Unsupported types whose
            // completion mapping isn't implemented yet). Unknowns render
            // as [?] so the user can see they're reachable in the world
            // graph even though we can't auto-track completion.
            const auto status = done.has_value() ? tpt::ui::CheckStatus::Pending
                                                 : tpt::ui::CheckStatus::Unknown;
            state_.reachableByStage[c.area].push_back({c.name, status});
            if (done.has_value()) {
                state_.pendingSet.insert(c.name);
                ++reachablePending;
            }
        }
        state_.totalReachablePending = reachablePending;
    }
}

const std::vector<tpt::ui::FilterSpec>& OcarinaOfTimeGame::filterSpecs() const {
    // No filter tabs until the world graph + check categorization land.
    static const std::vector<tpt::ui::FilterSpec> kEmpty{};
    return kEmpty;
}

void OcarinaOfTimeGame::renderOptionsRow() {
    ImGui::Checkbox("Show OoTR-extended shuffles", &overrideShowOotrShuffles_);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Force-show pots, crates, wonderitems, silver rupees, freestanding\n"
            "items, beehives, drops, hints, and other OoTR-specific shuffles.\n"
            "Off by default because most seeds don't shuffle them — when\n"
            "OoTR settings-string parsing lands this toggle goes away and\n"
            "each shuffle auto-detects per seed.");
    }
}

void OcarinaOfTimeGame::renderRightPane() {
    if (!playerData_.saveLoaded) {
        ImGui::TextDisabled("(no save loaded)");
        ImGui::Spacing();
        ImGui::TextDisabled("World graph + OoTR logic still pending.");
        return;
    }
    const auto& pd  = playerData_;
    const auto& inv = inventory_;

    const double curHearts = pd.health / 16.0;
    const double maxHearts = pd.healthCapacity / 16.0;
    const int magicCap = pd.isDoubleMagicAcquired ? 96
                       : pd.isMagicAcquired       ? 48 : 0;

    ImGui::TextWrapped("%s   %s   Hearts %g/%g   Rupees %d",
                       pd.playerName.empty() ? "(unset)" : pd.playerName.c_str(),
                       pd.isAdult ? "Adult" : "Child",
                       curHearts, maxHearts,
                       static_cast<int>(pd.rupees));
    if (magicCap > 0) {
        ImGui::Text("Magic %d/%d%s", static_cast<int>(pd.magic), magicCap,
                    pd.isDoubleMagicAcquired ? " (double)" : "");
    }
    if (pd.isDoubleDefenseAcquired) ImGui::TextDisabled("Double Defense");

    ImGui::Separator();
    ImGui::Text("Sword: %.*s   Shield: %.*s",
                static_cast<int>(save::swordLabel(inv).size()),  save::swordLabel(inv).data(),
                static_cast<int>(save::shieldLabel(inv).size()), save::shieldLabel(inv).data());
    ImGui::Text("Tunic: %.*s   Boots: %.*s",
                static_cast<int>(save::tunicLabel(inv).size()), save::tunicLabel(inv).data(),
                static_cast<int>(save::bootsLabel(inv).size()), save::bootsLabel(inv).data());
    ImGui::Text("Wallet: %.*s",
                static_cast<int>(save::walletLabel(inv).size()), save::walletLabel(inv).data());
    if (inv.strength) {
        ImGui::Text("Strength: %.*s",
                    static_cast<int>(save::strengthLabel(inv).size()),
                    save::strengthLabel(inv).data());
    }
    if (inv.scale) {
        ImGui::Text("Scale: %.*s",
                    static_cast<int>(save::scaleLabel(inv).size()),
                    save::scaleLabel(inv).data());
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Stones / Medallions");
    ImGui::Text("  %s Kokiri  %s Goron  %s Zora",
                inv.kokiriEmerald ? "[x]" : "[ ]",
                inv.goronRuby     ? "[x]" : "[ ]",
                inv.zoraSapphire  ? "[x]" : "[ ]");
    ImGui::Text("  %s Forest  %s Fire   %s Water",
                inv.medallionForest ? "[x]" : "[ ]",
                inv.medallionFire   ? "[x]" : "[ ]",
                inv.medallionWater  ? "[x]" : "[ ]");
    ImGui::Text("  %s Spirit  %s Shadow %s Light",
                inv.medallionSpirit ? "[x]" : "[ ]",
                inv.medallionShadow ? "[x]" : "[ ]",
                inv.medallionLight  ? "[x]" : "[ ]");

    ImGui::Separator();
    ImGui::TextUnformatted("Songs");
    ImGui::Text("  %s Lullaby  %s Epona    %s Saria",
                inv.songLullaby ? "[x]" : "[ ]",
                inv.songEpona   ? "[x]" : "[ ]",
                inv.songSaria   ? "[x]" : "[ ]");
    ImGui::Text("  %s Sun      %s Time     %s Storms",
                inv.songSun     ? "[x]" : "[ ]",
                inv.songTime    ? "[x]" : "[ ]",
                inv.songStorms  ? "[x]" : "[ ]");
    ImGui::Text("  %s Minuet   %s Bolero   %s Serenade",
                inv.songMinuet   ? "[x]" : "[ ]",
                inv.songBolero   ? "[x]" : "[ ]",
                inv.songSerenade ? "[x]" : "[ ]");
    ImGui::Text("  %s Requiem  %s Nocturne %s Prelude",
                inv.songRequiem  ? "[x]" : "[ ]",
                inv.songNocturne ? "[x]" : "[ ]",
                inv.songPrelude  ? "[x]" : "[ ]");

    ImGui::Separator();
    ImGui::Text("Skulltulas: %d", static_cast<int>(inv.gsTokens));
    if (pd.hasBiggoronSword) ImGui::Text("Biggoron Sword");
    if (inv.stoneOfAgony)    ImGui::Text("Stone of Agony");
    if (inv.gerudosCard)     ImGui::Text("Gerudo's Card");

    ImGui::Separator();
    ImGui::TextDisabled("World graph + OoTR logic still pending.");
}

void OcarinaOfTimeGame::renderSettingsPane() {
    // No detailed settings pane yet.
}

void OcarinaOfTimeGame::loadPrefs(const nlohmann::json& sub) {
    // Round-trip the full blob so future build's prefs stay intact
    // even when the active build doesn't yet know all the keys.
    prefs_ = sub.is_object() ? sub : nlohmann::json::object();
    overrideShowOotrShuffles_ = prefs_.value("overrideShowOotrShuffles", false);
}

nlohmann::json OcarinaOfTimeGame::savePrefs() const {
    auto j = prefs_;
    j["overrideShowOotrShuffles"] = overrideShowOotrShuffles_;
    return j;
}

// ---- Per-shuffle predicates -----------------------------------------------
//
// All of these currently return false (= "shuffle is off in this seed").
// The structure mirrors how OoTR's `SettingsList.py` exposes each shuffle
// as its own enum. When we add OoTR settings-string parsing, replace each
// body with the appropriate `ctx.settings[…]` check; the rest of the
// pipeline (filter dispatch, UI) doesn't need to change.
//
// Until then, the `overrideShowOotrShuffles_` master flag forces all
// predicates to true so power users can preview what tracking those
// types would look like.

bool OcarinaOfTimeGame::isPotShuffleEnabled() const {
    // TODO: return ctx.settings["shuffle_pots"] != "off";
    return overrideShowOotrShuffles_;
}
bool OcarinaOfTimeGame::isCrateShuffleEnabled() const {
    // TODO: return ctx.settings["shuffle_crates"] != "off";
    // Covers both `Crate` and `SmallCrate` in OoTR's location types.
    return overrideShowOotrShuffles_;
}
bool OcarinaOfTimeGame::isFlyingPotShuffleEnabled() const {
    // TODO: return ctx.settings["shuffle_flying_pots"] != "off";
    return overrideShowOotrShuffles_;
}
bool OcarinaOfTimeGame::isBeehiveShuffleEnabled() const {
    // TODO: return ctx.settings["shuffle_beehives"];
    return overrideShowOotrShuffles_;
}
bool OcarinaOfTimeGame::isWonderitemShuffleEnabled() const {
    // TODO: return ctx.settings["shuffle_wonderitems"];
    return overrideShowOotrShuffles_;
}
bool OcarinaOfTimeGame::isSilverRupeeShuffleEnabled() const {
    // TODO: return ctx.settings["shuffle_silver_rupees"] != "off";
    return overrideShowOotrShuffles_;
}
bool OcarinaOfTimeGame::isFreestandingShuffleEnabled() const {
    // TODO: return ctx.settings["shuffle_freestanding_items"] != "off";
    return overrideShowOotrShuffles_;
}
bool OcarinaOfTimeGame::isRupeeTowerShuffleEnabled() const {
    // TODO: read from OoTR's rupee-tower shuffle setting.
    return overrideShowOotrShuffles_;
}
bool OcarinaOfTimeGame::isDropShuffleEnabled() const {
    // TODO: return ctx.settings["shuffle_grass"] (drops live under grass shuffle).
    return overrideShowOotrShuffles_;
}
bool OcarinaOfTimeGame::isGrottoScrubShuffleEnabled() const {
    // TODO: return ctx.settings["shuffle_scrubs"] covers grotto scrubs.
    return overrideShowOotrShuffles_;
}
bool OcarinaOfTimeGame::isHintShuffleEnabled() const {
    // TODO: return ctx.settings["hints"] != "none";
    return overrideShowOotrShuffles_;
}

bool OcarinaOfTimeGame::shouldHideCheck(const Check& c) const {
    // Per-type dispatch. The list is the inverse of the "always-show"
    // vanilla-trackable types (Chest/Boss/Song/Cutscene/Event/BossHeart/
    // Collectable/Scrub/Shop/MaskShop/NPC/GS Token) — anything not here
    // falls through to "show".
    const auto& t = c.typeRaw;
    if (t == "Pot")          return !isPotShuffleEnabled();
    if (t == "Crate" ||
        t == "SmallCrate")   return !isCrateShuffleEnabled();
    if (t == "FlyingPot")    return !isFlyingPotShuffleEnabled();
    if (t == "Beehive")      return !isBeehiveShuffleEnabled();
    if (t == "Wonderitem")   return !isWonderitemShuffleEnabled();
    if (t == "SilverRupee")  return !isSilverRupeeShuffleEnabled();
    if (t == "Freestanding") return !isFreestandingShuffleEnabled();
    if (t == "RupeeTower")   return !isRupeeTowerShuffleEnabled();
    if (t == "Drop")         return !isDropShuffleEnabled();
    if (t == "GrottoScrub")  return !isGrottoScrubShuffleEnabled();
    if (t == "Hint" ||
        t == "HintStone")    return !isHintShuffleEnabled();
    return false;
}

std::unique_ptr<tpt::memory::MemorySource> OcarinaOfTimeGame::defaultSource() const {
    // BizHawk / RetroArch / Mupen attach paths are TODO — each emulator
    // has its own DLL-anchored RDRAM discovery. For v1, OoT is
    // Project64-only on the source side. When other emulators land, this
    // method gains the same auto-detect-among-supported-sources pattern
    // that makeMemorySource(Auto) provides for TP.
    return std::make_unique<tpt::p64::Source>();
}

}  // namespace tpt::game::oot
