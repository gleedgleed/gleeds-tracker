#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace tpt::core {

// Tier label tables — index 0 is "none".
inline constexpr std::array<std::string_view, 5> kSwordTiers{
    "-", "Wooden Sword", "Ordon Sword", "Master Sword", "Light Sword"};
inline constexpr std::array<std::string_view, 4> kBowTiers{
    "-", "Hero's Bow", "Big Quiver", "Giant Quiver"};
inline constexpr std::array<std::string_view, 3> kClawshotTiers{
    "-", "Clawshot", "Double Clawshot"};
inline constexpr std::array<std::string_view, 3> kDominionTiers{
    "-", "Powerless Dominion Rod", "Dominion Rod"};
inline constexpr std::array<std::string_view, 3> kFishingTiers{
    "-", "Fishing Rod", "Coral Earring"};
inline constexpr std::array<std::string_view, 3> kWalletTiers{
    "-", "Big Wallet", "Giant Wallet"};

struct DungeonItems {
    std::uint8_t smallKeys = 0;
    bool hasMap     = false;
    bool hasCompass = false;
    bool hasBigKey  = false;
};

struct Inventory {
    // Progressive items (0 = none).
    std::uint8_t sword         = 0;  // 0..4
    std::uint8_t bow           = 0;  // 0..3
    std::uint8_t clawshot      = 0;  // 0..2
    std::uint8_t dominionRod   = 0;  // 0..2
    std::uint8_t fishingRod    = 0;  // 0..2
    std::uint8_t wallet        = 0;  // 0..2
    std::uint8_t hiddenSkills  = 0;  // 0..7
    std::uint8_t fusedShadows  = 0;  // 0..3
    std::uint8_t mirrorShards  = 0;  // 0..4

    // Single-flag items.
    bool ordonShield   = false;
    bool hylianShield  = false;
    bool magicArmor    = false;
    bool zoraArmor     = false;
    bool shadowCrystal = false;
    bool hawkeye       = false;
    bool lantern       = false;
    bool galeBoomerang = false;
    bool spinner       = false;
    bool ballAndChain  = false;
    bool ironBoots     = false;
    bool slingshot     = false;
    bool auruMemo      = false;
    bool asheiSketch   = false;
    bool horseCall     = false;
    bool giantBombBag  = false;
    bool gateKeys      = false;

    // Counts.
    std::uint8_t bombBags = 0;   // 0..3 occupied slots
    std::uint8_t bottles  = 0;   // 0..4 occupied slots
    std::uint8_t poeSouls = 0;

    // Sets / maps.
    std::unordered_set<std::string>          bugs;
    std::unordered_map<std::string, DungeonItems> dungeonItems;
};

// Decode the player's inventory from a save block read at SAVE_FILE_ADDR.
// `block` must point to at least kSaveBlockSize bytes; `currentNode` is the
// active dungeon ID (read from save[kOffsetCurrentNode]).
Inventory readInventory(std::span<const std::uint8_t> block,
                        std::uint8_t currentNode);

// Item IDs that count as progression for "is this rupee/check worth showing?"
// purposes. Mirrors the rando's _02_*ItemGetCheck guarded items plus the major
// equipment, story, and unlock items. IDs come from
// vendor/libtp_rel/include/data/items.h and Randomizer-master/.../customItems.h.
bool isProgressionItemId(std::uint8_t itemId) noexcept;

}  // namespace tpt::core
