#include "cli/Headless.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>

#include <filesystem>
#include <map>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "core/CheckPlacements.h"
#include "core/EventFlags.h"
#include "core/Items.h"
#include "core/CheckSaveBindings.h"
#include "core/QuestState.h"
#include "core/Region.h"
#include "core/SaveOffsets.h"
#include "core/SeedHeader.h"
#include "core/SettingsString.h"
#include "core/Stages.h"
#include "core/logic/ContextBuilder.h"
#include "core/logic/Evaluator.h"
#include "core/logic/Parser.h"
#include "core/logic/Reach.h"
#include "core/logic/WorldData.h"
#include "dolphin/DolphinClient.h"
#include "dusk/DuskSource.h"
#include "game/oot/Checks.h"
#include "game/oot/logic/AliasTable.h"
#include "game/oot/logic/ContextBuilder.h"
#include "game/oot/logic/Evaluator.h"
#include "game/oot/logic/JsonPrep.h"
#include "game/oot/logic/Reach.h"
#include "game/oot/logic/RuleParser.h"
#include "game/oot/logic/WorldGraph.h"
#include "game/oot/save/Inventory.h"
#include "game/oot/save/PlayerData.h"
#include "game/oot/save/SaveFlags.h"
#include "game/oot/save/SaveOffsets.h"
#include "p64/Project64Source.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace tpt::cli {

namespace {

bool startsWith(std::string_view s, std::string_view p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

bool parseHexU32(std::string_view s, std::uint32_t& out) {
    if (startsWith(s, "0x") || startsWith(s, "0X")) s.remove_prefix(2);
    if (s.empty() || s.size() > 8) return false;
    std::uint32_t v = 0;
    for (char c : s) {
        v <<= 4;
        if (c >= '0' && c <= '9')      v |= static_cast<std::uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') v |= static_cast<std::uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= static_cast<std::uint32_t>(c - 'A' + 10);
        else return false;
    }
    out = v;
    return true;
}

bool parseUint(std::string_view s, std::uint32_t& out) {
    if (s.empty()) return false;
    std::uint32_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<std::uint32_t>(c - '0');
    }
    out = v;
    return true;
}

// Wait up to `timeoutSec` seconds for the memory source to connect. Polls
// the source's connect() every 250ms. Works for any MemorySource subclass
// — though today the only concrete source is Dolphin.
bool waitForConnection(tpt::memory::MemorySource& mem, int timeoutSec) {
    const Uint64 deadline = SDL_GetTicks() + static_cast<Uint64>(timeoutSec) * 1000;
    while (true) {
        mem.connect();
        if (mem.isConnected()) return true;
        if (SDL_GetTicks() >= deadline) return false;
        SDL_Delay(250);
    }
}

const char* tier(std::uint8_t t, std::span<const std::string_view> tiers) {
    return t < tiers.size() ? tiers[t].data() : "?";
}

void printInventory(const tpt::core::Inventory& inv) {
    std::printf("=== Inventory ===\n");
    std::printf("Sword: %s   Shield: %s   Tunic: %s   Wallet: %s\n",
        tier(inv.sword, tpt::core::kSwordTiers),
        inv.hylianShield ? "Hylian" : inv.ordonShield ? "Ordon" : "-",
        inv.magicArmor ? "Magic Armor" : inv.zoraArmor ? "Zora" : "-",
        tier(inv.wallet, tpt::core::kWalletTiers));
    std::printf("Bow: %s   Clawshot: %s   Dominion Rod: %s   Fishing Rod: %s\n",
        tier(inv.bow, tpt::core::kBowTiers),
        tier(inv.clawshot, tpt::core::kClawshotTiers),
        tier(inv.dominionRod, tpt::core::kDominionTiers),
        tier(inv.fishingRod, tpt::core::kFishingTiers));

    constexpr std::array<std::pair<const char*, bool tpt::core::Inventory::*>, 13> kFlags{{
        {"Lantern",        &tpt::core::Inventory::lantern},
        {"Gale Boomerang", &tpt::core::Inventory::galeBoomerang},
        {"Spinner",        &tpt::core::Inventory::spinner},
        {"Ball & Chain",   &tpt::core::Inventory::ballAndChain},
        {"Iron Boots",     &tpt::core::Inventory::ironBoots},
        {"Slingshot",      &tpt::core::Inventory::slingshot},
        {"Hawkeye",        &tpt::core::Inventory::hawkeye},
        {"Shadow Crystal", &tpt::core::Inventory::shadowCrystal},
        {"Horse Call",     &tpt::core::Inventory::horseCall},
        {"Auru's Memo",    &tpt::core::Inventory::auruMemo},
        {"Ashei's Sketch", &tpt::core::Inventory::asheiSketch},
        {"Gate Keys",      &tpt::core::Inventory::gateKeys},
        {"Giant Bomb Bag", &tpt::core::Inventory::giantBombBag},
    }};
    std::printf("Items:  ");
    bool first = true;
    for (auto [label, member] : kFlags) {
        if (inv.*member) {
            std::printf("%s%s", first ? "" : ", ", label);
            first = false;
        }
    }
    if (first) std::printf("-");
    std::printf("\n");

    std::printf("Bomb Bags: %u/3   Bottles: %u/4   Poe Souls: %u   "
                "Hidden Skills: %u/7   Fused Shadows: %u/3   Mirror Shards: %u/4\n",
                inv.bombBags, inv.bottles, inv.poeSouls,
                inv.hiddenSkills, inv.fusedShadows, inv.mirrorShards);

    std::printf("Golden Bugs: %zu/24", inv.bugs.size());
    if (!inv.bugs.empty()) {
        std::vector<std::string> sorted(inv.bugs.begin(), inv.bugs.end());
        std::sort(sorted.begin(), sorted.end());
        std::printf("  (");
        for (std::size_t i = 0; i < sorted.size(); ++i) {
            std::printf("%s%s", i ? ", " : "", sorted[i].c_str());
        }
        std::printf(")");
    }
    std::printf("\n");

    std::printf("Dungeon Items:\n");
    constexpr std::array<const char*, 9> kOrder{
        "Forest Temple", "Goron Mines", "Lakebed Temple",
        "Arbiters Grounds", "Snowpeak Ruins", "Temple of Time",
        "City in The Sky", "Palace of Twilight", "Hyrule Castle"};
    for (const char* name : kOrder) {
        auto it = inv.dungeonItems.find(name);
        if (it == inv.dungeonItems.end()) continue;
        const auto& di = it->second;
        std::vector<const char*> flags;
        if (di.hasMap)     flags.push_back("Map");
        if (di.hasCompass) flags.push_back("Compass");
        if (di.hasBigKey)  flags.push_back("Big Key");
        std::printf("  %-20s keys: %u   ", name, di.smallKeys);
        if (flags.empty()) std::printf("-");
        else for (std::size_t i = 0; i < flags.size(); ++i)
            std::printf("%s%s", i ? ", " : "", flags[i]);
        std::printf("\n");
    }
}

const char* twilightLabel(std::uint8_t level) {
    return level < tpt::core::kTwilightLevels.size()
        ? tpt::core::kTwilightLevels[level].data() : "?";
}

void printQuestState(const tpt::core::QuestState& qs) {
    // See Render.cpp note: TP encodes max in 5ths-of-a-heart (per heart
    // piece) and current in 4ths-of-a-heart (quarter-heart damage steps).
    const double curHearts = qs.curHealth / 4.0;
    const double maxHearts = qs.maxHealth / 5.0;

    // Translate the raw 8-char stage code (e.g. "F_SP108") into a friendly
    // area name ("Faron Woods") for display, but keep the raw code in
    // parens so the headless dump stays useful for debugging.
    const auto stageLabel = qs.currentStage.empty()
        ? std::string_view{"(none)"}
        : tpt::core::friendlyStageName(qs.currentStage);
    std::printf("=== Quest State ===\n");
    std::printf("Name: %s   Stage: %.*s [%s] (room %u, spawn %u)   Form: %s\n",
                qs.playerName.empty() ? "(unset)" : qs.playerName.c_str(),
                static_cast<int>(stageLabel.size()), stageLabel.data(),
                qs.currentStage.empty() ? "" : qs.currentStage.c_str(),
                qs.roomId, qs.spawnPoint,
                qs.currentForm ? "Wolf" : "Human");
    std::printf("Hearts: %g/%g   Rupees: %u   Lantern Oil: %u/%u\n",
                curHearts, maxHearts, qs.rupees,
                qs.curLanternOil, qs.maxLanternOil);
    std::printf("Twilight cleared up to: %s   Transformed up to: %s\n",
                twilightLabel(qs.darkClearLevel),
                twilightLabel(qs.transformLevel));
    std::printf("Tears  Faron: %u/16  Eldin: %u/16  Lanayru: %u/16\n",
                qs.faronTears, qs.eldinTears, qs.lanayruTears);
    std::printf("Deaths: %u\n", qs.deaths);

    std::size_t unlocked = 0;
    for (const auto& p : qs.portals) if (p.unlocked) ++unlocked;
    std::printf("Portals (%zu/%zu):\n", unlocked, qs.portals.size());
    std::printf("  Unlocked: ");
    bool first = true;
    for (const auto& p : qs.portals) if (p.unlocked) {
        std::printf("%s%.*s", first ? "" : ", ",
                    static_cast<int>(p.name.size()), p.name.data());
        first = false;
    }
    if (first) std::printf("(none)");
    std::printf("\n  Locked:   ");
    first = true;
    for (const auto& p : qs.portals) if (!p.unlocked) {
        std::printf("%s%.*s", first ? "" : ", ",
                    static_cast<int>(p.name.size()), p.name.data());
        first = false;
    }
    if (first) std::printf("(none)");
    std::printf("\n");

    std::printf("Switch keys:\n");
    for (const auto& s : qs.switchKeys) {
        std::printf("  [%s] %.*s\n", s.open ? "x" : " ",
                    static_cast<int>(s.name.size()), s.name.data());
    }
}

void printSeedSettings(const tpt::core::SeedSettings& s) {
    std::printf("=== Seed Settings ===\n");
    std::printf("Name: %s   Version: %u.%u   Found at: 0x%08X\n",
                s.seedName.empty() ? "(unset)" : s.seedName.c_str(),
                s.versionMajor, s.versionMinor, s.foundAt);
    std::printf("Castle: %s (%u)   Castle BK: %s (%u)   Palace: %s\n",
                s.castleRequirements.c_str(), s.castleRequirementCount,
                s.castleBkRequirements.c_str(), s.castleBkRequirementCount,
                s.palaceRequirements.c_str());
    std::printf("Wallet: %s   ToT Entrance: tier %u   Mirror Chamber: %s   Damage: %s\n",
                s.walletSize.c_str(), s.totEntranceTier,
                s.mirrorChamberEntrance.c_str(),
                s.damageMagnification.c_str());

    std::printf("Map flags: ");
    bool first = true;
    for (const auto& f : s.mapClearFlags) if (f.value) {
        std::printf("%s%s", first ? "" : ", ", f.name.c_str());
        first = false;
    }
    if (first) std::printf("none");
    std::printf("\n");

    if (!s.seedFlags.empty()) {
        std::printf("Seed flags ON:  ");
        first = true;
        for (const auto& f : s.seedFlags) if (f.value) {
            std::printf("%s%s", first ? "" : ", ", f.name.c_str());
            first = false;
        }
        if (first) std::printf("none");
        std::printf("\nSeed flags OFF: ");
        first = true;
        for (const auto& f : s.seedFlags) if (!f.value) {
            std::printf("%s%s", first ? "" : ", ", f.name.c_str());
            first = false;
        }
        if (first) std::printf("none");
        std::printf("\n");
    }
    if (!s.volatilePatches.empty()) {
        std::printf("Volatile patches ON: ");
        first = true;
        for (const auto& f : s.volatilePatches) if (f.value) {
            std::printf("%s%s", first ? "" : ", ", f.name.c_str());
            first = false;
        }
        if (first) std::printf("none");
        std::printf("\n");
    }
}

void printParsedSettings(const tpt::core::ParsedSettings& s) {
    std::printf("=== Parsed Settings String ===\n");
    std::printf("version=0x%X\n", s.version);

    constexpr std::array<std::pair<const char*, const std::string tpt::core::ParsedSettings::*>, 20>
        kEnums{{
        {"logicRules",          &tpt::core::ParsedSettings::logicRules},
        {"castleRequirements",  &tpt::core::ParsedSettings::castleRequirements},
        {"castleBK",            &tpt::core::ParsedSettings::castleBkRequirements},
        {"palaceRequirements",  &tpt::core::ParsedSettings::palaceRequirements},
        {"faronWoodsLogic",     &tpt::core::ParsedSettings::faronWoodsLogic},
        {"smallKeys",           &tpt::core::ParsedSettings::smallKeySettings},
        {"bigKeys",             &tpt::core::ParsedSettings::bigKeySettings},
        {"mapAndCompass",       &tpt::core::ParsedSettings::mapAndCompassSettings},
        {"walletSize",          &tpt::core::ParsedSettings::walletSize},
        {"totEntrance",         &tpt::core::ParsedSettings::totEntrance},
        {"goronMines",          &tpt::core::ParsedSettings::goronMinesEntrance},
        {"damageMagnification", &tpt::core::ParsedSettings::damageMagnification},
        {"iliaQuest",           &tpt::core::ParsedSettings::iliaQuest},
        {"mirrorChamber",       &tpt::core::ParsedSettings::mirrorChamberEntrance},
        {"dungeonER",           &tpt::core::ParsedSettings::shuffleDungeonEntrances},
        {"itemScarcity",        &tpt::core::ParsedSettings::itemScarcity},
        {"trapFrequency",       &tpt::core::ParsedSettings::trapFrequency},
        {"startingToD",         &tpt::core::ParsedSettings::startingTod},
        {"shufflePoes",         &tpt::core::ParsedSettings::shufflePoes},
        {"hints",               &tpt::core::ParsedSettings::hintDistribution},
    }};
    for (const auto& [label, member] : kEnums) {
        std::printf("  %s: %s\n", label, (s.*member).c_str());
    }
    if (s.castleRequirementCount)   std::printf("  castleRequirementCount: %u\n", s.castleRequirementCount);
    if (s.castleBkRequirementCount) std::printf("  castleBKRequirementCount: %u\n", s.castleBkRequirementCount);
    if (s.maloShopDonation)         std::printf("  maloShopDonation: %u\n", s.maloShopDonation);

    constexpr std::array<std::pair<const char*, bool tpt::core::ParsedSettings::*>, 39> kBoolFlags{{
        {"shuffleGoldenBugs",     &tpt::core::ParsedSettings::shuffleGoldenBugs},
        {"shuffleSkyCharacters",  &tpt::core::ParsedSettings::shuffleSkyCharacters},
        {"shuffleNpcItems",       &tpt::core::ParsedSettings::shuffleNpcItems},
        {"shuffleShopItems",      &tpt::core::ParsedSettings::shuffleShopItems},
        {"shuffleHiddenSkills",   &tpt::core::ParsedSettings::shuffleHiddenSkills},
        {"shuffleHiddenRupees",   &tpt::core::ParsedSettings::shuffleHiddenRupees},
        {"shuffleRewards",        &tpt::core::ParsedSettings::shuffleRewards},
        {"shuffleFreestandingRupees", &tpt::core::ParsedSettings::shuffleFreestandingRupees},
        {"skipPrologue",          &tpt::core::ParsedSettings::skipPrologue},
        {"skipMdh",               &tpt::core::ParsedSettings::skipMdh},
        {"skipMinorCutscenes",    &tpt::core::ParsedSettings::skipMinorCutscenes},
        {"skipMajorCutscenes",    &tpt::core::ParsedSettings::skipMajorCutscenes},
        {"skipLakebedEntrance",   &tpt::core::ParsedSettings::skipLakebedEntrance},
        {"skipArbitersEntrance",  &tpt::core::ParsedSettings::skipArbitersEntrance},
        {"skipSnowpeakEntrance",  &tpt::core::ParsedSettings::skipSnowpeakEntrance},
        {"skipGroveEntrance",     &tpt::core::ParsedSettings::skipGroveEntrance},
        {"skipCityEntrance",      &tpt::core::ParsedSettings::skipCityEntrance},
        {"skipBridgeDonation",    &tpt::core::ParsedSettings::skipBridgeDonation},
        {"faronTwilightCleared",  &tpt::core::ParsedSettings::faronTwilightCleared},
        {"eldinTwilightCleared",  &tpt::core::ParsedSettings::eldinTwilightCleared},
        {"lanayruTwilightCleared", &tpt::core::ParsedSettings::lanayruTwilightCleared},
        {"fastIronBoots",         &tpt::core::ParsedSettings::fastIronBoots},
        {"quickTransform",        &tpt::core::ParsedSettings::quickTransform},
        {"transformAnywhere",     &tpt::core::ParsedSettings::transformAnywhere},
        {"modifyShopModels",      &tpt::core::ParsedSettings::modifyShopModels},
        {"barrenDungeons",        &tpt::core::ParsedSettings::barrenDungeons},
        {"instantText",           &tpt::core::ParsedSettings::instantText},
        {"openMap",               &tpt::core::ParsedSettings::openMap},
        {"openDot",               &tpt::core::ParsedSettings::openDot},
        {"increaseSpinnerSpeed",  &tpt::core::ParsedSettings::increaseSpinnerSpeed},
        {"bonksDoDamage",         &tpt::core::ParsedSettings::bonksDoDamage},
        {"noSmallKeysOnBosses",   &tpt::core::ParsedSettings::noSmallKeysOnBosses},
        {"randomizeStartingPoint", &tpt::core::ParsedSettings::randomizeStartingPoint},
        {"gmShortcut",            &tpt::core::ParsedSettings::gmShortcut},
        {"hcShortcut",            &tpt::core::ParsedSettings::hcShortcut},
        {"unpairEntrances",       &tpt::core::ParsedSettings::unpairEntrances},
        {"decoupleEntrances",     &tpt::core::ParsedSettings::decoupleEntrances},
        {"autoFillWallet",        &tpt::core::ParsedSettings::autoFillWallet},
        {"hintDungeonEntrances",  &tpt::core::ParsedSettings::hintDungeonEntrances},
    }};
    std::printf("  flags ON: ");
    bool first = true;
    for (const auto& [label, member] : kBoolFlags) {
        if (s.*member) {
            std::printf("%s%s", first ? "" : ", ", label);
            first = false;
        }
    }
    if (first) std::printf("none");
    std::printf("\n");
}

std::filesystem::path exeDirectory() {
#ifdef _WIN32
    char buf[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return std::filesystem::path(buf).parent_path();
#endif
    return std::filesystem::current_path();
}

// Find the bundled "data/world" directory. Search next to the exe first,
// then fall back to the current working directory (handy for IDE runs).
std::filesystem::path findWorldDir() {
    const auto candidates = {
        exeDirectory() / "data" / "world",
        std::filesystem::current_path() / "data" / "world",
    };
    for (const auto& p : candidates) {
        if (std::filesystem::exists(p / "Rooms") &&
            std::filesystem::exists(p / "Checks")) {
            return p;
        }
    }
    return {};  // not found
}

int runLogicStats() {
    const auto worldDir = findWorldDir();
    if (worldDir.empty()) {
        std::fprintf(stderr,
            "error: could not find data/world (looked next to the exe and in cwd).\n"
            "       Make sure the build copied the JSONCs from "
            "Randomizer-Web-Generator-main/.\n");
        return 6;
    }
    std::printf("world dir: %s\n", worldDir.string().c_str());

    tpt::core::logic::RoomMap rooms;
    tpt::core::logic::CheckMap checks;
    try {
        rooms  = tpt::core::logic::loadRooms(worldDir);
        checks = tpt::core::logic::loadChecks(worldDir);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 6;
    }

    std::size_t totalExits = 0, totalRoomChecks = 0;
    for (const auto& [name, r] : rooms) {
        totalExits     += r.exits.size();
        totalRoomChecks += r.checks.size();
    }

    std::printf("rooms:  %zu loaded   exits: %zu   check-references: %zu\n",
                rooms.size(), totalExits, totalRoomChecks);
    std::printf("checks: %zu loaded\n", checks.size());

    // Parser smoke test: parse every requirement string we just loaded.
    // The parser caches by expression text, so duplicates are free.
    std::size_t parsed = 0, failed = 0;
    for (const auto& [_, r] : rooms) {
        for (const auto& e : r.exits) {
            for (const auto* expr : {&e.req, &e.reqGlitched}) {
                try {
                    (void)tpt::core::logic::parse(*expr);
                    ++parsed;
                } catch (const std::exception&) {
                    ++failed;
                }
            }
        }
    }
    for (const auto& [_, c] : checks) {
        for (const auto* expr : {&c.req, &c.reqGlitched}) {
            try {
                (void)tpt::core::logic::parse(*expr);
                ++parsed;
            } catch (const std::exception&) {
                ++failed;
            }
        }
    }
    std::printf("parsed: %zu expressions ok, %zu failed   "
                "(unique cached: %zu)\n",
                parsed, failed, tpt::core::logic::parseCacheSize());

    // Evaluator smoke test: empty context -> almost everything False (since
    // we have no items and no predicates registered yet).
    tpt::core::logic::Context emptyCtx;
    emptyCtx.permissiveSettings = true;
    int trueCount = 0, falseCount = 0;
    for (const auto& [_, c] : checks) {
        try {
            if (tpt::core::logic::evalExpr(c.req, emptyCtx)) ++trueCount;
            else ++falseCount;
        } catch (...) { /* ignore */ }
    }
    std::printf("eval (empty inventory): %d checks True, %d False "
                "(predicates not yet registered)\n",
                trueCount, falseCount);

    return failed > 0 ? 7 : 0;
}

void printSetFlags(std::span<const std::uint8_t> save) {
    std::printf("=== Event Flags (set) ===\n");
    std::size_t setCount = 0;
    for (const auto& e : tpt::core::eventFlagTable()) {
        if (tpt::core::readEventFlag(save, e.raw)) {
            std::printf("  %.*s\n", static_cast<int>(e.name.size()), e.name.data());
            ++setCount;
        }
    }
    std::printf("(%zu / %zu set)\n", setCount, tpt::core::eventFlagTable().size());

    std::printf("=== Get-Item Flags (set) ===\n");
    setCount = 0;
    for (const auto& e : tpt::core::getItemFlagTable()) {
        if (tpt::core::readGetItemFlag(save, e.itemId)) {
            std::printf("  %.*s (id 0x%02X)\n",
                        static_cast<int>(e.name.size()), e.name.data(), e.itemId);
            ++setCount;
        }
    }
    std::printf("(%zu / %zu set)\n", setCount, tpt::core::getItemFlagTable().size());
}

// Dev tool: report the layout of a running dusklight process. Closes the
// returned HANDLE before exit. If PDB symbols are in scope (development
// builds with the .pdb next to the .exe), prints the address of
// g_dComIfG_gameInfo so we can derive an AOB pattern around its
// references. Production builds (no PDB) just get module + .text info.
int runDuskProbe() {
    auto info = tpt::dusk::probeDusk();
    if (!info) {
        std::fprintf(stderr,
            "error: no running dusklight.exe found, or OpenProcess failed\n");
        return 2;
    }

    std::printf("=== Dusk Probe ===\n");
    std::printf("pid             %lu\n", info->pid);
    std::printf("module base     0x%016llX\n",
                static_cast<unsigned long long>(info->moduleBase));
    std::printf("module size     0x%llX bytes\n",
                static_cast<unsigned long long>(info->moduleSize));
    std::printf(".text base      0x%016llX\n",
                static_cast<unsigned long long>(info->textBase));
    std::printf(".text size      0x%llX bytes\n",
                static_cast<unsigned long long>(info->textSize));
    if (info->gameInfoAddr) {
        std::printf("g_dComIfG_gameInfo (PDB) 0x%016llX  (RVA 0x%llX)\n",
                    static_cast<unsigned long long>(info->gameInfoAddr),
                    static_cast<unsigned long long>(info->gameInfoAddr - info->moduleBase));
        std::printf("dSv_save_c (computed)   0x%016llX  "
                    "(offset 0x%llX from g_dComIfG_gameInfo)\n",
                    static_cast<unsigned long long>(info->saveBlockAddr),
                    static_cast<unsigned long long>(info->saveBlockAddr - info->gameInfoAddr));
    } else {
        std::printf("g_dComIfG_gameInfo (PDB) <unresolved>  "
                    "(place dusklight.pdb next to dusklight.exe to enable)\n");
    }

    // AOB result — fast fallback code path. Compare vs PDB if available.
    if (info->aobGameInfoAddr) {
        std::printf("g_dComIfG_gameInfo (AOB) 0x%016llX",
                    static_cast<unsigned long long>(info->aobGameInfoAddr));
        if (info->gameInfoAddr) {
            std::printf("  [%s]\n",
                info->aobGameInfoAddr == info->gameInfoAddr ? "MATCH" : "MISMATCH");
        } else {
            std::printf("\n");
        }
    } else {
        std::printf("g_dComIfG_gameInfo (AOB) <pattern not found in .text>\n");
    }

    // Content scan — resilient fallback path. Multiple buffers in memory
    // can pass the looksLikeSaveBlock() validator (Dusk keeps save-slot
    // mirrors / staging buffers around); the picker does temporal-change
    // detection to identify which one is actively updating.
    if (info->contentSaveBlockAddr) {
        std::printf("dSv_save_c (content)    0x%016llX",
                    static_cast<unsigned long long>(info->contentSaveBlockAddr));
        if (info->saveBlockAddr) {
            std::printf("  [%s vs PDB]",
                info->contentSaveBlockAddr == info->saveBlockAddr ? "MATCH" : "MISMATCH");
        } else if (info->aobGameInfoAddr) {
            std::printf("  [%s vs AOB]",
                info->contentSaveBlockAddr == info->aobGameInfoAddr ? "MATCH" : "MISMATCH");
        }
        std::printf("\n");
    } else {
        std::printf("dSv_save_c (content)    <no save block found in writable memory>\n"
                    "                        (probably no save loaded yet in the game)\n");
    }
    if (info->contentCandidates.size() > 1) {
        std::printf("                        %zu candidates; live-state fields per candidate:\n",
                    info->contentCandidates.size());
        auto rd16 = [](const std::vector<std::uint8_t>& d, std::size_t off) -> int {
            if (off + 1 >= d.size()) return -1;
            return (int(d[off]) << 8) | int(d[off + 1]);
        };
        for (std::size_t i = 0; i < info->contentCandidates.size(); ++i) {
            const auto addr = info->contentCandidates[i];
            const auto& head = (i < info->contentCandidateHeads.size())
                ? info->contentCandidateHeads[i]
                : std::vector<std::uint8_t>{};
            const int maxHp = rd16(head, 0x000);
            const int curHp = rd16(head, 0x002);
            const int rup   = rd16(head, 0x004);
            const int oil   = rd16(head, 0x008);
            std::printf("                          0x%016llX  "
                        "maxHp=%-5d curHp=%-5d rupees=%-5d oil=%d%s\n",
                        static_cast<unsigned long long>(addr),
                        maxHp, curHp, rup, oil,
                        addr == info->contentSaveBlockAddr ? "  <- picked" : "");
        }
    }

    // Decoded mutable fields from both candidates. These are the values
    // that change with gameplay; whichever candidate's numbers match
    // what's on-screen in Dusk is the *live* save copy.
    auto rd16BE = [](const std::vector<std::uint8_t>& d, std::size_t off) -> int {
        if (off + 1 >= d.size()) return -1;
        return (int(d[off]) << 8) | int(d[off + 1]);
    };
    if (!info->gameInfoDump.empty() || !info->contentDump.empty()) {
        std::printf("\n=== Live-state fields (decode @ each candidate) ===\n");
        std::printf("                       %-18s %-18s\n",
                    "AOB/PDB block", "content block");
        auto row = [&](const char* label, std::size_t off) {
            const int a = rd16BE(info->gameInfoDump, off);
            const int c = rd16BE(info->contentDump,  off);
            std::printf("  %-20s 0x%04X (%-6d)   0x%04X (%-6d)%s\n",
                label,
                a < 0 ? 0 : a, a < 0 ? 0 : a,
                c < 0 ? 0 : c, c < 0 ? 0 : c,
                (a >= 0 && c >= 0 && a != c) ? "  <-- DIVERGES" : "");
        };
        row("maxHealth (+0x000)", 0x000);
        row("curHealth (+0x002)", 0x002);
        row("rupees    (+0x004)", 0x004);
        row("lanternOil(+0x008)", 0x008);
    }

    // Deep decode per candidate: read the full 0x1800 save block from
    // each candidate, run the same decoder pipeline the tracker uses for
    // live reads, and tabulate downstream values. Lets us spot candidates
    // that pass the structural validator but contain inconsistent /
    // never-played data (empty save slots, half-initialized buffers).
#ifdef _WIN32
    if (info->process && info->contentCandidates.size() >= 2) {
        auto* h = static_cast<HANDLE>(info->process);
        struct Decoded {
            std::uintptr_t addr = 0;
            std::string player;
            std::string stage;
            int curHp = 0, maxHp = 0, rupees = 0, oil = 0;
            int sword = 0, bow = 0, clawshot = 0;
            int bottles = 0, bombBags = 0, poeSouls = 0;
            int singleItems = 0;
            int eventFlagsSet = 0, eventFlagsTotal = 0;
            int getItemFlagsSet = 0, getItemFlagsTotal = 0;
            int portalsOpen = 0, portalsTotal = 0;
            std::uint64_t totalFrames = 0;
            bool readOk = false;
        };
        std::vector<Decoded> rows;
        rows.reserve(info->contentCandidates.size());
        for (auto addr : info->contentCandidates) {
            Decoded d;
            d.addr = addr;
            std::vector<std::uint8_t> save(tpt::core::kSaveBlockSize);
            SIZE_T got = 0;
            if (!::ReadProcessMemory(h, reinterpret_cast<LPCVOID>(addr),
                    save.data(), save.size(), &got) || got != save.size()) {
                rows.push_back(d);
                continue;
            }
            d.readOk = true;
            const std::uint8_t currentNode = save[tpt::core::kOffsetCurrentNode];
            const auto inv = tpt::core::readInventory(save, currentNode);
            const auto qs  = tpt::core::readQuestState(save, currentNode);
            const auto ev  = tpt::core::logic::readAllEventFlags(save);
            const auto gi  = tpt::core::logic::readAllGetItemFlags(save);
            d.player = qs.playerName;
            d.stage  = qs.currentStage;
            d.curHp = qs.curHealth;  d.maxHp = qs.maxHealth;
            d.rupees = qs.rupees;    d.oil   = qs.curLanternOil;
            d.sword = inv.sword; d.bow = inv.bow; d.clawshot = inv.clawshot;
            d.bottles = inv.bottles; d.bombBags = inv.bombBags;
            d.poeSouls = inv.poeSouls;
            d.singleItems = inv.ordonShield + inv.hylianShield + inv.magicArmor
                          + inv.zoraArmor + inv.shadowCrystal + inv.hawkeye
                          + inv.lantern + inv.galeBoomerang + inv.spinner
                          + inv.ballAndChain + inv.ironBoots + inv.slingshot
                          + inv.auruMemo + inv.asheiSketch + inv.horseCall
                          + inv.giantBombBag + inv.gateKeys;
            for (const auto& [k, v] : ev) { if (v) ++d.eventFlagsSet; ++d.eventFlagsTotal; }
            for (const auto& [k, v] : gi) { if (v) ++d.getItemFlagsSet; ++d.getItemFlagsTotal; }
            for (const auto& p : qs.portals) { if (p.unlocked) ++d.portalsOpen; ++d.portalsTotal; }
            d.totalFrames = qs.totalTimeFrames;
            rows.push_back(std::move(d));
        }
        std::printf("\n=== Deep decode @ each candidate ===\n");
        auto colHdr = [&](const char* label) {
            std::printf("  %-22s", label);
            for (const auto& r : rows) {
                if (!r.readOk) { std::printf(" %-18s", "(read failed)"); continue; }
                char buf[32];
                std::snprintf(buf, sizeof(buf), "0x%llX",
                              static_cast<unsigned long long>(r.addr));
                std::printf(" %-18s", buf);
            }
            std::printf("\n");
        };
        colHdr("candidate");
        auto strRow = [&](const char* label, auto getter) {
            std::printf("  %-22s", label);
            for (const auto& r : rows) {
                if (!r.readOk) { std::printf(" %-18s", "-"); continue; }
                std::printf(" %-18s", std::string(getter(r)).c_str());
            }
            std::printf("\n");
        };
        auto intRow = [&](const char* label, auto getter) {
            std::printf("  %-22s", label);
            for (const auto& r : rows) {
                if (!r.readOk) { std::printf(" %-18s", "-"); continue; }
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%d", getter(r));
                std::printf(" %-18s", buf);
            }
            std::printf("\n");
        };
        auto ratioRow = [&](const char* label, auto num, auto den) {
            std::printf("  %-22s", label);
            for (const auto& r : rows) {
                if (!r.readOk) { std::printf(" %-18s", "-"); continue; }
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%d / %d", num(r), den(r));
                std::printf(" %-18s", buf);
            }
            std::printf("\n");
        };
        strRow("player",   [](const Decoded& r){ return r.player.empty() ? "(empty)" : r.player; });
        strRow("stage",    [](const Decoded& r){ return r.stage.empty() ? "(empty)" : r.stage; });
        intRow("rupees",   [](const Decoded& r){ return r.rupees; });
        intRow("curHp",    [](const Decoded& r){ return r.curHp; });
        intRow("maxHp",    [](const Decoded& r){ return r.maxHp; });
        intRow("oil",      [](const Decoded& r){ return r.oil; });
        intRow("sword tier",   [](const Decoded& r){ return r.sword; });
        intRow("bow tier",     [](const Decoded& r){ return r.bow; });
        intRow("clawshot tier",[](const Decoded& r){ return r.clawshot; });
        intRow("bottles",  [](const Decoded& r){ return r.bottles; });
        intRow("bomb bags",[](const Decoded& r){ return r.bombBags; });
        intRow("poe souls",[](const Decoded& r){ return r.poeSouls; });
        intRow("single items",  [](const Decoded& r){ return r.singleItems; });
        ratioRow("event flags",
            [](const Decoded& r){ return r.eventFlagsSet; },
            [](const Decoded& r){ return r.eventFlagsTotal; });
        ratioRow("get-item flags",
            [](const Decoded& r){ return r.getItemFlagsSet; },
            [](const Decoded& r){ return r.getItemFlagsTotal; });
        ratioRow("portals open",
            [](const Decoded& r){ return r.portalsOpen; },
            [](const Decoded& r){ return r.portalsTotal; });
        // totalTimeFrames is the in-game frame counter — ticks every
        // frame the live save is running, regardless of player input.
        // If the live save's value is substantially larger than the
        // mirrors', that's a strong "I am the live one" signal.
        std::printf("  %-22s", "totalTimeFrames");
        for (const auto& r : rows) {
            if (!r.readOk) { std::printf(" %-18s", "-"); continue; }
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%llu",
                          static_cast<unsigned long long>(r.totalFrames));
            std::printf(" %-18s", buf);
        }
        std::printf("\n");
    }
#endif

    // Byte-diff summary: how many of the 512 dumped bytes disagree
    // between the two candidates? A non-zero count means the copies have
    // diverged (i.e. one is updating, the other is stale).
    if (!info->gameInfoDump.empty() && !info->contentDump.empty()) {
        const auto& a = info->gameInfoDump;
        const auto& c = info->contentDump;
        const std::size_t n = std::min(a.size(), c.size());
        std::size_t diffs = 0;
        std::size_t firstDiff = SIZE_MAX;
        for (std::size_t i = 0; i < n; ++i) {
            if (a[i] != c[i]) {
                if (firstDiff == SIZE_MAX) firstDiff = i;
                ++diffs;
            }
        }
        std::printf("\n=== Byte diff (first 512 bytes of each) ===\n");
        std::printf("  %zu / %zu bytes differ", diffs, n);
        if (firstDiff != SIZE_MAX) {
            std::printf(", first divergence at +0x%03zX", firstDiff);
        }
        std::printf("\n");
    }

    // LEA references — these are the candidates for the AOB pattern.
    std::printf("\n=== LEA refs to g_dComIfG_gameInfo (.text) ===\n");
    if (info->leaMatches.empty()) {
        std::printf("(none found)\n");
    } else {
        std::printf("%zu match(es) — first 32 bytes of each:\n",
                    info->leaMatches.size());
        for (std::size_t i = 0; i < info->leaMatches.size(); ++i) {
            const auto& m = info->leaMatches[i];
            std::printf("[%zu] @ 0x%016llX (RVA 0x%llX)\n",
                        i, static_cast<unsigned long long>(m.addr),
                        static_cast<unsigned long long>(m.addr - info->moduleBase));
            std::printf("    ");
            for (std::size_t k = 0; k < m.bytesLen; ++k) {
                std::printf("%02X ", m.bytes[k]);
            }
            std::printf("\n");
        }
    }

    // Hex dump of memory at the resolved address (PDB if available, else AOB).
    if (!info->gameInfoDump.empty()) {
        const char* src = info->gameInfoAddr ? "PDB" : "AOB";
        std::printf("\n=== Hex dump @ g_dComIfG_gameInfo via %s (512 bytes) ===\n", src);
        const auto& d = info->gameInfoDump;
        for (std::size_t row = 0; row < d.size(); row += 16) {
            std::printf("+0x%03zX  ", row);
            for (std::size_t c = 0; c < 16 && row + c < d.size(); ++c) {
                std::printf("%02X ", d[row + c]);
            }
            std::printf(" |");
            for (std::size_t c = 0; c < 16 && row + c < d.size(); ++c) {
                const std::uint8_t b = d[row + c];
                std::printf("%c", (b >= 0x20 && b < 0x7F) ? b : '.');
            }
            std::printf("|\n");
        }
    }

#ifdef _WIN32
    if (info->process) ::CloseHandle(static_cast<HANDLE>(info->process));
#endif
    return 0;
}

// ===========================================================================
// --p64-probe — introspect a running Project64 process for RDRAM derivation.
// ===========================================================================
int runP64Probe() {
    auto info = tpt::p64::probeProject64();
    if (!info) {
        std::fprintf(stderr,
            "error: no running project64.exe found, or OpenProcess failed\n");
        return 2;
    }

    std::printf("=== Project64 Probe ===\n");
    std::printf("pid                %lu\n", info->pid);
    std::printf("module base        0x%016llX\n",
                static_cast<unsigned long long>(info->moduleBase));
    std::printf("module size        0x%llX bytes\n",
                static_cast<unsigned long long>(info->moduleSize));

    std::printf("\n=== Known-base fast path ===\n");
    for (std::size_t i = 0; i < info->fastPathCandidates.size(); ++i) {
        const auto base = info->fastPathCandidates[i];
        const bool ok = i < info->fastPathValidated.size()
            ? info->fastPathValidated[i] : false;
        std::printf("  0x%016llX  %s%s\n",
                    static_cast<unsigned long long>(base),
                    ok ? "VALID" : "no",
                    (base == info->validatedFastPath) ? "  <- picked" : "");
    }

    std::printf("\n=== VirtualQueryEx fallback scan ===\n");
    std::printf("  regions walked     %zu total, %zu committed, %zu unique reservations tested\n",
                info->scanRegionsTotal,
                info->scanRegionsCommitted,
                info->scanAllocBasesTested);
    std::printf("  highest addr seen  0x%016llX\n",
                static_cast<unsigned long long>(info->scanHighestAddr));
    if (info->scannedCandidates.empty()) {
        std::printf("  (no AllocationBase matched the save-magic validator)\n");
    } else {
        for (auto base : info->scannedCandidates) {
            const bool isKnown = std::any_of(
                info->fastPathCandidates.begin(),
                info->fastPathCandidates.end(),
                [base](std::uintptr_t b){ return b == base; });
            std::printf("  0x%016llX  VALID%s\n",
                        static_cast<unsigned long long>(base),
                        isKnown ? "  (already in known list)"
                                : "  <- consider adding to kKnownBases");
        }
    }

    if (info->headerDump.empty()) {
        std::printf("\n(no validated RDRAM base — is an OoT-family ROM loaded?)\n");
#ifdef _WIN32
        if (info->process) ::CloseHandle(static_cast<HANDLE>(info->process));
#endif
        return 1;
    }

    std::printf("\n=== Save-magic @ validated base + 0x11A5EC ===\n");
    std::printf("  decoded magic      \"%s\" (expected \"ZELDAZ\")\n",
                info->cartId.c_str());
    std::printf("\n  hex dump (64 bytes starting at +0x11A5EC, raw P64 memory):\n");
    const auto& d = info->headerDump;
    for (std::size_t row = 0; row < d.size(); row += 16) {
        std::printf("  +0x%02zX  ", row);
        for (std::size_t c = 0; c < 16 && row + c < d.size(); ++c) {
            std::printf("%02X ", d[row + c]);
        }
        std::printf(" |");
        for (std::size_t c = 0; c < 16 && row + c < d.size(); ++c) {
            const std::uint8_t b = d[row + c];
            std::printf("%c", (b >= 0x20 && b < 0x7F) ? b : '.');
        }
        std::printf("|\n");
    }

#ifdef _WIN32
    if (info->process) ::CloseHandle(static_cast<HANDLE>(info->process));
#endif
    return 0;
}

// ===========================================================================
// --oot-dump — attach to Project64, decode PlayerData + Inventory, print.
// Quick end-to-end validation of the decoders against a live save.
// ===========================================================================
int runOotDump() {
    tpt::p64::Source src;
    if (!src.connect()) {
        std::fprintf(stderr,
            "error: could not attach to Project64 "
            "(running? OoT-family ROM loaded with a save?)\n");
        return 2;
    }

    std::vector<std::uint8_t> buf(tpt::game::oot::save::kHeaderReadSize);
    if (!src.readBytes(tpt::game::oot::save::kSaveContextAddr,
                       buf.data(), buf.size())) {
        std::fprintf(stderr, "error: SaveContext read failed\n");
        return 3;
    }

    const auto pd  = tpt::game::oot::save::readPlayerData(buf);
    if (!pd.saveLoaded) {
        std::fprintf(stderr,
            "error: SaveContext read OK but newf magic missing — "
            "is a save loaded in-game?\n");
        return 4;
    }
    const auto inv = tpt::game::oot::save::readInventory(buf);

    std::printf("=== OoT Player Data ===\n");
    std::printf("  player name        \"%s\"\n", pd.playerName.c_str());
    std::printf("  form               %s\n", pd.isAdult ? "Adult" : "Child");
    std::printf("  hearts             %.2f / %.2f (raw %d / %d)\n",
                pd.health / 16.0, pd.healthCapacity / 16.0,
                pd.health, pd.healthCapacity);
    std::printf("  magic              %d / %d   level=%d\n",
                pd.magic,
                pd.isDoubleMagicAcquired ? 96 : pd.isMagicAcquired ? 48 : 0,
                pd.magicLevel);
    std::printf("  rupees             %d\n", pd.rupees);
    std::printf("  deaths             %u\n", pd.deaths);
    std::printf("  flags              magic=%d dblMagic=%d dblDefense=%d bgs=%d\n",
                pd.isMagicAcquired, pd.isDoubleMagicAcquired,
                pd.isDoubleDefenseAcquired, pd.hasBiggoronSword);

    std::printf("\n=== OoT Inventory ===\n");
    auto sv = [](std::string_view s) { return std::string(s); };
    std::printf("  sword              %s\n", sv(tpt::game::oot::save::swordLabel(inv)).c_str());
    std::printf("  shield             %s\n", sv(tpt::game::oot::save::shieldLabel(inv)).c_str());
    std::printf("  tunic (owned high) %s\n", sv(tpt::game::oot::save::tunicLabel(inv)).c_str());
    std::printf("  boots              %s\n", sv(tpt::game::oot::save::bootsLabel(inv)).c_str());
    std::printf("  wallet             %s\n", sv(tpt::game::oot::save::walletLabel(inv)).c_str());
    std::printf("  strength           %s\n", sv(tpt::game::oot::save::strengthLabel(inv)).c_str());
    std::printf("  scale              %s\n", sv(tpt::game::oot::save::scaleLabel(inv)).c_str());
    std::printf("  quiver             tier %d   bomb bag tier %d\n", inv.quiver, inv.bombBag);
    std::printf("  bullet bag         tier %d   sticks tier %d   nuts tier %d\n",
                inv.bulletBag, inv.dekuSticks, inv.dekuNuts);
    std::printf("  gold skulltulas    %d\n", inv.gsTokens);

    std::printf("\n=== OoT Quest Items ===\n");
    auto chk = [](bool b) { return b ? "[x]" : "[ ]"; };
    std::printf("  stones    %s Kokiri  %s Goron  %s Zora\n",
                chk(inv.kokiriEmerald), chk(inv.goronRuby), chk(inv.zoraSapphire));
    std::printf("  medallions %s Forest %s Fire   %s Water\n",
                chk(inv.medallionForest), chk(inv.medallionFire),
                chk(inv.medallionWater));
    std::printf("             %s Spirit %s Shadow %s Light\n",
                chk(inv.medallionSpirit), chk(inv.medallionShadow),
                chk(inv.medallionLight));
    std::printf("  warp songs %s Minuet %s Bolero %s Serenade\n",
                chk(inv.songMinuet), chk(inv.songBolero), chk(inv.songSerenade));
    std::printf("             %s Requiem %s Nocturne %s Prelude\n",
                chk(inv.songRequiem), chk(inv.songNocturne), chk(inv.songPrelude));
    std::printf("  ocarina    %s Lullaby %s Epona   %s Saria\n",
                chk(inv.songLullaby), chk(inv.songEpona), chk(inv.songSaria));
    std::printf("             %s Sun     %s Time    %s Storms\n",
                chk(inv.songSun), chk(inv.songTime), chk(inv.songStorms));
    std::printf("  misc       %s Stone of Agony   %s Gerudo's Card\n",
                chk(inv.stoneOfAgony), chk(inv.gerudosCard));

    std::printf("\n=== Slot items[24] (ItemID per slot, 0xFF = empty) ===\n");
    for (std::size_t i = 0; i < inv.items.size(); ++i) {
        std::printf("  slot %02zu = 0x%02X\n", i, inv.items[i]);
    }
    return 0;
}

// ===========================================================================
// --oot-checks — attach to Project64, decode flags, list checks by area with
// completion status. End-to-end validation of the world data → flags →
// completion pipeline.
// ===========================================================================
int runOotChecks() {
    // Load checks from the location-json next to the binary.
    std::filesystem::path locFile;
#ifdef _WIN32
    {
        char buf[260] = {};
        const DWORD n = ::GetModuleFileNameA(nullptr, buf, sizeof(buf));
        if (n > 0 && n < sizeof(buf)) {
            locFile = std::filesystem::path(buf).parent_path()
                    / "data" / "oot" / "locations.json";
        }
    }
#endif
    if (locFile.empty()) {
        locFile = std::filesystem::current_path() / "data" / "oot" / "locations.json";
    }
    std::vector<tpt::game::oot::Check> checks;
    if (!tpt::game::oot::loadChecks(locFile, checks, std::cerr)) {
        return 1;
    }

    // Attach + read save flags.
    tpt::p64::Source src;
    if (!src.connect()) {
        std::fprintf(stderr, "error: could not attach to Project64\n");
        return 2;
    }
    std::vector<std::uint8_t> buf(tpt::game::oot::save::kHeaderReadSize);
    if (!src.readBytes(tpt::game::oot::save::kSaveContextAddr,
                       buf.data(), buf.size())) {
        std::fprintf(stderr, "error: SaveContext read failed\n");
        return 3;
    }
    const auto pd    = tpt::game::oot::save::readPlayerData(buf);
    if (!pd.saveLoaded) {
        std::fprintf(stderr, "error: no save loaded in-game\n");
        return 4;
    }
    const auto inv   = tpt::game::oot::save::readInventory(buf);
    const auto flags = tpt::game::oot::save::readSaveFlags(buf);

    // Fetch xflag tables + collectible bytes so Xflag-typed checks
    // resolve. Failure leaves xst.valid = false, which yields nullopt
    // for those checks (rendered as "unknown" in the tally).
    tpt::game::oot::save::XflagState xst;
    if (tpt::game::oot::save::resolveOotrAddrs(src, xst.ootrAddrs)) {
        if (tpt::game::oot::save::fetchXflagTables(src, xst)) {
            tpt::game::oot::save::fetchCollectibleFlags(src, xst);
        }
        tpt::game::oot::save::fetchExtendedSavectx(src, xst);
    }

    // Tally by area + type.
    struct AreaStat {
        int done = 0;
        int pending = 0;
        int unknown = 0;
    };
    std::map<std::string, AreaStat> byArea;
    std::map<std::string, AreaStat> byType;
    for (const auto& c : checks) {
        const auto r = tpt::game::oot::isCheckComplete(c, pd, inv, flags, xst);
        auto& a = byArea[c.area];
        auto& t = byType[c.typeRaw];
        if (!r.has_value())     { ++a.unknown; ++t.unknown; }
        else if (*r)            { ++a.done;    ++t.done;    }
        else                    { ++a.pending; ++t.pending; }
    }

    std::printf("=== OoT Checks by area ===\n");
    for (const auto& [area, s] : byArea) {
        std::printf("  %-40s done=%-4d pending=%-4d unknown=%d\n",
                    area.c_str(), s.done, s.pending, s.unknown);
    }
    std::printf("\n=== OoT Checks by type ===\n");
    for (const auto& [t, s] : byType) {
        std::printf("  %-15s done=%-4d pending=%-4d unknown=%d\n",
                    t.c_str(), s.done, s.pending, s.unknown);
    }

    // Show all currently-completed checks (the "what have I done so far"
    // overview — the more interesting list when validating).
    std::printf("\n=== Completed checks ===\n");
    int completed = 0;
    for (const auto& c : checks) {
        const auto r = tpt::game::oot::isCheckComplete(c, pd, inv, flags, xst);
        if (r.value_or(false)) {
            std::printf("  [%s] %s\n", c.typeRaw.c_str(), c.name.c_str());
            ++completed;
        }
    }
    if (completed == 0) {
        std::printf("  (none)\n");
    }
    return 0;
}

// ===========================================================================
// --oot-world — load OoTR's World/*.json and print region/edge stats. No
// emulator attach needed. Validates the JSON sanitizer + region loader
// before the rule parser comes online.
// ===========================================================================
int runOotWorld() {
    std::filesystem::path worldDir;
#ifdef _WIN32
    {
        char buf[260] = {};
        const DWORD n = ::GetModuleFileNameA(nullptr, buf, sizeof(buf));
        if (n > 0 && n < sizeof(buf)) {
            worldDir = std::filesystem::path(buf).parent_path()
                     / "data" / "oot-world";
        }
    }
#endif
    if (worldDir.empty()) {
        worldDir = std::filesystem::current_path() / "data" / "oot-world";
    }

    tpt::game::oot::logic::RegionMap regions;
    try {
        regions = tpt::game::oot::logic::loadRegions(worldDir);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    std::size_t totalExits = 0, totalLocations = 0, totalEvents = 0;
    std::size_t dungeonRegions = 0, overworldRegions = 0;
    for (const auto& [name, r] : regions) {
        totalExits     += r.exits.size();
        totalLocations += r.locations.size();
        totalEvents    += r.events.size();
        if (r.dungeon.empty()) ++overworldRegions; else ++dungeonRegions;
    }

    std::printf("=== OoT World Graph ===\n");
    std::printf("  world dir          %s\n", worldDir.string().c_str());
    std::printf("  regions            %zu (overworld %zu + dungeon %zu)\n",
                regions.size(), overworldRegions, dungeonRegions);
    std::printf("  total exits        %zu\n", totalExits);
    std::printf("  total locations    %zu\n", totalLocations);
    std::printf("  total events       %zu\n", totalEvents);

    // Tally by dungeon — a quick way to spot missing files.
    std::map<std::string, std::size_t> byDungeon;
    for (const auto& [name, r] : regions) {
        const std::string key = r.dungeon.empty() ? "(overworld)" : r.dungeon;
        ++byDungeon[key];
    }
    std::printf("\n=== Regions by dungeon ===\n");
    for (const auto& [d, n] : byDungeon) {
        std::printf("  %-32s %zu regions\n", d.c_str(), n);
    }

    // Highlight the Root region — it's the BFS source. Useful smoke check.
    const auto it = regions.find("Root");
    if (it != regions.end()) {
        const auto& r = it->second;
        std::printf("\n=== Root region ===\n");
        std::printf("  exits (%zu):\n", r.exits.size());
        for (const auto& e : r.exits) {
            std::printf("    -> %-40s  if: %s\n",
                        e.target.c_str(), e.rule.c_str());
        }
        std::printf("  events (%zu):\n", r.events.size());
        for (const auto& e : r.events) {
            std::printf("    %s\n", e.target.c_str());
        }
    } else {
        std::fprintf(stderr, "warning: Root region not found\n");
    }
    return 0;
}

// ===========================================================================
// --oot-parse — exercise the OoT rule parser against every rule string in
// the loaded world graph and LogicHelpers.json. Reports pass/fail counts
// and the first few failures with their full text. Gate for evaluator work.
// ===========================================================================
int runOotParse() {
    namespace fs = std::filesystem;

    fs::path worldDir;
    fs::path helpersPath;
#ifdef _WIN32
    {
        char buf[260] = {};
        const DWORD n = ::GetModuleFileNameA(nullptr, buf, sizeof(buf));
        if (n > 0 && n < sizeof(buf)) {
            const auto base = fs::path(buf).parent_path();
            worldDir    = base / "data" / "oot-world";
            helpersPath = base / "data" / "oot" / "LogicHelpers.json";
        }
    }
#endif
    if (worldDir.empty()) {
        worldDir    = fs::current_path() / "data" / "oot-world";
        helpersPath = fs::current_path() / "data" / "oot" / "LogicHelpers.json";
    }

    tpt::game::oot::logic::RegionMap regions;
    try {
        regions = tpt::game::oot::logic::loadRegions(worldDir);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: world load failed: %s\n", e.what());
        return 1;
    }

    struct Failure {
        std::string where;   // e.g. "Region 'Root'.exits[0] -> Target"
        std::string rule;
        std::string error;
    };
    std::vector<Failure> failures;

    std::size_t totalRules = 0;
    std::size_t parsedOk   = 0;
    auto tryParse = [&](std::string where, const std::string& rule) {
        ++totalRules;
        try {
            (void)tpt::game::oot::logic::parseRule(rule);
            ++parsedOk;
        } catch (const tpt::game::oot::logic::RuleParseError& e) {
            failures.push_back({std::move(where), rule, e.what()});
        }
    };

    for (const auto& [name, r] : regions) {
        for (std::size_t i = 0; i < r.exits.size(); ++i) {
            tryParse(name + ".exits[" + std::to_string(i) + "] -> "
                         + r.exits[i].target,
                     r.exits[i].rule);
        }
        for (std::size_t i = 0; i < r.locations.size(); ++i) {
            tryParse(name + ".locations[" + std::to_string(i) + "] "
                         + r.locations[i].target,
                     r.locations[i].rule);
        }
        for (std::size_t i = 0; i < r.events.size(); ++i) {
            tryParse(name + ".events[" + std::to_string(i) + "] "
                         + r.events[i].target,
                     r.events[i].rule);
        }
    }

    // Now LogicHelpers — the macro values are rule expressions too.
    std::size_t helpersTotal = 0, helpersOk = 0;
    std::vector<Failure> helperFailures;
    if (fs::exists(helpersPath)) {
        std::ifstream in(helpersPath, std::ios::binary);
        std::ostringstream buf;
        buf << in.rdbuf();
        const auto prepared = tpt::game::oot::logic::prepOotJson(buf.str());
        try {
            auto doc = nlohmann::json::parse(prepared);
            if (doc.is_object()) {
                for (auto it = doc.begin(); it != doc.end(); ++it) {
                    if (!it.value().is_string()) continue;
                    ++helpersTotal;
                    const auto rule = it.value().get<std::string>();
                    try {
                        (void)tpt::game::oot::logic::parseRule(rule);
                        ++helpersOk;
                    } catch (const tpt::game::oot::logic::RuleParseError& e) {
                        helperFailures.push_back(
                            {"LogicHelpers[" + it.key() + "]", rule, e.what()});
                    }
                }
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr,
                "warning: LogicHelpers.json parse failed: %s\n", e.what());
        }
    } else {
        std::fprintf(stderr,
            "warning: LogicHelpers.json not found at %s — skipping\n",
            helpersPath.string().c_str());
    }

    std::printf("=== OoT Rule Parse Results ===\n");
    std::printf("  world rules    %zu parsed, %zu failed (of %zu total)\n",
                parsedOk, failures.size(), totalRules);
    std::printf("  helper rules   %zu parsed, %zu failed (of %zu total)\n",
                helpersOk, helperFailures.size(), helpersTotal);

    auto dumpFailures = [](const char* heading,
                           const std::vector<Failure>& fs,
                           std::size_t maxShow) {
        if (fs.empty()) return;
        std::printf("\n=== %s — first %zu of %zu ===\n",
                    heading,
                    std::min(fs.size(), maxShow),
                    fs.size());
        for (std::size_t i = 0; i < std::min(fs.size(), maxShow); ++i) {
            std::printf("[%zu] %s\n", i, fs[i].where.c_str());
            std::printf("     rule: %s\n", fs[i].rule.c_str());
            std::printf("     err:  %s\n", fs[i].error.c_str());
        }
    };
    dumpFailures("World failures",  failures,        15);
    dumpFailures("Helper failures", helperFailures,  15);

    return (failures.empty() && helperFailures.empty()) ? 0 : 1;
}

// ===========================================================================
// --oot-reach — attach to Project64, build context from the live save,
// run the reach BFS, and list reachable not-yet-done checks by area.
// End-to-end smoke test of the full reachability pipeline.
// ===========================================================================
int runOotReach() {
    namespace fs = std::filesystem;

    fs::path dataDir;
#ifdef _WIN32
    {
        char buf[260] = {};
        const DWORD n = ::GetModuleFileNameA(nullptr, buf, sizeof(buf));
        if (n > 0 && n < sizeof(buf)) {
            dataDir = fs::path(buf).parent_path() / "data";
        }
    }
#endif
    if (dataDir.empty()) dataDir = fs::current_path() / "data";

    // Load all the static data.
    std::vector<tpt::game::oot::Check> checks;
    if (!tpt::game::oot::loadChecks(dataDir / "oot" / "locations.json",
                                    checks, std::cerr)) return 1;
    tpt::game::oot::logic::RegionMap raw;
    try {
        raw = tpt::game::oot::logic::loadRegions(dataDir / "oot-world");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    auto world = tpt::game::oot::logic::compileWorld(raw);
    tpt::game::oot::logic::AliasTable aliases;
    try {
        aliases = tpt::game::oot::logic::loadAliases(
            dataDir / "oot" / "LogicHelpers.json", std::cerr);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

    // Attach + decode save.
    tpt::p64::Source src;
    if (!src.connect()) {
        std::fprintf(stderr, "error: could not attach to Project64\n");
        return 2;
    }
    std::vector<std::uint8_t> buf(tpt::game::oot::save::kHeaderReadSize);
    if (!src.readBytes(tpt::game::oot::save::kSaveContextAddr,
                       buf.data(), buf.size())) {
        std::fprintf(stderr, "error: SaveContext read failed\n");
        return 3;
    }
    const auto pd    = tpt::game::oot::save::readPlayerData(buf);
    if (!pd.saveLoaded) {
        std::fprintf(stderr, "error: no save loaded in-game\n");
        return 4;
    }
    const auto inv   = tpt::game::oot::save::readInventory(buf);
    const auto flags = tpt::game::oot::save::readSaveFlags(buf);

    tpt::game::oot::save::XflagState xst;
    if (tpt::game::oot::save::fetchXflagTables(src, xst)) {
        tpt::game::oot::save::fetchCollectibleFlags(src, xst);
    }

    auto ctx = tpt::game::oot::logic::buildContext(pd, inv, flags);
    const auto reachable = tpt::game::oot::logic::reach(world, aliases, ctx);

    std::printf("=== OoT Reachability ===\n");
    std::printf("  reached regions       %zu / %zu\n",
                ctx.reachedRegions.size(), world.size());
    std::printf("  triggered events      %zu\n", ctx.events.size());
    std::printf("  reachable locations   %zu\n", reachable.size());

    // Per-area tally of reachable-but-not-done.
    std::unordered_set<std::string> reachableNames(reachable.begin(),
                                                   reachable.end());
    std::map<std::string, int> reachableByArea;
    int totalReachablePending = 0;
    for (const auto& c : checks) {
        if (!reachableNames.count(c.name)) continue;
        const auto done = tpt::game::oot::isCheckComplete(c, pd, inv, flags, xst);
        if (done.value_or(false)) continue;
        if (!done.has_value())   continue;
        ++reachableByArea[c.area];
        ++totalReachablePending;
    }
    std::printf("  reachable + pending   %d\n", totalReachablePending);

    std::printf("\n=== Reachable pending by area ===\n");
    for (const auto& [area, n] : reachableByArea) {
        std::printf("  %-40s %d\n", area.c_str(), n);
    }

    std::printf("\n=== Reachable pending check names ===\n");
    for (const auto& c : checks) {
        if (!reachableNames.count(c.name)) continue;
        const auto done = tpt::game::oot::isCheckComplete(c, pd, inv, flags, xst);
        if (done.value_or(false) || !done.has_value()) continue;
        std::printf("  [%s] %s\n", c.typeRaw.c_str(), c.name.c_str());
    }

    std::printf("\n=== Reached regions ===\n");
    std::vector<std::string> reachedSorted(ctx.reachedRegions.begin(),
                                            ctx.reachedRegions.end());
    std::sort(reachedSorted.begin(), reachedSorted.end());
    for (const auto& r : reachedSorted) std::printf("  %s\n", r.c_str());

    std::printf("\n=== All reachable locations (incl. unknown/done) ===\n");
    std::vector<std::string> reachSorted(reachable.begin(), reachable.end());
    std::sort(reachSorted.begin(), reachSorted.end());
    for (const auto& n : reachSorted) std::printf("  %s\n", n.c_str());

    std::printf("\n=== ctx.items (non-zero) ===\n");
    std::vector<std::pair<std::string,int>> itemsSorted(ctx.items.begin(),
                                                         ctx.items.end());
    std::sort(itemsSorted.begin(), itemsSorted.end());
    for (const auto& [k, v] : itemsSorted) {
        if (v != 0) std::printf("  %-40s = %d\n", k.c_str(), v);
    }

    std::printf("\n=== ctx.events (triggered) ===\n");
    std::vector<std::string> evtSorted(ctx.events.begin(), ctx.events.end());
    std::sort(evtSorted.begin(), evtSorted.end());
    if (evtSorted.empty()) std::printf("  (none)\n");
    for (const auto& e : evtSorted) std::printf("  %s\n", e.c_str());

    std::printf("\n=== ctx.settings (key/value) ===\n");
    std::vector<std::pair<std::string,std::string>> setSorted(
        ctx.settings.begin(), ctx.settings.end());
    std::sort(setSorted.begin(), setSorted.end());
    for (const auto& [k, v] : setSorted) {
        std::printf("  %-40s = %s\n", k.c_str(), v.c_str());
    }

    return 0;
}

// ===========================================================================
// --oot-extras — dump OoTR extended_savectx + xflag pointer state, then
// re-read ~800ms later and report any byte changes. Tool for diagnosing
// "false [x]" and "toggling check" reports — if these regions drift while
// the player is idle, we're reading volatile memory (wrong address for the
// loaded OoTR build) instead of save state.
// ===========================================================================
int runOotExtras() {
    using namespace std::chrono_literals;

    tpt::p64::Source src;
    if (!src.connect()) {
        std::fprintf(stderr, "error: could not attach to Project64\n");
        return 2;
    }

    // SaveContext sanity. saveLoaded=false means the game is at title
    // screen / file select; anything we read from runtime regions then
    // is meaningless.
    std::vector<std::uint8_t> hdr(tpt::game::oot::save::kHeaderReadSize);
    if (!src.readBytes(tpt::game::oot::save::kSaveContextAddr,
                       hdr.data(), hdr.size())) {
        std::fprintf(stderr, "error: SaveContext read failed\n");
        return 3;
    }
    const auto pd = tpt::game::oot::save::readPlayerData(hdr);
    std::printf("=== SaveContext header ===\n");
    std::printf("  saveLoaded=%s playerName='%s' isAdult=%d health=%u/%u rupees=%u\n",
                pd.saveLoaded ? "true" : "false",
                pd.playerName.c_str(),
                pd.isAdult ? 1 : 0,
                static_cast<unsigned>(pd.health),
                static_cast<unsigned>(pd.healthCapacity),
                static_cast<unsigned>(pd.rupees));
    if (!pd.saveLoaded) {
        std::printf("  (no save loaded — extended_savectx contents below are pre-game state)\n");
    }

    auto dump = [](const char* label, std::uint32_t addr,
                   const std::vector<std::uint8_t>& bytes) {
        std::printf("  %s @ 0x%08X (%zu bytes):\n   ", label, addr, bytes.size());
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            if (i && i % 16 == 0) std::printf("\n   ");
            std::printf(" %02X", bytes[i]);
        }
        std::printf("\n");
    };

    // OoTR puts a 4-slot pointer table at 0x80400000 — slot [2] (0x80400008)
    // varies per OoTR build and uniquely identifies the version.
    {
        std::vector<std::uint8_t> anchor(16, 0);
        const bool ok = src.readBytes(0x80400000u, anchor.data(), anchor.size());
        std::printf("\n=== OoTR version anchor (0x80400000) ===\n");
        if (!ok) {
            std::printf("  read failed\n");
        } else {
            auto rdU32 = [&](std::size_t o) {
                return (std::uint32_t(anchor[o + 0]) << 24) |
                       (std::uint32_t(anchor[o + 1]) << 16) |
                       (std::uint32_t(anchor[o + 2]) << 8)  |
                        std::uint32_t(anchor[o + 3]);
            };
            const auto p0 = rdU32(0);
            const auto p1 = rdU32(4);
            const auto p2 = rdU32(8);
            const auto p3 = rdU32(12);
            std::printf("  +0x00 0x%08X\n", p0);
            std::printf("  +0x04 0x%08X\n", p1);
            std::printf("  +0x08 0x%08X  <- version identifier\n", p2);
            std::printf("  +0x0C 0x%08X\n", p3);

            // Known build mappings.
            const char* match = "unknown";
            switch (p2) {
                case 0x80409FD4u: match = "AP 0.3.1";       break;
                case 0x8040A334u: match = "AP 0.3.2";       break;
                case 0x8040A474u: match = "OOTR 6.2";       break;
                case 0x8040AA7Cu: match = "OOTR 6.2.72";    break;
                case 0x80411E64u: match = "OOTR 7.0";       break;
                case 0x80412064u: match = "OOTR 7.1";       break;
                case 0x8040ACC4u: match = "Roman 6.2.43";   break;
                case 0x8040B11Cu: match = "Roman 6.2.72-R2";break;
                case 0x8040E478u: match = "Roman 6.2.163";  break;
                default: break;
            }
            std::printf("  match: %s (slot[2] = extern_ctxt pointer)\n", match);

            // p2 IS extern_ctxt in this build (per RANDO_CONTEXT layout in
            // OoT-Randomizer-Dev/ASM/src/build.asm). Our checked-in symbols
            // place extern_ctxt at 0x8042B56C and extended_savectx at
            // 0x8044A120 — delta 0x1EBB4. If that delta is stable across
            // builds, user's extended_savectx ≈ p2 + 0x1EBB4. Try it.
            constexpr std::uint32_t kExternCtxtOurs       = 0x8042B56Cu;
            constexpr std::uint32_t kExtendedSavectxOurs  = 0x8044A120u;
            constexpr std::uint32_t kDelta                = kExtendedSavectxOurs - kExternCtxtOurs;
            const std::uint32_t guess = p2 + kDelta;
            std::printf("\n  Computed extended_savectx guess (extern_ctxt + 0x%X) = 0x%08X\n",
                        kDelta, guess);

            std::vector<std::uint8_t> guessBytes(64, 0);
            if (src.readBytes(guess, guessBytes.data(), guessBytes.size())) {
                dump("guessed extended_savectx", guess, guessBytes);
                bool srLooksClean = true;
                for (int i = 0; i < 0x16; ++i) if (guessBytes[i] > 30) { srLooksClean = false; break; }
                bool bossesClean = true;
                for (int i = 0; i < 8; ++i) if (guessBytes[0x16 + i] > 1) { bossesClean = false; break; }
                std::printf("  guess passes silver_rupee_counts sanity?     %s\n",
                            srLooksClean ? "YES" : "no");
                std::printf("  guess passes collected_dungeon_rewards sanity? %s\n",
                            bossesClean ? "YES" : "no");
            } else {
                std::printf("  read failed at guess\n");
            }
        }
    }

    // Also surface the AUTO_TRACKER_CONTEXT pointer at RANDO_CONTEXT+0xC.
    // Per OoT-Randomizer-Dev/Notes/auto-tracker-ctx.md, this is OoTR's
    // intentionally-stable region for trackers — version field + boss/
    // dungeon shuffle config that we can read without OoTR symbols.
    {
        std::uint8_t atcPtrBytes[4]{};
        if (src.readBytes(0x8040000Cu, atcPtrBytes, 4)) {
            const std::uint32_t atcPtr =
                (std::uint32_t(atcPtrBytes[0]) << 24) |
                (std::uint32_t(atcPtrBytes[1]) << 16) |
                (std::uint32_t(atcPtrBytes[2]) << 8)  |
                 std::uint32_t(atcPtrBytes[3]);
            std::printf("\n=== AUTO_TRACKER_CONTEXT (RANDO_CONTEXT+0xC) ===\n");
            std::printf("  pointer: 0x%08X\n", atcPtr);
            if ((atcPtr & 0xFF000000u) == 0x80000000u && atcPtr < 0x80800000u) {
                std::uint8_t verBytes[4]{};
                if (src.readBytes(atcPtr, verBytes, 4)) {
                    const std::uint32_t ver =
                        (std::uint32_t(verBytes[0]) << 24) |
                        (std::uint32_t(verBytes[1]) << 16) |
                        (std::uint32_t(verBytes[2]) << 8)  |
                         std::uint32_t(verBytes[3]);
                    std::printf("  AUTO_TRACKER_VERSION = %u\n", ver);
                }
            }
        }
    }

    // Resolve OoTR addresses dynamically (the new path — replaces the
    // previous baked addresses that only matched our checked-in OoTR
    // commit). Snapshots below read from the resolved addresses.
    tpt::game::oot::save::OotrAddrs addrs;
    const bool addrsOk = tpt::game::oot::save::resolveOotrAddrs(src, addrs);
    std::printf("\n=== Resolved OoTR addresses (from extern_ctxt) ===\n");
    std::printf("  valid=%s  extern_ctxt=0x%08X\n",
                addrsOk ? "yes" : "no", addrs.externCtxt);
    if (addrsOk) {
        std::printf("    extended_savectx       = 0x%08X\n", addrs.extendedSavectx);
        std::printf("    collectible_flags ptr  = 0x%08X\n", addrs.collectibleFlagsPtr);
        std::printf("    xflag_scene_table      = 0x%08X\n", addrs.xflagSceneTable);
        std::printf("    xflag_room_table       = 0x%08X\n", addrs.xflagRoomTable);
        std::printf("    xflag_room_blob        = 0x%08X\n", addrs.xflagRoomBlob);
    }

    auto snapshot = [&](std::vector<std::uint8_t>& es,
                        std::vector<std::uint8_t>& xfPtr,
                        std::vector<std::uint8_t>& xfScene) {
        es.assign(64, 0);
        xfPtr.assign(8, 0);
        xfScene.assign(32, 0);
        if (!addrsOk) return;
        src.readBytes(addrs.extendedSavectx,    es.data(),    es.size());
        src.readBytes(addrs.collectibleFlagsPtr, xfPtr.data(), xfPtr.size());
        src.readBytes(addrs.xflagSceneTable,    xfScene.data(), xfScene.size());
    };

    std::vector<std::uint8_t> es1, ptr1, scn1;
    snapshot(es1, ptr1, scn1);

    std::printf("\n=== Snapshot 1 ===\n");
    if (addrsOk) {
        dump("extended_savectx", addrs.extendedSavectx, es1);
        dump("xflag ptr+count",  addrs.collectibleFlagsPtr, ptr1);
        dump("xflag scene_table (first 32 bytes)",
             addrs.xflagSceneTable, scn1);
    } else {
        std::printf("  (skipped — RANDO_CONTEXT not resolvable)\n");
    }

    // Boss interpretation. extended_savectx layout: silver_rupee_counts[22]
    // then collected_dungeon_rewards[8] at offset 0x16.
    const auto bossOff = tpt::game::oot::save::kCollectedDungeonRewardsOffset;
    std::printf("\n  Boss interpretation (collected_dungeon_rewards[0..7]):\n");
    static const char* kBossNames[8] = {
        "Queen Gohma", "King Dodongo", "Barinade",     "Phantom Ganon",
        "Volvagia",    "Morpha",       "Twinrova",     "Bongo Bongo",
    };
    for (int i = 0; i < 8; ++i) {
        const std::uint8_t v = es1[bossOff + i];
        std::printf("    [%d] %-14s = 0x%02X %s\n", i, kBossNames[i], v,
                    v ? "(would show [x])" : "(would show [ ])");
    }

    // Sanity: silver_rupee_counts on a fresh save should be all zeros.
    // On a played save with silver-rupee shuffle active, some non-zero
    // counts are expected. Anything looking like high-entropy junk
    // (0xC4, 0xFF, mix of values) on a fresh save is a strong "wrong
    // address" signal.
    bool srcLooksClean = true;
    for (std::size_t i = 0; i < bossOff; ++i) {
        if (es1[i] != 0) { srcLooksClean = false; break; }
    }
    std::printf("\n  silver_rupee_counts looks clean (all zeros)? %s\n",
                srcLooksClean ? "yes" : "NO — possibly wrong address");

    // Drift check.
    std::this_thread::sleep_for(800ms);
    std::vector<std::uint8_t> es2, ptr2, scn2;
    snapshot(es2, ptr2, scn2);

    std::printf("\n=== Snapshot 2 (after ~800ms idle) ===\n");
    std::printf("  extended_savectx       %s\n",
                es1 == es2 ? "STABLE" : "CHANGED");
    std::printf("  xflag ptr+count        %s\n",
                ptr1 == ptr2 ? "STABLE" : "CHANGED");
    std::printf("  xflag scene_table[0:32] %s\n",
                scn1 == scn2 ? "STABLE" : "CHANGED");

    auto diff = [](const char* label, const std::vector<std::uint8_t>& a,
                   const std::vector<std::uint8_t>& b) {
        if (a == b) return;
        std::printf("\n  Diff in %s:\n", label);
        for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
            if (a[i] != b[i]) {
                std::printf("    +0x%02zX: 0x%02X -> 0x%02X\n", i, a[i], b[i]);
            }
        }
    };
    diff("extended_savectx", es1, es2);
    diff("xflag ptr+count",  ptr1, ptr2);
    diff("xflag scene_table", scn1, scn2);

    // Scan a wide window of the OoTR payload region for byte patterns
    // consistent with extended_savectx.collected_dungeon_rewards[8]:
    // the bool[8] follows silver_rupee_counts[22]. We look for windows
    // where bytes -22..-1 are all in [0,30] (plausible silver_rupee
    // counts), bytes 0..7 are all in [0,1] (boolean array), and at
    // least one byte 0..7 is set — Gohma should now be 1.
    {
        std::printf("\n=== Scan for extended_savectx candidates ===\n");
        const std::uint32_t scanStart = 0x80420000;
        const std::uint32_t scanEnd   = 0x80470000;
        const std::size_t   scanSize  = scanEnd - scanStart;
        std::vector<std::uint8_t> blob(scanSize, 0);
        if (!src.readBytes(scanStart, blob.data(), blob.size())) {
            std::printf("  scan read failed\n");
            return 0;
        }
        int hits = 0;
        for (std::size_t i = 22; i + 8 <= blob.size(); ++i) {
            // bool[8] window
            bool boolsOk = true;
            int  ones    = 0;
            for (int k = 0; k < 8; ++k) {
                const auto b = blob[i + k];
                if (b > 1) { boolsOk = false; break; }
                if (b == 1) ++ones;
            }
            if (!boolsOk || ones == 0) continue;
            // silver_rupee_counts[22] window before
            bool srOk = true;
            for (int k = 1; k <= 22; ++k) {
                if (blob[i - k] > 30) { srOk = false; break; }
            }
            if (!srOk) continue;
            // skip "everything = 1" walls
            if (i >= 1 && blob[i - 1] == 1 && blob[i + 8] == 1) continue;
            ++hits;
            if (hits > 12) {
                if (hits == 13) std::printf("  ... (more truncated)\n");
                continue;
            }
            const std::uint32_t fieldAddr = scanStart + static_cast<std::uint32_t>(i);
            const std::uint32_t structAddr = fieldAddr - 0x16;
            // Verify: read the 22 bytes of silver_rupee_counts before
            // and 24 bytes of incoming_queue after. Print both so the
            // human can sanity-check it looks like a real struct.
            std::printf("  candidate extended_savectx @ 0x%08X "
                        "(field @ 0x%08X)\n",
                        structAddr, fieldAddr);
            std::printf("    silver_rupee_counts[22]: ");
            for (int k = -22; k < 0; ++k) {
                std::printf("%02X ", blob[i + k]);
                if (k == -11) std::printf("\n                              ");
            }
            std::printf("\n    bools[8]:                %02X %02X %02X %02X %02X %02X %02X %02X (ones=%d)\n",
                        blob[i+0], blob[i+1], blob[i+2], blob[i+3],
                        blob[i+4], blob[i+5], blob[i+6], blob[i+7], ones);
            if (i + 8 + 24 <= blob.size()) {
                std::printf("    incoming_queue[24]:      ");
                for (int k = 0; k < 24; ++k) {
                    std::printf("%02X ", blob[i + 8 + k]);
                    if (k == 11) std::printf("\n                              ");
                }
                std::printf("\n");
            }
        }
        if (hits == 0) std::printf("  (no candidates)\n");
    }

    return 0;
}

// ===========================================================================
// --oot-events — dump eventChkInf / itemGetInf / infTable bits with named
// labels where decomp provides them. Useful for diagnosing "tracker says
// check X is pending but I think I did it" — confirm directly which bits
// the running ROM has set.
// ===========================================================================
int runOotEvents() {
    tpt::p64::Source src;
    if (!src.connect()) {
        std::fprintf(stderr, "error: could not attach to Project64\n");
        return 2;
    }

    std::vector<std::uint8_t> buf(tpt::game::oot::save::kHeaderReadSize);
    if (!src.readBytes(tpt::game::oot::save::kSaveContextAddr,
                       buf.data(), buf.size())) {
        std::fprintf(stderr, "error: SaveContext read failed\n");
        return 3;
    }

    const auto pd    = tpt::game::oot::save::readPlayerData(buf);
    if (!pd.saveLoaded) {
        std::fprintf(stderr, "error: no save loaded in-game\n");
        return 4;
    }
    const auto flags = tpt::game::oot::save::readSaveFlags(buf);

    // Bit labels — decomp-named bits we'd want to see at a glance.
    // Limited to the ones most useful for check-completion debugging.
    auto labelEvent = [](int bit) -> const char* {
        switch (bit) {
            case 0x02: return "MIDO_DENIED_DEKU_TREE_ACCESS";
            case 0x07: return "(Gohma blue warp)";
            case 0x10: return "TALKED_TO_MALON_FIRST_TIME";
            case 0x12: return "RECEIVED_WEIRD_EGG";
            case 0x13: return "TALON_WOKEN_IN_CASTLE";
            case 0x16: return "CAN_LEARN_EPONAS_SONG";
            case 0x18: return "EPONA_OBTAINED";
            case 0x1E: return "HORSE_RACE_COW_UNLOCK";
            case 0x25: return "(Dodongo blue warp)";
            case 0x33: return "GAVE_LETTER_TO_KING_ZORA";
            case 0x37: return "(Barinade blue warp)";
            case 0x38: return "OBTAINED_SILVER_SCALE";
            case 0x3A: return "OPENED_JABU_JABU";
            case 0x3C: return "DEFEATED_NABOORU_KNUCKLE";
            case 0x40: return "(reached SFM Saria area)";
            case 0x48: return "(Phantom Ganon / Forest Medallion)";
            case 0x49: return "(Volvagia / Fire Medallion)";
            case 0x4A: return "(Morpha / Water Medallion)";
            case 0x4B: return "OPENED_DOOR_OF_TIME";
            case 0x4D: return "CREATED_RAINBOW_BRIDGE";
            case 0x4F: return "REVEALED_MASTER_SWORD";
            case 0x50: return "MINUET_taught (Sheik Forest)";
            case 0x51: return "BOLERO_taught  (Sheik Crater)";
            case 0x52: return "SERENADE_taught(Sheik Ice Cavern)";
            case 0x54: return "NOCTURNE_taught(Sheik Kakariko)";
            case 0x55: return "PRELUDE_taught (Sheik Temple)";
            case 0x57: return "*SARIA_taught (OoTR set)*";
            case 0x58: return "*EPONA_taught (OoTR set)*";
            case 0x59: return "LULLABY_taught (Impa)";
            case 0x5A: return "*SUN_taught (OoTR set, also vanilla revisit)*";
            case 0x5B: return "STORMS_taught (Windmill)";
            case 0x67: return "DRAINED_WELL";
            case 0x69: return "RESTORED_LAKE_HYLIA";
            case 0xA9: return "(Song of Time / OoT cutscene)";
            case 0xAA: return "(Nocturne cutscene entry trigger)";
            case 0xAC: return "(Requiem cutscene entry trigger)";
            case 0xC1: return "(LW Gift from Saria cutscene)";
            case 0xC4: return "(ToT Light Arrows cutscene)";
            case 0xC5: return "(first adult ToT entry)";
            default:   return "";
        }
    };

    std::printf("=== eventChkInf (set bits) ===\n");
    for (std::size_t w = 0; w < flags.eventChkInf.size(); ++w) {
        const std::uint16_t v = flags.eventChkInf[w];
        std::printf("  word[%2zu] = 0x%04X", w, v);
        if (v == 0) { std::printf("\n"); continue; }
        std::printf("  set:");
        for (int b = 0; b < 16; ++b) {
            if (!(v & (1u << b))) continue;
            const int flag = static_cast<int>(w * 16 + b);
            const char* lbl = labelEvent(flag);
            std::printf(" 0x%02X%s%s", flag, *lbl ? "=" : "", lbl);
        }
        std::printf("\n");
    }

    std::printf("\n=== itemGetInf (set bits) ===\n");
    auto labelItemGet = [](int bit) -> const char* {
        switch (bit) {
            case 0x02: return "TALON_BOTTLE";
            case 0x0B: return "DEKU_HEART_PIECE";
            case 0x16: return "(LW Skull Kid heart piece)";
            case 0x17: return "(LW Ocarina Memory Game round 3)";
            case 0x18: return "MAGIC_WIND  (ZF Great Fairy)";
            case 0x19: return "MAGIC_FIRE  (HC Great Fairy)";
            case 0x1A: return "MAGIC_DARK  (Colossus Great Fairy)";
            case 0x1E: return "FOREST_STAGE_STICK_UPGRADE / Deku Theater Skull Mask";
            case 0x1F: return "FOREST_STAGE_NUT_UPGRADE / Deku Theater Mask of Truth";
            default:   return "";
        }
    };
    for (std::size_t w = 0; w < flags.itemGetInf.size(); ++w) {
        const std::uint16_t v = flags.itemGetInf[w];
        std::printf("  word[%2zu] = 0x%04X", w, v);
        if (v == 0) { std::printf("\n"); continue; }
        std::printf("  set:");
        for (int b = 0; b < 16; ++b) {
            if (!(v & (1u << b))) continue;
            const int flag = static_cast<int>(w * 16 + b);
            const char* lbl = labelItemGet(flag);
            std::printf(" 0x%02X%s%s", flag, *lbl ? "=" : "", lbl);
        }
        std::printf("\n");
    }

    std::printf("\n=== infTable (non-zero words only) ===\n");
    int infNonzero = 0;
    for (std::size_t w = 0; w < flags.infTable.size(); ++w) {
        const std::uint16_t v = flags.infTable[w];
        if (v == 0) continue;
        std::printf("  word[%2zu] = 0x%04X\n", w, v);
        ++infNonzero;
    }
    if (infNonzero == 0) std::printf("  (all zero)\n");

    // Direct Sun's Song answer.
    const bool sunsBit = (flags.eventChkInf[5] & (1u << 10)) != 0;
    std::printf("\n=== Sun's Song summary ===\n");
    std::printf("  EVENTCHKINF_5A (OoTR override_suns_song writes here): %s\n",
                sunsBit ? "SET" : "CLEAR");

    // Dump non-zero scene flags so we can correlate which bits flip on
    // which player actions. Especially useful for chasing "the cutscene
    // won't replay so SOMETHING persisted" mysteries.
    std::printf("\n=== Scene flags (non-zero scenes only) ===\n");
    int scenesWithBits = 0;
    for (std::size_t s = 0; s < flags.scenes.size(); ++s) {
        const auto& sf = flags.scenes[s];
        if (sf.chest == 0 && sf.swch == 0 && sf.clear == 0 &&
            sf.collect == 0 && sf.unk == 0) continue;
        std::printf("  scene %3zu (0x%02zX): chest=0x%08X swch=0x%08X clear=0x%08X collect=0x%08X unk=0x%08X\n",
                    s, s, sf.chest, sf.swch, sf.clear, sf.collect, sf.unk);
        ++scenesWithBits;
    }
    if (scenesWithBits == 0) std::printf("  (none)\n");

    // Highlight the Royal Family's Tomb specifically (scene 0x41 = 65).
    // We expect a switch flag set there if the player has done the
    // Sun's Song grave check (the En_Okarina_Tag actor kills itself on
    // init when its switchFlag is set).
    constexpr std::size_t kRoyalFamilysTomb = 0x41;
    if (kRoyalFamilysTomb < flags.scenes.size()) {
        const auto& tomb = flags.scenes[kRoyalFamilysTomb];
        std::printf("\n=== Royal Family's Tomb (scene 0x41) detail ===\n");
        std::printf("  chest=0x%08X swch=0x%08X clear=0x%08X collect=0x%08X unk=0x%08X\n",
                    tomb.chest, tomb.swch, tomb.clear, tomb.collect, tomb.unk);
        if (tomb.swch) {
            std::printf("  swch bits set:");
            for (int b = 0; b < 32; ++b) {
                if (tomb.swch & (std::uint32_t{1} << b)) {
                    std::printf(" %d", b);
                }
            }
            std::printf("\n");
        }
    }
    return 0;
}

}  // namespace

void printUsage(const char* progName) {
    std::printf(
        "tptracker — Twilight Princess auto-tracker (C++ port)\n"
        "\n"
        "Usage:\n"
        "  %s                          launch the GUI (default)\n"
        "  %s --items                  dump decoded inventory and exit\n"
        "  %s --quest                  dump quest state (vitals, portals, ...)\n"
        "  %s --flags                  dump set event flags + set get-item flags\n"
        "  %s --region                 dump region + game ID and exit\n"
        "  %s --save-dump=ADDR,N       hex-dump N bytes from console address ADDR\n"
        "  %s --seed-info              scan for the TPR seed header and dump it\n"
        "  %s --settings=STRING        decode a settings string (no Dolphin needed)\n"
        "  %s --logic-stats            load world data + verify parser (no Dolphin needed)\n"
        "  %s --next [--glitched]      list reachable in-logic checks given current save+seed\n"
        "  %s --placements             dump seed placements per check, flagging progression items\n"
        "  %s --mem-write=ADDR,VALUE   write a single byte to console address ADDR (debug/testing)\n"
        "  %s --dusk-probe             dev tool: introspect a running Dusk process for pattern derivation\n"
        "  %s --p64-probe              dev tool: introspect a running Project64 process for RDRAM derivation\n"
        "  %s --oot-dump               dev tool: decode OoT PlayerData+Inventory from Project64\n"
        "  %s --oot-checks             dev tool: list OoT checks by area with completion status\n"
        "  %s --oot-world              dev tool: load OoTR World/*.json and print region/edge stats\n"
        "  %s --oot-parse              dev tool: parse every rule in the world graph + LogicHelpers\n"
        "  %s --oot-reach              dev tool: run reachability BFS against live save\n"
        "  %s --oot-extras             dev tool: dump OoTR extended_savectx + xflag bytes, watch for drift\n"
        "  %s --oot-events             dev tool: dump eventChkInf / itemGetInf / infTable set bits (labeled)\n"
        "  %s --help                   show this message\n"
        "\n"
        "Common options (apply to Dolphin-backed modes):\n"
        "  --wait=SECS                 max seconds to wait for Dolphin hook (default 5)\n"
        "  --settings-string=STRING    overlay seed-header settings with this web-gen string\n"
        "                              (more complete than seed-header alone — e.g. for --next)\n"
        "\n"
        "Examples:\n"
        "  %s --items\n"
        "  %s --save-dump=0x804061C0,32\n"
        "  %s --settings=\"60s48qabc...\"\n",
        progName, progName, progName, progName, progName,
        progName, progName, progName, progName, progName,
        progName, progName, progName, progName, progName,
        progName, progName, progName, progName, progName,
        progName, progName, progName, progName, progName);
}

Options parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "--help" || a == "-h") { o.mode = Mode::Help; }
        else if (a == "--items")        { o.mode = Mode::Items; }
        else if (a == "--quest")        { o.mode = Mode::Quest; }
        else if (a == "--flags")        { o.mode = Mode::Flags; }
        else if (a == "--region")       { o.mode = Mode::Region; }
        else if (a == "--seed-info")    { o.mode = Mode::SeedInfo; }
        else if (a == "--logic-stats")  { o.mode = Mode::LogicStats; }
        else if (a == "--next")         { o.mode = Mode::Next; }
        else if (a == "--placements")   { o.mode = Mode::Placements; }
        else if (a == "--dusk-probe")   { o.mode = Mode::DuskProbe; }
        else if (a == "--p64-probe")    { o.mode = Mode::P64Probe; }
        else if (a == "--oot-dump")     { o.mode = Mode::OotDump; }
        else if (a == "--oot-checks")   { o.mode = Mode::OotChecks; }
        else if (a == "--oot-world")    { o.mode = Mode::OotWorld; }
        else if (a == "--oot-parse")    { o.mode = Mode::OotParse; }
        else if (a == "--oot-reach")    { o.mode = Mode::OotReach; }
        else if (a == "--oot-extras")   { o.mode = Mode::OotExtras; }
        else if (a == "--oot-events")   { o.mode = Mode::OotEvents; }
        else if (startsWith(a, "--oot-write=")) {
            o.mode = Mode::OotWrite;
            auto rest = a.substr(std::string_view{"--oot-write="}.size());
            auto comma = rest.find(',');
            std::uint32_t valU32 = 0;
            if (comma == std::string_view::npos ||
                !parseHexU32(rest.substr(0, comma), o.writeAddr) ||
                !parseHexU32(rest.substr(comma + 1), valU32)) {
                o.parseError = "bad --oot-write value (expected ADDR,VALUE — both hex)";
                return o;
            }
            if (valU32 > 0xFF) {
                o.parseError = "--oot-write VALUE must be 0x00..0xFF (single byte)";
                return o;
            }
            o.writeValue = static_cast<std::uint8_t>(valU32);
        }
        else if (a == "--glitched")     { o.glitched = true; }
        else if (startsWith(a, "--settings-string=")) {
            o.settingsString = std::string(a.substr(std::string_view{"--settings-string="}.size()));
            if (o.settingsString.empty()) {
                o.parseError = "--settings-string requires a non-empty value";
                return o;
            }
        }
        else if (startsWith(a, "--settings=")) {
            o.mode = Mode::Settings;
            o.settingsString = std::string(a.substr(std::string_view{"--settings="}.size()));
            if (o.settingsString.empty()) {
                o.parseError = "--settings requires a non-empty value";
                return o;
            }
        }
        else if (startsWith(a, "--save-dump=")) {
            o.mode = Mode::SaveDump;
            auto rest = a.substr(std::string_view{"--save-dump="}.size());
            auto comma = rest.find(',');
            if (comma == std::string_view::npos ||
                !parseHexU32(rest.substr(0, comma), o.dumpAddr) ||
                !parseUint(rest.substr(comma + 1), o.dumpLen)) {
                o.parseError = "bad --save-dump value (expected ADDR,N)";
                return o;
            }
            if (o.dumpLen == 0 || o.dumpLen > 0x10000) {
                o.parseError = "--save-dump count must be 1..65536";
                return o;
            }
        }
        else if (startsWith(a, "--mem-write=")) {
            o.mode = Mode::MemWrite;
            auto rest = a.substr(std::string_view{"--mem-write="}.size());
            auto comma = rest.find(',');
            std::uint32_t valU32 = 0;
            if (comma == std::string_view::npos ||
                !parseHexU32(rest.substr(0, comma), o.writeAddr) ||
                !parseHexU32(rest.substr(comma + 1), valU32)) {
                o.parseError = "bad --mem-write value (expected ADDR,VALUE — both hex)";
                return o;
            }
            if (valU32 > 0xFF) {
                o.parseError = "--mem-write VALUE must fit in one byte (0x00..0xFF)";
                return o;
            }
            o.writeValue = static_cast<std::uint8_t>(valU32);
        }
        else if (startsWith(a, "--wait=")) {
            std::uint32_t s;
            if (!parseUint(a.substr(std::string_view{"--wait="}.size()), s) || s == 0 || s > 600) {
                o.parseError = "bad --wait value (expected 1..600)";
                return o;
            }
            o.hookTimeoutSec = static_cast<int>(s);
        }
        else {
            o.parseError = std::string("unknown argument: ") + std::string(a);
            return o;
        }
    }
    return o;
}

int runHeadless(const Options& opts) {
    if (opts.mode == Mode::Help) {
        printUsage("tptracker");
        return 0;
    }

    // --logic-stats doesn't need Dolphin — pure data load + parser smoke test.
    if (opts.mode == Mode::LogicStats) {
        return runLogicStats();
    }

    // --dusk-probe attaches to a running dusklight.exe, not Dolphin. Dev
    // tool: report module layout + (if a PDB is in scope) the resolved
    // address of g_dComIfG_gameInfo, so we can pick a stable AOB pattern.
    if (opts.mode == Mode::P64Probe) {
        return runP64Probe();
    }

    if (opts.mode == Mode::OotDump) {
        return runOotDump();
    }

    if (opts.mode == Mode::OotChecks) {
        return runOotChecks();
    }

    if (opts.mode == Mode::OotWorld) {
        return runOotWorld();
    }

    if (opts.mode == Mode::OotParse) {
        return runOotParse();
    }

    if (opts.mode == Mode::OotReach) {
        return runOotReach();
    }

    if (opts.mode == Mode::OotEvents) {
        return runOotEvents();
    }
    if (opts.mode == Mode::OotExtras) {
        return runOotExtras();
    }

    if (opts.mode == Mode::OotWrite) {
        tpt::p64::Source src;
        if (!src.connect()) {
            std::fprintf(stderr, "error: could not attach to Project64\n");
            return 2;
        }
        std::uint8_t before = 0;
        const bool readOk = src.readBytes(opts.writeAddr, &before, 1);
        if (!src.writeBytes(opts.writeAddr, &opts.writeValue, 1)) {
            std::fprintf(stderr, "error: write failed at 0x%08X\n", opts.writeAddr);
            return 4;
        }
        std::uint8_t after = 0;
        const bool readBackOk = src.readBytes(opts.writeAddr, &after, 1);
        if (readOk && readBackOk) {
            std::printf("0x%08X: %02X -> %02X (after=%02X)\n",
                        opts.writeAddr, before, opts.writeValue, after);
        } else {
            std::printf("0x%08X: wrote 0x%02X\n", opts.writeAddr, opts.writeValue);
        }
        return 0;
    }

    if (opts.mode == Mode::DuskProbe) {
        return runDuskProbe();
    }

    // --settings doesn't need Dolphin — it just decodes a literal string.
    if (opts.mode == Mode::Settings) {
        try {
            const auto parsed = tpt::core::decodeSettingsString(opts.settingsString);
            printParsedSettings(parsed);
            return 0;
        } catch (const tpt::core::SettingsParseError& e) {
            std::fprintf(stderr, "error: %s\n", e.what());
            return 1;
        }
    }

    tpt::dolphin::Client dme;
    if (!waitForConnection(dme, opts.hookTimeoutSec)) {
        std::fprintf(stderr,
            "error: could not connect to emulator within %d seconds (status: %s)\n",
            opts.hookTimeoutSec, tpt::dolphin::toString(dme.status()));
        return 2;
    }

    // --seed-info doesn't need a recognised TP region — the seed magic can
    // sit in any rando-patched build's heap.
    if (opts.mode == Mode::SeedInfo) {
        const auto s = tpt::core::readSeedSettings(dme);
        if (!s) {
            std::fprintf(stderr, "error: no TPR seed header found in main RAM "
                                 "(is the rando REL loaded?)\n");
            return 5;
        }
        printSeedSettings(*s);
        return 0;
    }

    const auto region = tpt::core::detectRegion(dme);
    if (!region) {
        std::fprintf(stderr,
            "error: hooked, but the running game is not a recognised TP build "
            "(game ID: %s)\n", dme.gameId().c_str());
        return 3;
    }

    if (opts.mode == Mode::Region) {
        std::printf("game_id   %s\n", dme.gameId().c_str());
        std::printf("region    %.*s\n",
                    static_cast<int>(region->name.size()), region->name.data());
        std::printf("save_addr 0x%08X\n", region->saveAddr);
        return 0;
    }

    if (opts.mode == Mode::SaveDump) {
        std::vector<std::uint8_t> buf(opts.dumpLen);
        if (!dme.readBytes(opts.dumpAddr, buf.data(), buf.size())) {
            std::fprintf(stderr, "error: read failed at 0x%08X (%u bytes)\n",
                         opts.dumpAddr, opts.dumpLen);
            return 4;
        }
        for (std::uint32_t off = 0; off < opts.dumpLen; off += 16) {
            std::printf("%08X ", opts.dumpAddr + off);
            const std::uint32_t row = std::min<std::uint32_t>(16, opts.dumpLen - off);
            for (std::uint32_t i = 0; i < row; ++i) std::printf(" %02X", buf[off + i]);
            std::printf("\n");
        }
        return 0;
    }

    if (opts.mode == Mode::MemWrite) {
        // Read-back-and-confirm pattern so the user can see the write took.
        std::uint8_t before = 0;
        const bool readOk = dme.readBytes(opts.writeAddr, &before, 1);
        if (!dme.writeBytes(opts.writeAddr, &opts.writeValue, 1)) {
            std::fprintf(stderr, "error: write failed at 0x%08X\n", opts.writeAddr);
            return 4;
        }
        std::uint8_t after = 0;
        const bool readBackOk = dme.readBytes(opts.writeAddr, &after, 1);
        if (readOk && readBackOk) {
            std::printf("0x%08X: %02X -> %02X (after=%02X)\n",
                        opts.writeAddr, before, opts.writeValue, after);
        } else {
            std::printf("0x%08X: wrote 0x%02X (read-back unavailable)\n",
                        opts.writeAddr, opts.writeValue);
        }
        return 0;
    }

    // --next: needs Dolphin + region + save block + seed header (settings).
    if (opts.mode == Mode::Next) {
        std::vector<std::uint8_t> save(tpt::core::kSaveBlockSize);
        if (!dme.readBytes(region->saveAddr, save.data(), save.size())) {
            std::fprintf(stderr, "error: save block read failed at 0x%08X\n",
                         region->saveAddr);
            return 4;
        }
        const std::uint8_t currentNode = save[tpt::core::kOffsetCurrentNode];
        const auto inv  = tpt::core::readInventory(save, currentNode);
        const auto qs   = tpt::core::readQuestState(save, currentNode);
        const auto efs  = tpt::core::logic::readAllEventFlags(save);
        const auto gifs = tpt::core::logic::readAllGetItemFlags(save);

        // Settings overlay: seed header from RAM (best-effort), then user-provided
        // settings string overlay if any.
        std::unordered_map<std::string, std::string> dslSettings =
            tpt::core::logic::dslDefaultSettings();
        std::optional<tpt::core::SeedSettings> seedOpt = tpt::core::readSeedSettings(dme);
        if (seedOpt) {
            for (const auto& [k, v] : tpt::core::logic::dslSettingsFromSeed(*seedOpt))
                dslSettings[k] = v;
        }
        if (!opts.settingsString.empty()) {
            try {
                const auto parsed = tpt::core::decodeSettingsString(opts.settingsString);
                for (const auto& [k, v] : tpt::core::logic::dslSettingsFromParsed(parsed)) {
                    dslSettings[k] = v;
                }
            } catch (const tpt::core::SettingsParseError& e) {
                std::fprintf(stderr, "WARNING: --settings-string parse failed: %s\n", e.what());
            }
        }

        const auto worldDir = findWorldDir();
        if (worldDir.empty()) {
            std::fprintf(stderr,
                "error: could not find data/world (looked next to the exe and in cwd).\n");
            return 6;
        }
        tpt::core::logic::RoomMap rooms;
        tpt::core::logic::CheckMap checks;
        try {
            rooms  = tpt::core::logic::loadRooms(worldDir);
            checks = tpt::core::logic::loadChecks(worldDir);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "error: %s\n", e.what());
            return 6;
        }

        auto ctx = tpt::core::logic::buildContext(
            inv, qs, efs, gifs, opts.glitched, dslSettings);

        const auto warps = tpt::core::logic::warpRoomsFromPortals(qs.portals);
        const auto& reached = tpt::core::logic::reach(
            rooms, ctx, {tpt::core::logic::kDefaultStartRoom}, warps);

        // Build the completed-set from the save-bit bindings — bit set in
        // the save block means we already did this check. For checks the
        // bindings can't resolve (rupees, etc.), seed placements + the
        // item's first-bit give a fallback signal.
        std::unordered_set<std::string> completed;
        const auto dataDir = findWorldDir().parent_path();
        const auto bindings = tpt::core::loadCheckSaveBindings(
            dataDir / "check_save_bindings.json");
        tpt::core::SeedPlacements placements;
        if (seedOpt) {
            const auto placementsIndex = tpt::core::CheckPlacementsIndex::load(
                dataDir / "check_placements.json");
            placements = tpt::core::readSeedPlacements(dme, *seedOpt, placementsIndex);
        }
        if (!bindings.empty() || !placements.empty()) {
            completed = tpt::core::completedCheckSet(
                bindings, save, currentNode, placements);
        }
        const auto pending = tpt::core::logic::pendingInReach(
            rooms, checks, reached, completed, ctx);

        std::printf("=== Next (%s) ===\n", opts.glitched ? "glitched" : "glitchless");
        std::printf("Reachable rooms: %zu   Pending in-logic checks: %zu\n",
                    reached.size(), pending.size());
        if (pending.empty()) {
            std::printf("  (no pending in-logic checks - you may be stuck or already cleared all)\n");
            return 0;
        }

        // Group by region/dungeon. Skip the leading scope (Overworld/Dungeon)
        // and the rando's internal-only tags so e.g. "Faron Mist North Chest"
        // (tagged DZX) ends up in the same bucket as its plain neighbors.
        auto isInternalTag = [](std::string_view t) {
            return t == "ARC" || t == "DZX";
        };
        std::map<std::string, std::vector<std::string>> byBucket;
        for (const auto& name : pending) {
            const auto it = checks.find(name);
            std::string bucket = "(unknown)";
            if (it != checks.end() && !it->second.categories.empty()) {
                const auto& cats = it->second.categories;
                if (cats.size() == 1) {
                    bucket = cats[0];
                } else {
                    bucket.clear();
                    for (std::size_t i = 1; i < cats.size(); ++i) {
                        if (isInternalTag(cats[i])) continue;
                        if (!bucket.empty()) bucket += " / ";
                        bucket += cats[i];
                    }
                    if (bucket.empty()) bucket = cats[0];
                }
            }
            byBucket[bucket].push_back(name);
        }
        for (auto& [bucket, names] : byBucket) {
            std::sort(names.begin(), names.end());
            std::printf("  [%s]  (%zu)\n", bucket.c_str(), names.size());
            for (const auto& n : names) std::printf("    - %s\n", n.c_str());
        }
        return 0;
    }

    // --placements: dump per-check seed placements with progression+completion.
    if (opts.mode == Mode::Placements) {
        auto seed = tpt::core::readSeedSettings(dme);
        if (!seed) {
            std::fprintf(stderr,
                "error: no seed header found in RAM. is the rando running with a seed loaded?\n");
            return 7;
        }
        std::printf("seed: %s  (v%u.%u, header @ 0x%08X)\n",
                    seed->seedName.c_str(), seed->versionMajor, seed->versionMinor,
                    seed->foundAt);

        const auto dataDir = findWorldDir().parent_path();
        const auto index = tpt::core::CheckPlacementsIndex::load(
            dataDir / "check_placements.json");
        if (index.empty()) {
            std::fprintf(stderr, "error: check_placements.json missing or empty at %s\n",
                         (dataDir / "check_placements.json").string().c_str());
            return 8;
        }
        const auto placements = tpt::core::readSeedPlacements(dme, *seed, index);
        if (placements.empty()) {
            std::printf("(no placements parsed — either arcCheckInfo is empty or "
                        "no fingerprints matched)\n");
            return 0;
        }

        std::vector<std::uint8_t> save(tpt::core::kSaveBlockSize);
        const bool haveSave = dme.readBytes(region->saveAddr, save.data(), save.size());
        if (!haveSave) {
            std::fprintf(stderr, "warning: save read failed; completion column will be blank\n");
        }

        // Sort by check name for stable output.
        std::vector<std::pair<std::string, std::uint8_t>> rows(
            placements.begin(), placements.end());
        std::sort(rows.begin(), rows.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        std::size_t total = rows.size();
        std::size_t totalProg = 0, totalProgDone = 0;
        std::size_t totalRupee = 0, totalRupeeProg = 0, totalRupeeProgDone = 0;

        std::printf("\n%-50s  %-4s  %-4s  %-4s  item\n",
                    "check", "prog", "done", "rupe");
        std::printf("%s\n", std::string(78, '-').c_str());
        for (const auto& [name, itemId] : rows) {
            const bool prog  = tpt::core::isProgressionItemId(itemId);
            const bool done  = haveSave && tpt::core::readGetItemFlag(save, itemId);
            const bool rupee = name.find("Rupee") != std::string::npos;
            if (prog) ++totalProg;
            if (prog && done) ++totalProgDone;
            if (rupee) ++totalRupee;
            if (rupee && prog) ++totalRupeeProg;
            if (rupee && prog && done) ++totalRupeeProgDone;
            std::printf("%-50s  %-4s  %-4s  %-4s  0x%02X\n",
                        name.c_str(),
                        prog  ? "yes" : "-",
                        haveSave ? (done ? "yes" : "no") : "?",
                        rupee ? "yes" : "-",
                        itemId);
        }
        std::printf("\nTotals: %zu placements, %zu progression (%zu done)\n",
                    total, totalProg, totalProgDone);
        std::printf("Rupees: %zu total, %zu progression (%zu done)\n",
                    totalRupee, totalRupeeProg, totalRupeeProgDone);
        return 0;
    }

    // Save-block backed modes.
    if (opts.mode == Mode::Items || opts.mode == Mode::Quest || opts.mode == Mode::Flags) {
        std::vector<std::uint8_t> save(tpt::core::kSaveBlockSize);
        if (!dme.readBytes(region->saveAddr, save.data(), save.size())) {
            std::fprintf(stderr, "error: save block read failed at 0x%08X\n",
                         region->saveAddr);
            return 4;
        }
        const std::uint8_t currentNode = save[tpt::core::kOffsetCurrentNode];
        std::printf("game_id %s   region %.*s   current_node 0x%02X\n",
                    dme.gameId().c_str(),
                    static_cast<int>(region->name.size()), region->name.data(),
                    currentNode);

        if (opts.mode == Mode::Items) {
            printInventory(tpt::core::readInventory(save, currentNode));
        } else if (opts.mode == Mode::Quest) {
            printQuestState(tpt::core::readQuestState(save, currentNode));
        } else {
            printSetFlags(save);
        }
        return 0;
    }

    std::fprintf(stderr, "error: unhandled mode\n");
    return 1;
}

}  // namespace tpt::cli
