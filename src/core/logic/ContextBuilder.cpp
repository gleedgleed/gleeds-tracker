#include "core/logic/ContextBuilder.h"

#include <array>
#include <utility>

#include "core/logic/Predicates.h"

namespace tpt::core::logic {

namespace {

void put(Context& ctx, std::string name, int countN) {
    if (countN > 0) ctx.items.emplace(std::move(name), countN);
}

const char* yn(bool b) { return b ? "true" : "false"; }

constexpr std::array<std::pair<const char*, const char*>, 9> kDungeonDsl{{
    {"Forest Temple",      "Forest_Temple"},
    {"Goron Mines",        "Goron_Mines"},
    {"Lakebed Temple",     "Lakebed_Temple"},
    {"Arbiters Grounds",   "Arbiters_Grounds"},
    {"Snowpeak Ruins",     "Snowpeak_Ruins"},
    {"Temple of Time",     "Temple_of_Time"},
    {"City in The Sky",    "City_in_the_Sky"},
    {"Palace of Twilight", "Palace_of_Twilight"},
    {"Hyrule Castle",      "Hyrule_Castle"},
}};

// Bug display name -> DSL name (snake_case).
constexpr std::array<std::pair<const char*, const char*>, 24> kBugDsl{{
    {"Male Beetle",        "Male_Beetle"},
    {"Female Beetle",      "Female_Beetle"},
    {"Male Butterfly",     "Male_Butterfly"},
    {"Female Butterfly",   "Female_Butterfly"},
    {"Male Stag Beetle",   "Male_Stag_Beetle"},
    {"Female Stag Beetle", "Female_Stag_Beetle"},
    {"Male Grasshopper",   "Male_Grasshopper"},
    {"Female Grasshopper", "Female_Grasshopper"},
    {"Male Phasmid",       "Male_Phasmid"},
    {"Female Phasmid",     "Female_Phasmid"},
    {"Male Pill Bug",      "Male_Pill_Bug"},
    {"Female Pill Bug",    "Female_Pill_Bug"},
    {"Male Mantis",        "Male_Mantis"},
    {"Female Mantis",      "Female_Mantis"},
    {"Male Ladybug",       "Male_Ladybug"},
    {"Female Ladybug",     "Female_Ladybug"},
    {"Male Snail",         "Male_Snail"},
    {"Female Snail",       "Female_Snail"},
    {"Male Dragonfly",     "Male_Dragonfly"},
    {"Female Dragonfly",   "Female_Dragonfly"},
    {"Male Ant",           "Male_Ant"},
    {"Female Ant",         "Female_Ant"},
    {"Male Dayfly",        "Male_Dayfly"},
    {"Female Dayfly",      "Female_Dayfly"},
}};

// quest_item_map: DSL name -> get-item-flag name (which is itself the libtp item name).
constexpr std::array<std::pair<const char*, const char*>, 13> kQuestItemMap{{
    {"Renados_Letter",                   "Renados_Letter"},
    {"Invoice",                          "Invoice"},
    {"Wooden_Statue",                    "Wooden_Statue"},
    {"Statue",                           "Wooden_Statue"},
    {"Ilias_Charm",                      "Ilias_Charm"},
    {"Charm",                            "Ilias_Charm"},
    {"Coro_Bottle",                      "Coro_Bottle"},
    {"Sera_Bottle",                      "Sera_Bottle"},
    {"Jovani_Bottle",                    "Jovani_Bottle"},
    {"Bed_Key",                          "Bed_Key"},
    {"Snowpeak_Ruins_Bedroom_Key",       "Bed_Key"},
    {"Faron_Woods_Coro_Key",             "Coro_Key"},
    {"North_Faron_Woods_Gate_Key",       "Small_Key_N_Faron_Gate"},
}};

constexpr std::array<std::pair<const char*, const char*>, 8> kBossEvents{{
    {"Diababa_Defeated",   "FOREST_TEMPLE_CLEARED"},
    {"Fyrus_Defeated",     "GORON_MINES_CLEARED"},
    {"Morpheel_Defeated",  "LAKEBED_TEMPLE_CLEARED"},
    {"Stallord_Defeated",  "ARBITERS_GROUNDS_CLEARED"},
    {"Blizzeta_Defeated",  "SNOWPEAK_RUINS_CLEARED"},
    {"Armogohma_Defeated", "TEMPLE_OF_TIME_CLEARED"},
    {"Argorok_Defeated",   "CITY_IN_THE_SKY_CLEARED"},
    {"Zant_Defeated",      "PALACE_OF_TWILIGHT_CLEARED"},
}};

bool flagIsTrue(const std::unordered_map<std::string, bool>& flags, const char* name) {
    const auto it = flags.find(name);
    return it != flags.end() && it->second;
}

}  // namespace

Context buildContext(
    const Inventory& inv,
    const QuestState& qs,
    const std::unordered_map<std::string, bool>& eventFlags,
    const std::unordered_map<std::string, bool>& getItemFlags,
    bool glitched,
    const std::unordered_map<std::string, std::string>& settings) {

    Context ctx;
    ctx.glitched = glitched;
    ctx.darkClearLevel = qs.darkClearLevel;
    ctx.eventFlags = eventFlags;
    ctx.getItemFlags = getItemFlags;
    ctx.settings = settings;

    // Progressive items.
    put(ctx, "Progressive_Sword",        inv.sword);
    put(ctx, "Progressive_Bow",          inv.bow);
    put(ctx, "Progressive_Clawshot",     inv.clawshot);
    put(ctx, "Progressive_Dominion_Rod", inv.dominionRod);
    put(ctx, "Progressive_Fishing_Rod",  inv.fishingRod);
    put(ctx, "Progressive_Wallet",       inv.wallet);
    put(ctx, "Progressive_Hidden_Skill", inv.hiddenSkills);
    put(ctx, "Progressive_Fused_Shadow", inv.fusedShadows);
    put(ctx, "Progressive_Mirror_Shard", inv.mirrorShards);

    // Single-flag items.
    if (inv.ordonShield)   put(ctx, "Ordon_Shield", 1);
    if (inv.hylianShield)  put(ctx, "Hylian_Shield", 1);
    if (inv.magicArmor)    put(ctx, "Magic_Armor", 1);
    if (inv.zoraArmor)     put(ctx, "Zora_Armor", 1);
    if (inv.hawkeye)       put(ctx, "Hawkeye", 1);
    if (inv.lantern)       put(ctx, "Lantern", 1);
    if (inv.galeBoomerang) put(ctx, "Boomerang", 1);  // rando DSL uses "Boomerang"
    if (inv.spinner)       put(ctx, "Spinner", 1);
    if (inv.ballAndChain)  put(ctx, "Ball_and_Chain", 1);
    if (inv.ironBoots)     put(ctx, "Iron_Boots", 1);
    if (inv.slingshot)     put(ctx, "Slingshot", 1);
    if (inv.auruMemo)      put(ctx, "Aurus_Memo", 1);
    if (inv.asheiSketch)   put(ctx, "Asheis_Sketch", 1);
    if (inv.horseCall)     put(ctx, "Horse_Call", 1);
    if (inv.gateKeys)      put(ctx, "Gate_Keys", 1);

    // Shadow_Crystal: gated on TRANSFORMING_UNLOCKED for edge-case correctness.
    // (The rando assumes prologue is done; we honor the actual save state.)
    const bool tfUnlocked = flagIsTrue(eventFlags, "TRANSFORMING_UNLOCKED");
    if (inv.shadowCrystal && (tfUnlocked || eventFlags.empty())) {
        put(ctx, "Shadow_Crystal", 1);
    }

    // Counts.
    put(ctx, "Bomb_Bag",     inv.bombBags);
    if (inv.bombBags > 0) put(ctx, "Filled_Bomb_Bag", 1);  // proxy; we don't track shells
    put(ctx, "Empty_Bottle", inv.bottles);
    put(ctx, "Poe_Soul",     inv.poeSouls);

    // Per-dungeon items.
    for (const auto& [display, prefix] : kDungeonDsl) {
        const auto it = inv.dungeonItems.find(display);
        if (it == inv.dungeonItems.end()) continue;
        const auto& di = it->second;
        std::string p(prefix);
        put(ctx, p + "_Small_Key", di.smallKeys);
        if (di.hasMap)     put(ctx, p + "_Map", 1);
        if (di.hasCompass) put(ctx, p + "_Compass", 1);
        if (di.hasBigKey)  put(ctx, p + "_Big_Key", 1);
    }

    // Bugs.
    for (const auto& [disp, dsl] : kBugDsl) {
        if (inv.bugs.count(disp)) put(ctx, dsl, 1);
    }

    // Switch-key state (overworld gates) — open gate -> "have" the DSL key.
    for (const auto& sk : qs.switchKeys) {
        if (sk.open) put(ctx, std::string(sk.name), 1);
    }

    // First-bit-got items: letters / quest items / special keys.
    for (const auto& [dsl, libtp] : kQuestItemMap) {
        if (flagIsTrue(getItemFlags, libtp)) put(ctx, dsl, 1);
    }

    // Goron Mines key shards: 3 shards combine into Big Key.
    int shardCount = 0;
    if (flagIsTrue(getItemFlags, "Key_Shard_1")) ++shardCount;
    if (flagIsTrue(getItemFlags, "Key_Shard_2")) ++shardCount;
    if (flagIsTrue(getItemFlags, "Key_Shard_3")) ++shardCount;
    if (shardCount > 0) put(ctx, "Goron_Mines_Key_Shard", shardCount);
    if (shardCount >= 3 || flagIsTrue(getItemFlags, "Big_Key_Goron_Mines")) {
        put(ctx, "Goron_Mines_Big_Key", 1);
    }

    // Progressive Sky Book: 1 (book) + each of 6 character letters.
    if (flagIsTrue(eventFlags, "ANCIENT_SKYBOOK_FROM_IMPAZ")) {
        int tier = 1;
        for (const auto* letter : {"FARON_SKY_LETTER", "DESERT_SKY_LETTER",
                                   "GORGE_SKY_LETTER", "BRIDGE_OF_ELDIN_SKY_LETTER",
                                   "LAKE_HYLIA_SKY_LETTER", "AMPITHEATER_SKYLETTER"}) {
            if (flagIsTrue(eventFlags, letter)) ++tier;
        }
        put(ctx, "Progressive_Sky_Book", tier);
    }

    // Virtual boss-defeated items, aliased from dungeon-clear event flags.
    for (const auto& [virt, ev] : kBossEvents) {
        if (flagIsTrue(eventFlags, ev)) put(ctx, virt, 1);
    }

    registerPredicates(ctx);
    return ctx;
}

std::unordered_map<std::string, bool>
readAllEventFlags(std::span<const std::uint8_t> save) {
    std::unordered_map<std::string, bool> out;
    for (const auto& e : eventFlagTable()) {
        out.emplace(std::string(e.name), readEventFlag(save, e.raw));
    }
    return out;
}

std::unordered_map<std::string, bool>
readAllGetItemFlags(std::span<const std::uint8_t> save) {
    std::unordered_map<std::string, bool> out;
    for (const auto& e : getItemFlagTable()) {
        out.emplace(std::string(e.name), readGetItemFlag(save, e.itemId));
    }
    return out;
}

std::unordered_map<std::string, std::string>
dslSettingsFromSeed(const SeedSettings& s) {
    std::unordered_map<std::string, std::string> d;
    d["castleRequirements"]    = s.castleRequirements;
    d["castleBKRequirements"]  = s.castleBkRequirements;
    d["palaceRequirements"]    = s.palaceRequirements;
    d["walletSize"]            = s.walletSize;
    d["damageMagnification"]   = s.damageMagnification;
    d["mirrorChamberEntrance"] = s.mirrorChamberEntrance;
    d["castleRequirementCount"]   = std::to_string(s.castleRequirementCount);
    d["castleBKRequirementCount"] = std::to_string(s.castleBkRequirementCount);
    d["totEntranceTier"]       = std::to_string(s.totEntranceTier);
    // Boolean settings derived from map_clear_bits.
    for (const auto& f : s.mapClearFlags) d[f.name] = yn(f.value);
    // Seed flags: some are exposed as Setting comparators in DSL.
    for (const auto& f : s.seedFlags) {
        if (f.name == "TRANSFORM_ANYWHERE")     d["transformAnywhere"]    = yn(f.value);
        else if (f.name == "QUICK_TRANSFORM")   d["quickTransform"]       = yn(f.value);
        else if (f.name == "INCREASE_SPINNER_SPEED") d["increaseSpinnerSpeed"] = yn(f.value);
        else if (f.name == "BONKS_DO_DAMAGE")   d["bonksDoDamage"]        = yn(f.value);
        else if (f.name == "AUTOFILL_WALLETS")  d["autoFillWallet"]       = yn(f.value);
        else if (f.name == "MODIFY_SHOP_MODELS")d["modifyShopModels"]     = yn(f.value);
    }
    // Volatile patches.
    for (const auto& f : s.volatilePatches) {
        if (f.name == "skipMdh") d["skipMdh"] = yn(f.value);
    }
    return d;
}

std::unordered_map<std::string, std::string> dslDefaultSettings() {
    return dslSettingsFromParsed(defaultParsedSettings());
}

std::unordered_map<std::string, std::string>
dslSettingsFromParsed(const ParsedSettings& s) {
    std::unordered_map<std::string, std::string> d;
    d["logicRules"]               = s.logicRules;
    d["castleRequirements"]       = s.castleRequirements;
    d["castleBKRequirements"]     = s.castleBkRequirements;
    d["palaceRequirements"]       = s.palaceRequirements;
    d["faronWoodsLogic"]          = s.faronWoodsLogic;
    d["shufflePoes"]              = s.shufflePoes;
    d["smallKeySettings"]         = s.smallKeySettings;
    d["bigKeySettings"]           = s.bigKeySettings;
    d["mapAndCompassSettings"]    = s.mapAndCompassSettings;
    d["walletSize"]               = s.walletSize;
    d["trapFrequency"]            = s.trapFrequency;
    d["goronMinesEntrance"]       = s.goronMinesEntrance;
    d["totEntrance"]              = s.totEntrance;
    d["itemScarcity"]             = s.itemScarcity;
    d["damageMagnification"]      = s.damageMagnification;
    d["startingToD"]              = s.startingTod;
    d["hintDistribution"]         = s.hintDistribution;
    d["iliaQuest"]                = s.iliaQuest;
    d["mirrorChamberEntrance"]    = s.mirrorChamberEntrance;
    d["shuffleDungeonEntrances"]  = s.shuffleDungeonEntrances;
    d["hintImportance"]           = s.hintImportance;

    d["shuffleGoldenBugs"]    = yn(s.shuffleGoldenBugs);
    d["shuffleSkyCharacters"] = yn(s.shuffleSkyCharacters);
    d["shuffleNpcItems"]      = yn(s.shuffleNpcItems);
    d["shuffleShopItems"]     = yn(s.shuffleShopItems);
    d["shuffleHiddenSkills"]  = yn(s.shuffleHiddenSkills);
    d["skipPrologue"]         = yn(s.skipPrologue);
    d["faronTwilightCleared"] = yn(s.faronTwilightCleared);
    d["eldinTwilightCleared"] = yn(s.eldinTwilightCleared);
    d["lanayruTwilightCleared"] = yn(s.lanayruTwilightCleared);
    d["skipMdh"]              = yn(s.skipMdh);
    d["skipMinorCutscenes"]   = yn(s.skipMinorCutscenes);
    d["fastIronBoots"]        = yn(s.fastIronBoots);
    d["quickTransform"]       = yn(s.quickTransform);
    d["transformAnywhere"]    = yn(s.transformAnywhere);
    d["modifyShopModels"]     = yn(s.modifyShopModels);
    d["barrenDungeons"]       = yn(s.barrenDungeons);
    d["skipLakebedEntrance"]  = yn(s.skipLakebedEntrance);
    d["skipArbitersEntrance"] = yn(s.skipArbitersEntrance);
    d["skipSnowpeakEntrance"] = yn(s.skipSnowpeakEntrance);
    d["skipGroveEntrance"]    = yn(s.skipGroveEntrance);
    d["skipCityEntrance"]     = yn(s.skipCityEntrance);
    d["instantText"]          = yn(s.instantText);
    d["openMap"]              = yn(s.openMap);
    d["increaseSpinnerSpeed"] = yn(s.increaseSpinnerSpeed);
    d["openDot"]              = yn(s.openDot);
    d["bonksDoDamage"]        = yn(s.bonksDoDamage);
    d["shuffleRewards"]       = yn(s.shuffleRewards);
    d["skipMajorCutscenes"]   = yn(s.skipMajorCutscenes);
    d["noSmallKeysOnBosses"]  = yn(s.noSmallKeysOnBosses);
    d["randomizeStartingPoint"] = yn(s.randomizeStartingPoint);
    d["shuffleHiddenRupees"]  = yn(s.shuffleHiddenRupees);
    d["gmShortcut"]           = yn(s.gmShortcut);
    d["hcShortcut"]           = yn(s.hcShortcut);
    d["unpairEntrances"]      = yn(s.unpairEntrances);
    d["decoupleEntrances"]    = yn(s.decoupleEntrances);
    d["shuffleFreestandingRupees"] = yn(s.shuffleFreestandingRupees);
    d["autoFillWallet"]       = yn(s.autoFillWallet);
    d["skipBridgeDonation"]   = yn(s.skipBridgeDonation);
    d["noPlandoHints"]        = yn(s.noPlandoHints);
    d["adjustHintsForCompletionists"] = yn(s.adjustHintsForCompletionists);
    d["hintDungeonEntrances"] = yn(s.hintDungeonEntrances);

    d["castleRequirementCount"]   = std::to_string(s.castleRequirementCount);
    d["castleBKRequirementCount"] = std::to_string(s.castleBkRequirementCount);
    d["maloShopDonation"]         = std::to_string(s.maloShopDonation);

    // Derive totEntranceTier (used by CanStrikePedestal).
    int tier = 0;
    if      (s.totEntrance == "Wooden_Sword") tier = 1;
    else if (s.totEntrance == "Ordon_Sword")  tier = 2;
    else if (s.totEntrance == "Master_Sword") tier = 3;
    else if (s.totEntrance == "Light_Sword")  tier = 4;
    d["totEntranceTier"] = std::to_string(tier);
    return d;
}

std::vector<std::string>
warpRoomsFromPortals(const std::vector<PortalState>& portals) {
    static const std::array<std::pair<std::string_view, std::string_view>, 14> kMap{{
        {"South Faron",       "South Faron Woods"},
        {"North Faron",       "North Faron Woods"},
        {"Kakariko Gorge",    "Kakariko Gorge"},
        {"Kakariko Village",  "Lower Kakariko Village"},
        {"Death Mountain",    "Death Mountain Trail"},
        {"Castle Town",       "Outside Castle Town West"},
        {"Zoras Domain",      "Zoras Domain"},
        {"Lake Hylia",        "Lake Hylia"},
        {"Gerudo Desert",     "Gerudo Desert"},
        {"Mirror Chamber",    "Mirror Chamber"},
        {"Snowpeak",          "Snowpeak Summit Upper"},
        {"Sacred Grove",      "Sacred Grove Lower"},
        {"Bridge of Eldin",   "Eldin Field"},
        {"Upper Zoras River", "Upper Zoras River"},
    }};
    std::vector<std::string> out;
    for (const auto& p : portals) {
        if (!p.unlocked) continue;
        for (const auto& [portal, room] : kMap) {
            if (portal == p.name) {
                out.emplace_back(room);
                break;
            }
        }
    }
    return out;
}

}  // namespace tpt::core::logic
