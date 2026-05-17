#include "core/EventFlags.h"

namespace tpt::core {

namespace {

constexpr EventFlagEntry kEventFlagData[] = {
    // Dungeon clears.
    {"FOREST_TEMPLE_CLEARED",       0x602},
    {"GORON_MINES_CLEARED",         0x701},
    {"LAKEBED_TEMPLE_CLEARED",      0x904},
    {"ARBITERS_GROUNDS_CLEARED",    0x2010},
    {"SNOWPEAK_RUINS_CLEARED",      0x2008},
    {"TEMPLE_OF_TIME_CLEARED",      0x2004},
    {"CITY_IN_THE_SKY_CLEARED",     0x2002},
    {"PALACE_OF_TWILIGHT_CLEARED",  0x5410},

    // Twilight clears.
    {"CLEARED_FARON_TWILIGHT",        0x610},
    {"CLEARED_ELDIN_TWILIGHT",        0x708},
    {"CLEARED_LANAYRU_TWILIGHT",      0xC02},
    {"MIDNAS_DESPERATE_HOUR_STARTED",   0xC01},
    {"MIDNAS_DESPERATE_HOUR_COMPLETED", 0x1E08},

    // Wolf form.
    {"TRANSFORMING_UNLOCKED",  0xD04},
    {"MIDNA_CHARGE_UNLOCKED",  0x501},
    {"MIDNA_ACCOMPANIES_WOLF", 0xC10},
    {"SENSES_UNLOCKED",        0x4308},

    // Major story / inventory milestones.
    {"GOT_MASTER_SWORD",          0x2020},
    {"ESCAPED_CELL_IN_SEWERS",    0x540},
    {"FINISHED_SEWERS",           0x502},
    {"EPONA_TAMED",               0x601},
    {"GOT_FISHING_ROD_FROM_ULI",  0x301},
    {"GOT_LANTERN_FROM_CORO",     0xF01},

    // Quest items.
    {"GOT_RENADOS_LETTER",          0xF80},
    {"GAVE_TELMA_RENADOS_LETTER",   0x2180},
    {"GOT_WOOD_STATUE",             0x2204},
    {"GAVE_ILIA_THE_WOOD_STATUE",   0x2340},
    {"GOT_ILIAS_CHARM",             0x2280},
    {"GAVE_ILIA_HER_CHARM",         0x2320},
    {"GAVE_INVOICE_TO_DOCTOR",      0x2710},
    {"GOT_AURUS_MEMO",              0x2510},
    {"SHOWED_AURUS_MEMO_TO_FYER",   0x2680},
    {"GOT_ASHEIS_SKETCH",           0x2940},
    {"GOT_CORAL_EARRING_FROM_RALIS", 0x3B80},
    {"GOT_BOTTLE_FROM_SERA",        0x1408},
    {"GOT_BOTTLE_FROM_JOVANI",      0x4D80},

    // Sky Book chain.
    {"ANCIENT_SKYBOOK_FROM_IMPAZ",  0x5F80},
    {"FARON_SKY_LETTER",            0x6080},
    {"DESERT_SKY_LETTER",           0x6040},
    {"GORGE_SKY_LETTER",            0x6020},
    {"BRIDGE_OF_ELDIN_SKY_LETTER",  0x6010},
    {"LAKE_HYLIA_SKY_LETTER",       0x6008},
    {"AMPITHEATER_SKYLETTER",       0x6204},
    {"SHAD_USED_COMPLETED_SKYBOOK", 0x2540},

    // Hidden skills (the 7 skill unlocks).
    {"ENDING_BLOW_UNLOCKED",        0x2904},
    {"SHIELD_ATTACK_UNLOCKED",      0x2908},
    {"BACKSLICE_UNLOCKED",          0x2902},
    {"HELM_SPLITTER_UNLOCKED",      0x2901},
    {"GREAT_SPIN_UNLOCKED",         0x2A20},
    {"JUMP_STRIKE_UNLOCKED",        0x2A40},
    {"MORTAL_DRAW_UNLOCKED",        0x2A80},

    // Major world unlocks.
    {"FIXED_THE_MIRROR_OF_TWILIGHT",       0x2B08},
    {"BARRIER_GONE",                       0x4208},
    {"HIDDEN_VILLAGE_BARRIER_REMOVED",     0x2E08},
    {"BRIDGE_REPAIR_FUNDRAISING_COMPLETED", 0x2E20},
    {"MALO_MART_CASTLE_TOWN_BRANCH_IS_OPEN", 0x2210},
    {"MALO_MART_FUNDRAISING_STARTS",       0x1E80},
    {"BOUGHT_HYLIAN_SHIELD_AT_MALO_MART",  0x6102},
    {"BRIDGE_OF_ELDIN_STOLEN",             0xA20},
    {"WARPED_BRIDGE_OF_ELDIN_BACK",        0xF08},
    {"SKY_CANNON_REPAIRED",                0x3B08},
    {"WARPED_SKY_CANNON_TO_LAKE_HYLIA",    0x3120},
    {"CAN_NOW_WARP_METEOR",                0x5D01},
    {"WARPED_METEOR_TO_ZORAS_DOMAIN",      0x880},
    {"RAISED_MIRROR_IN_MIRROR_CHAMBER",    0x2C10},

    // Snowpeak progression.
    {"TALKED_TO_YETO_IN_SPR_FOR_FIRST_TIME",        0x140},
    {"TOLD_YETA_ABOUT_CHEESE",                      0x120},
    {"TOLD_YETA_ABOUT_PUMPKIN",                     0x480},
    {"TALKED_WITH_YETA_AFTER_GIVING_CHEESE",        0x1420},
    {"TALKED_WITH_YETA_AFTER_GIVING_PUMPKIN",       0x1440},
    {"CHEESE_PUT_IN_SOUP",                          0x0001},
    {"PUMPKIN_PUT_IN_SOUP",                         0x0002},
    {"GOT_SNOWPEAK_RUINS_MAP_FROM_YETA",            0xB10},
    {"TALKED_WITH_YETA_AFTER_GETTING_BEDROOM_KEY",  0xD10},
    {"WON_SNOWBOARD_RACE_AGAINST_YETO",             0x3B40},
    {"WON_SNOWBOARD_RACE_AGAINST_YETA",             0x3B10},

    // Iza / Upper Zoras River.
    {"IZA_1_MINIGAME_UNLOCKED",         0xB02},
    {"IZA_1_MINIGAME_DONE",             0xB01},
    {"IZA_2_MINIGAME_DONE",             0x5908},
    {"TALKED_TO_IZA_BEFORE_UZR_PORTAL", 0x1304},

    // Story progression beats.
    {"WAGON_ESCORT_STARTED",          0x840},
    {"ZORA_ESCORT_CLEARED",           0x810},
    {"KING_BULBLIN_1_DEFEATED",       0xA08},
    {"FYRUS_GETS_UP_FIRST_TIME",      0x5210},
    {"KNOCKED_FYRUS_DOWN_FIRST_TIME", 0x5220},

    // Howling stones.
    {"HOWLED_AT_HIDDEN_VILLAGE_STONE",       0x3A04},
    {"HOWLED_AT_SNOWPEAK_STONE",             0x3A08},
    {"HOWLED_AT_LAKE_HYLIA_STONE",           0x3A10},
    {"HOWLED_AT_SACRED_GROVE_OUTSIDE_STONE", 0x3A20},
    {"HOWLED_AT_UPPER_ZORAS_RIVER_STONE",    0x3A40},
    {"HOWLED_AT_DEATH_MOUNTAIN_STONE",       0x3A80},

    // Sacred Grove.
    {"SACRED_GROVE_STATUE_PUZZLE_COMPLETED", 0x1C04},
    {"SACRED_GROVE_STATUES_SWITCHED",        0x4A08},

    // Misc gameplay events that gate checks.
    {"GAVE_ALL_24_GOLDEN_BUGS_TO_AGITHA",  0x2E04},
    {"WON_PLUMMS_HEART_PIECE",             0x2380},
    {"DONATED_1000_RUPEES_TO_CHARLO",      0x2480},
    {"FUNDED_CASTLE_TOWN_MALO_MART",       0xF10},
    {"CAT_MINIGAME_DONE",                  0x5B08},
    {"WATCHED_START_OF_GAME_CUTSCENE",     0x1010},
    {"MAP_WARPING_UNLOCKED",               0x604},
    {"GOATS_3_DONE",                       0x4240},
    {"CAUGHT_THE_SINKING_LURE",            0x3920},
};

constexpr GetItemFlagEntry kGetItemFlagData[] = {
    // Quest items (often consumed).
    {"Renados_Letter",        0x80},
    {"Invoice",               0x81},
    {"Wooden_Statue",         0x82},
    {"Ilias_Charm",           0x83},
    {"Horse_Call",            0x84},
    {"Aurus_Memo",            0x90},
    {"Asheis_Sketch",         0x91},
    {"Coro_Bottle",           0x9D},
    {"Jovani_Bottle",         0x75},
    {"Sera_Bottle",           0x65},

    // Vessels.
    {"Vessel_Of_Light_Faron",   0xA1},
    {"Vessel_Of_Light_Eldin",   0xA2},
    {"Vessel_Of_Light_Lanayru", 0xA3},

    // Special keys (rando + vanilla).
    {"Coro_Key",                0xFE},
    {"Small_Key_N_Faron_Gate",  0xEE},
    {"Gate_Keys",               0xF3},
    {"Ordon_Pumpkin",           0xF4},
    {"Ordon_Goat_Cheese",       0xF5},
    {"Bed_Key",                 0xF6},
    {"Key_Shard_1",             0xF9},
    {"Key_Shard_2",             0xFA},
    {"Key_Shard_3",             0xFB},
    {"Big_Key_Goron_Mines",     0xFD},
    {"Got_Lantern_Back",        0xF8},

    // Sky Book progression items.
    {"Ancient_Sky_Book_Empty",         0xE9},
    {"Ancient_Sky_Book_Partly_Filled", 0xEA},
    {"Ancient_Sky_Book_Completed",     0xEB},

    // Scents.
    {"Ilias_Scent",     0xB0},
    {"Poe_Scent",       0xB2},
    {"Reekfish_Scent",  0xB3},
    {"Youths_Scent",    0xB4},
    {"Medicine_Scent",  0xB5},
};

}  // namespace

std::span<const EventFlagEntry>   eventFlagTable()   { return kEventFlagData; }
std::span<const GetItemFlagEntry> getItemFlagTable() { return kGetItemFlagData; }

bool readEventFlag(std::span<const std::uint8_t> block, std::uint16_t raw) {
    const auto [byteOff, mask] = decodeEventFlag(raw);
    return (block[kOffsetEventBlock + byteOff] & mask) != 0;
}

bool readGetItemFlag(std::span<const std::uint8_t> block, std::uint8_t itemId) {
    // dSv_player_get_item_c.mItemsFlags[8] is 8 big-endian uint32 words.
    // Item N's bit lives in word (N/32) at bit position (N%32) — but BE
    // storage maps bit B of a uint32 to byte (3 - B/8) within the word,
    // bit (B%8) of that byte. Empirically matches items.cpp's hand-tuned
    // (offset, mask) pairs for Shadow_Crystal/Lantern/etc.
    const std::uint32_t wordIdx   = itemId / 32;
    const std::uint32_t bitInWord = itemId % 32;
    const std::uint32_t byteOff   = kOffsetGetItemFlags
                                  + 4 * wordIdx
                                  + (3 - bitInWord / 8);
    const std::uint8_t  mask      = static_cast<std::uint8_t>(1u << (bitInWord % 8));
    return (block[byteOff] & mask) != 0;
}

}  // namespace tpt::core
