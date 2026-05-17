#include "core/Items.h"

#include <array>
#include <utility>

#include "core/SaveOffsets.h"

namespace tpt::core {

namespace {

inline bool bit(std::span<const std::uint8_t> b, std::size_t off, std::uint8_t mask) {
    return (b[off] & mask) != 0;
}

struct GoldenBug {
    std::string_view name;
    std::uint16_t    offset;
    std::uint8_t     mask;
};

constexpr std::array<GoldenBug, 24> kGoldenBugs{{
    {"Male Beetle",        0xE7, 0x01}, {"Female Beetle",      0xE7, 0x02},
    {"Male Butterfly",     0xE7, 0x04}, {"Female Butterfly",   0xE7, 0x08},
    {"Male Stag Beetle",   0xE7, 0x10}, {"Female Stag Beetle", 0xE7, 0x20},
    {"Male Grasshopper",   0xE7, 0x40}, {"Female Grasshopper", 0xE7, 0x80},
    {"Male Phasmid",       0xE6, 0x01}, {"Female Phasmid",     0xE6, 0x02},
    {"Male Pill Bug",      0xE6, 0x04}, {"Female Pill Bug",    0xE6, 0x08},
    {"Male Mantis",        0xE6, 0x10}, {"Female Mantis",      0xE6, 0x20},
    {"Male Ladybug",       0xE6, 0x40}, {"Female Ladybug",     0xE6, 0x80},
    {"Male Snail",         0xE5, 0x01}, {"Female Snail",       0xE5, 0x02},
    {"Male Dragonfly",     0xE5, 0x04}, {"Female Dragonfly",   0xE5, 0x08},
    {"Male Ant",           0xE5, 0x10}, {"Female Ant",         0xE5, 0x20},
    {"Male Dayfly",        0xE5, 0x40}, {"Female Dayfly",      0xE5, 0x80},
}};

struct DungeonNode {
    std::string_view name;
    std::uint8_t     id;
};

// Must match locations.NodeID.
constexpr std::array<DungeonNode, 9> kDungeonNodes{{
    {"Forest Temple",    0x10}, {"Goron Mines",       0x11},
    {"Lakebed Temple",   0x12}, {"Arbiters Grounds",  0x13},
    {"Snowpeak Ruins",   0x14}, {"Temple of Time",    0x15},
    {"City in The Sky",  0x16}, {"Palace of Twilight", 0x17},
    {"Hyrule Castle",    0x18},
}};

}  // namespace

Inventory readInventory(std::span<const std::uint8_t> b, std::uint8_t currentNode) {
    Inventory inv;

    // Progressive Master Sword.
    if      (bit(b, 0xD6, 0x02)) inv.sword = 4;
    else if (bit(b, 0xD2, 0x02)) inv.sword = 3;
    else if (bit(b, 0xD2, 0x01)) inv.sword = 2;
    else if (bit(b, 0xD0, 0x80)) inv.sword = 1;

    // Progressive Hero's Bow.
    if      (bit(b, 0xD5, 0x40)) inv.bow = 3;
    else if (bit(b, 0xD5, 0x20)) inv.bow = 2;
    else if (bit(b, 0xD7, 0x08)) inv.bow = 1;

    // Progressive Clawshot.
    if      (bit(b, 0xD7, 0x80)) inv.clawshot = 2;
    else if (bit(b, 0xD7, 0x10)) inv.clawshot = 1;

    // Progressive Dominion Rod.
    if      (bit(b, 0xD7, 0x40)) inv.dominionRod = 2;
    else if (bit(b, 0xD6, 0x10)) inv.dominionRod = 1;

    // Progressive Fishing Rod.
    if      (bit(b, 0xD0, 0x20)) inv.fishingRod = 2;
    else if (bit(b, 0xD6, 0x08)) inv.fishingRod = 1;

    // Progressive Wallet.
    if      (bit(b, 0xD1, 0x40)) inv.wallet = 2;
    else if (bit(b, 0xD1, 0x20)) inv.wallet = 1;

    // Hidden Skills — count distinct bits set so the number reflects total
    // skills learned (apworld picks the highest, which doesn't reflect total).
    constexpr std::array<std::pair<std::uint16_t, std::uint8_t>, 7> kSkillBits{{
        {0x81A, 0x20}, {0x81A, 0x40}, {0x81A, 0x80},  // Great Spin / Jump Strike / Mortal Draw
        {0x819, 0x01}, {0x819, 0x02}, {0x819, 0x08},  // Helm Splitter / Backslice / Shield Attack
        {0x819, 0x04},                                 // Ending Blow
    }};
    for (auto [off, m] : kSkillBits) {
        if (bit(b, off, m)) ++inv.hiddenSkills;
    }

    // Fused Shadows.
    if      (bit(b, 0x109, 0x04)) inv.fusedShadows = 3;
    else if (bit(b, 0x109, 0x02)) inv.fusedShadows = 2;
    else if (bit(b, 0x109, 0x01)) inv.fusedShadows = 1;

    // Mirror Shards.
    if      (bit(b, 0x10A, 0x08)) inv.mirrorShards = 4;
    else if (bit(b, 0x10A, 0x04)) inv.mirrorShards = 3;
    else if (bit(b, 0x10A, 0x02)) inv.mirrorShards = 2;
    else if (bit(b, 0x10A, 0x01)) inv.mirrorShards = 1;

    // Single-flag items.
    inv.ordonShield   = bit(b, 0xD2, 0x04);
    inv.hylianShield  = bit(b, 0xD2, 0x10);
    inv.magicArmor    = bit(b, 0xD1, 0x01);
    inv.zoraArmor     = bit(b, 0xD1, 0x02);
    inv.shadowCrystal = bit(b, 0xD1, 0x04);
    inv.hawkeye       = bit(b, 0xD0, 0x40);
    inv.lantern       = bit(b, 0xD6, 0x01);
    inv.galeBoomerang = bit(b, 0xD7, 0x01);
    inv.spinner       = bit(b, 0xD7, 0x02);
    inv.ballAndChain  = bit(b, 0xD7, 0x04);
    inv.ironBoots     = bit(b, 0xD7, 0x20);
    inv.slingshot     = bit(b, 0xD8, 0x01);
    inv.auruMemo      = bit(b, 0xDD, 0x01);
    inv.asheiSketch   = bit(b, 0xDD, 0x02);
    inv.horseCall     = bit(b, 0xDF, 0x10);
    inv.giantBombBag  = bit(b, 0xD6, 0x80);
    inv.gateKeys      = bit(b, 0xE8, 0x08);

    // Bomb-bag slots: 3 bytes, 0xFF = empty.
    for (auto off : {0xAB, 0xAC, 0xAD}) {
        if (b[off] != 0xFF) ++inv.bombBags;
    }
    // Bottle slots: 4 bytes, 0xFF = empty.
    for (auto off : {0xA7, 0xA8, 0xA9, 0xAA}) {
        if (b[off] != 0xFF) ++inv.bottles;
    }

    inv.poeSouls = b[0x10C];

    // Golden bugs.
    for (const auto& g : kGoldenBugs) {
        if (bit(b, g.offset, g.mask)) inv.bugs.emplace(g.name);
    }

    // Per-dungeon items: each node has a 32-byte block, with the active
    // node's block held in the kOffsetActiveNode buffer instead.
    for (const auto& d : kDungeonNodes) {
        const std::uint32_t base = (d.id == currentNode)
            ? kOffsetActiveNode
            : kOffsetNodesStart + d.id * 32;
        DungeonItems di;
        di.smallKeys  = b[base + 0x1C];
        di.hasMap     = bit(b, base + 0x1D, 0x01);
        di.hasCompass = bit(b, base + 0x1D, 0x02);
        di.hasBigKey  = bit(b, base + 0x1D, 0x04);
        inv.dungeonItems.emplace(std::string(d.name), di);
    }

    return inv;
}

bool isProgressionItemId(std::uint8_t itemId) noexcept {
    // Major equipment & unlocks.
    switch (itemId) {
        // Swords (progressive: Wooden -> Ordon -> Master -> MS_Light).
        case 0x3F:  // Wooden_Sword
        case 0x28:  // Ordon_Sword
        case 0x29:  // Master_Sword
        case 0x49:  // Master_Sword_Light
        // Bows / Quivers (progressive).
        case 0x43:  // Heros_Bow
        case 0x55:  // Big_Quiver
        case 0x56:  // Giant_Quiver
        // Clawshots (progressive).
        case 0x44:  // Clawshot
        case 0x47:  // Double_Clawshots
        // Dominion Rod (progressive).
        case 0x46:  // Dominion_Rod_Uncharged
        case 0x4C:  // Dominion_Rod
        // Fishing Rod / Coral Earring (progressive).
        case 0x4A:  // Fishing_Rod
        case 0x3D:  // Coral_Earring
        // Wallets (progressive).
        case 0x35:  // Big_Wallet
        case 0x36:  // Giant_Wallet
        // Other key equipment.
        case 0x40:  // Boomerang (Gale)
        case 0x41:  // Spinner
        case 0x42:  // Ball_and_Chain
        case 0x45:  // Iron_Boots
        case 0x48:  // Lantern
        case 0x4B:  // Slingshot
        case 0x3E:  // Hawkeye
        case 0x32:  // Shadow_Crystal
        // Armor / shields.
        case 0x2C:  // Hylian_Shield
        case 0x30:  // Magic_Armor
        case 0x31:  // Zora_Armor
        // Bomb bags.
        case 0x4F:  // Giant_Bomb_Bag
        case 0x50:  // Empty_Bomb_Bag
        case 0x51:  // Goron_Bomb_Bag
        case 0x70:  // Bomb_Bag_Regular_Bombs
        case 0x71:  // Bomb_Bag_Water_Bombs
        case 0x72:  // Bomb_Bag_Bomblings
        // Bottles (unique-NPC bottles count as progression).
        case 0x60:  // Empty_Bottle
        case 0x65:  // Sera_Bottle
        case 0x75:  // Jovani_Bottle
        case 0x9D:  // Coro_Bottle
        // Vessels of Light (story-gating).
        case 0xA1:  // Vessel_Of_Light_Faron
        case 0xA2:  // Vessel_Of_Light_Eldin
        case 0xA3:  // Vessel_Of_Light_Lanayru
        // Sky-book progression.
        case 0xE9:  // Ancient_Sky_Book_Empty
        case 0xEA:  // Ancient_Sky_Book_Partly_Filled
        case 0xEB:  // Ancient_Sky_Book_Completed
        // Story / quest trade items.
        case 0x80:  // Renardos_Letter
        case 0x81:  // Invoice
        case 0x82:  // Wooden_Statue
        case 0x83:  // Ilias_Charm
        case 0x84:  // Horse_Call
        case 0x90:  // Aurus_Memo
        case 0x91:  // Asheis_Sketch
        // Scents (wolf-quest gates).
        case 0xB0:  // Ilias_Scent
        case 0xB2:  // Poe_Scent
        case 0xB3:  // Reekfish_Scent
        case 0xB4:  // Youths_Scent
        case 0xB5:  // Medicine_Scent
        // Special / story keys.
        case 0xEE:  // Small_Key_N_Faron_Gate
        case 0xF3:  // Gate_Keys
        case 0xF4:  // Ordon_Pumpkin
        case 0xF5:  // Ordon_Goat_Cheese
        case 0xF6:  // Bed_Key
        case 0xF8:  // Got_Lantern_Back
        case 0xF9:  // Key_Shard_1
        case 0xFA:  // Key_Shard_2
        case 0xFB:  // Key_Shard_3
        case 0xFD:  // Big_Key_Goron_Mines (= Goron Mines big key)
        case 0xFE:  // Coro_Key (Faron Coro Key)
        // Custom-rando per-dungeon small/big keys (forest..hyrule).
        case 0x85: case 0x86: case 0x87: case 0x88: case 0x89:  // SK Forest..Snowpeak
        case 0x8A: case 0x8B: case 0x8C: case 0x8D:             // SK ToT..HyruleCastle
        case 0x8E:                                              // Bulblin_Camp_Key
        case 0x92: case 0x93: case 0x94: case 0x95: case 0x96:  // BK Forest..CitS
        case 0x97: case 0x98:                                   // BK PoT, HyruleCastle
        // Custom-rando mirror shards & fused shadows.
        case 0xA5: case 0xA6: case 0xA7:  // Mirror_Piece_2/3/4 (libtp "unused" slots)
        case 0xD8: case 0xD9: case 0xDA:  // Fused_Shadow_1/2/3
        case 0xDB:                        // Mirror_Piece_1 (custom)
        // Custom-rando hidden skills (Ending Blow ... Great Spin).
        case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5:
        case 0xE6: case 0xE7:
            return true;
        default:
            return false;
    }
}

}  // namespace tpt::core
