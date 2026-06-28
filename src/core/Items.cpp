#include "core/Items.h"

#include <array>
#include <utility>

#include "core/EventFlags.h"   // readGetItemFlag
#include "core/SaveOffsets.h"

namespace tpt::core {

namespace {

inline bool bit(std::span<const std::uint8_t> b, std::size_t off, std::uint8_t mask) {
    return (b[off] & mask) != 0;
}

// Get-item "first-bit" item IDs (vendor/libtp_rel/include/data/items.h plus the
// custom rando IDs in Randomizer-master/.../customItems.h). Reading via
// readGetItemFlag(b, id) expresses these in terms of the canonical item ID
// rather than hand-computed (byte, mask) pairs into player_get_item — the
// latter caused real off-by-one bugs (Slingshot / Fishing Rod / Gate Keys).
namespace id {
constexpr std::uint8_t Wooden_Sword = 0x3F, Ordon_Sword = 0x28,
                       Master_Sword = 0x29, Master_Sword_Light = 0x49;
constexpr std::uint8_t Heros_Bow = 0x43, Big_Quiver = 0x55, Giant_Quiver = 0x56;
constexpr std::uint8_t Clawshot = 0x44, Double_Clawshots = 0x47;
constexpr std::uint8_t Dominion_Rod_Uncharged = 0x46, Dominion_Rod = 0x4C;
constexpr std::uint8_t Fishing_Rod = 0x4A, Coral_Earring = 0x3D;
constexpr std::uint8_t Big_Wallet = 0x35, Giant_Wallet = 0x36;
constexpr std::uint8_t Ordon_Shield = 0x2A, Hylian_Shield = 0x2C,
                       Magic_Armor = 0x30, Zora_Armor = 0x31;
constexpr std::uint8_t Shadow_Crystal = 0x32, Hawkeye = 0x3E, Lantern = 0x48;
constexpr std::uint8_t Boomerang = 0x40, Spinner = 0x41, Ball_and_Chain = 0x42,
                       Iron_Boots = 0x45, Slingshot = 0x4B;
constexpr std::uint8_t Aurus_Memo = 0x90, Asheis_Sketch = 0x91, Horse_Call = 0x84;
constexpr std::uint8_t Giant_Bomb_Bag = 0x4F, Gate_Keys = 0xF3;
}  // namespace id

// Golden bugs are also get-item flags: the 24 bugs occupy item IDs 0xC0..0xD7.
struct GoldenBug {
    std::string_view name;
    std::uint8_t     itemId;
};

constexpr std::array<GoldenBug, 24> kGoldenBugs{{
    {"Male Beetle",      0xC0}, {"Female Beetle",      0xC1},
    {"Male Butterfly",   0xC2}, {"Female Butterfly",   0xC3},
    {"Male Stag Beetle", 0xC4}, {"Female Stag Beetle", 0xC5},
    {"Male Grasshopper", 0xC6}, {"Female Grasshopper", 0xC7},
    {"Male Phasmid",     0xC8}, {"Female Phasmid",     0xC9},
    {"Male Pill Bug",    0xCA}, {"Female Pill Bug",    0xCB},
    {"Male Mantis",      0xCC}, {"Female Mantis",      0xCD},
    {"Male Ladybug",     0xCE}, {"Female Ladybug",     0xCF},
    {"Male Snail",       0xD0}, {"Female Snail",       0xD1},
    {"Male Dragonfly",   0xD2}, {"Female Dragonfly",   0xD3},
    {"Male Ant",         0xD4}, {"Female Ant",         0xD5},
    {"Male Dayfly",      0xD6}, {"Female Dayfly",      0xD7},
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
    if      (readGetItemFlag(b, id::Master_Sword_Light)) inv.sword = 4;
    else if (readGetItemFlag(b, id::Master_Sword))       inv.sword = 3;
    else if (readGetItemFlag(b, id::Ordon_Sword))        inv.sword = 2;
    else if (readGetItemFlag(b, id::Wooden_Sword))       inv.sword = 1;

    // Progressive Hero's Bow.
    if      (readGetItemFlag(b, id::Giant_Quiver)) inv.bow = 3;
    else if (readGetItemFlag(b, id::Big_Quiver))   inv.bow = 2;
    else if (readGetItemFlag(b, id::Heros_Bow))    inv.bow = 1;

    // Progressive Clawshot.
    if      (readGetItemFlag(b, id::Double_Clawshots)) inv.clawshot = 2;
    else if (readGetItemFlag(b, id::Clawshot))         inv.clawshot = 1;

    // Progressive Dominion Rod. The rando's _04_verifyItemFunctions hands out
    // Dominion_Rod_Uncharged (Powerless) on the first pickup and the charged
    // Dominion_Rod on the second, so tier 2 must win the precedence.
    if      (readGetItemFlag(b, id::Dominion_Rod))           inv.dominionRod = 2;
    else if (readGetItemFlag(b, id::Dominion_Rod_Uncharged)) inv.dominionRod = 1;

    // Progressive Fishing Rod (tier 2 = the Coral Earring upgrade).
    if      (readGetItemFlag(b, id::Coral_Earring)) inv.fishingRod = 2;
    else if (readGetItemFlag(b, id::Fishing_Rod))   inv.fishingRod = 1;

    // Progressive Wallet.
    if      (readGetItemFlag(b, id::Giant_Wallet)) inv.wallet = 2;
    else if (readGetItemFlag(b, id::Big_Wallet))   inv.wallet = 1;

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
    inv.ordonShield   = readGetItemFlag(b, id::Ordon_Shield);
    inv.hylianShield  = readGetItemFlag(b, id::Hylian_Shield);
    inv.magicArmor    = readGetItemFlag(b, id::Magic_Armor);
    inv.zoraArmor     = readGetItemFlag(b, id::Zora_Armor);
    inv.shadowCrystal = readGetItemFlag(b, id::Shadow_Crystal);
    inv.hawkeye       = readGetItemFlag(b, id::Hawkeye);
    inv.lantern       = readGetItemFlag(b, id::Lantern);
    inv.galeBoomerang = readGetItemFlag(b, id::Boomerang);
    inv.spinner       = readGetItemFlag(b, id::Spinner);
    inv.ballAndChain  = readGetItemFlag(b, id::Ball_and_Chain);
    inv.ironBoots     = readGetItemFlag(b, id::Iron_Boots);
    inv.slingshot     = readGetItemFlag(b, id::Slingshot);
    inv.auruMemo      = readGetItemFlag(b, id::Aurus_Memo);
    inv.asheiSketch   = readGetItemFlag(b, id::Asheis_Sketch);
    inv.horseCall     = readGetItemFlag(b, id::Horse_Call);
    inv.giantBombBag  = readGetItemFlag(b, id::Giant_Bomb_Bag);
    inv.gateKeys      = readGetItemFlag(b, id::Gate_Keys);

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
        if (readGetItemFlag(b, g.itemId)) inv.bugs.emplace(g.name);
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
