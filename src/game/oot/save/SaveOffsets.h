#pragma once

#include <cstddef>
#include <cstdint>

namespace tpt::game::oot::save {

// gSaveContext lives at this N64 KSEG0 virtual address for OoT NTSC 1.0.
// Other versions (1.1 / 1.2 / PAL 1.0 / MQ Debug / JP) shift it slightly;
// the tracker currently assumes 1.0. Version detection lands when the
// first non-1.0 user reports they're stuck.
inline constexpr std::uint32_t kSaveContextAddr = 0x8011A5D0;

// Bytes to fetch on every poll. Covers everything we decode:
// Save header, PlayerData, Inventory, SavedSceneFlags[124], gsFlags[6],
// eventChkInf[14], itemGetInf[4], infTable[30] (ends at 0xF34), and
// scarecrowSpawnSongSet at 0x12A9. 0x1400 gives slack past 0x12A9.
inline constexpr std::size_t kHeaderReadSize = 0x1400;

// ----- Offsets relative to SaveContext start --------------------------------
//
// Layout is from oot-main/include/save.h. The second column of comments
// in that file is the offset within SaveContext, which is what we use here.

// Save struct fields (the top-level Save inside SaveContext):
inline constexpr std::uint32_t kOffEntranceIndex = 0x0000;  // s32
inline constexpr std::uint32_t kOffLinkAge       = 0x0004;  // s32 (0=Adult, 1=Child per LinkAge)
inline constexpr std::uint32_t kOffDayTime       = 0x000C;  // u16
inline constexpr std::uint32_t kOffNightFlag     = 0x0010;  // s32
inline constexpr std::uint32_t kOffTotalDays     = 0x0014;  // s32

// SaveInfo.playerData (at SaveContext+0x001C):
inline constexpr std::uint32_t kOffNewf                    = 0x001C;  // char[6] = "ZELDAZ"
inline constexpr std::uint32_t kOffDeaths                  = 0x0022;  // u16
inline constexpr std::uint32_t kOffPlayerName              = 0x0024;  // u8[8]
inline constexpr std::uint32_t kOffHealthCapacity          = 0x002E;  // s16, 16 units = 1 heart
inline constexpr std::uint32_t kOffHealth                  = 0x0030;  // s16
inline constexpr std::uint32_t kOffMagicLevel              = 0x0032;  // s8: 0/1/2
inline constexpr std::uint32_t kOffMagic                   = 0x0033;  // s8: current
inline constexpr std::uint32_t kOffRupees                  = 0x0034;  // s16
inline constexpr std::uint32_t kOffSwordHealth             = 0x0036;  // u16 (Biggoron sword durability)
inline constexpr std::uint32_t kOffIsMagicAcquired         = 0x003A;  // u8
inline constexpr std::uint32_t kOffIsDoubleMagicAcquired   = 0x003C;  // u8
inline constexpr std::uint32_t kOffIsDoubleDefenseAcquired = 0x003D;  // u8
inline constexpr std::uint32_t kOffBgsFlag                 = 0x003E;  // u8 (Biggoron sword owned)

// SaveInfo.inventory (at SaveContext+0x0074):
inline constexpr std::uint32_t kOffInventoryItems          = 0x0074;  // u8[24]
inline constexpr std::uint32_t kOffInventoryAmmo           = 0x008C;  // s8[16]
inline constexpr std::uint32_t kOffInventoryEquipment      = 0x009C;  // u16 (nibble per type)
inline constexpr std::uint32_t kOffInventoryUpgrades       = 0x00A0;  // u32 (per-type bitfield)
inline constexpr std::uint32_t kOffInventoryQuestItems     = 0x00A4;  // u32 (per-bit medallions/songs/stones)
inline constexpr std::uint32_t kOffInventoryDungeonItems   = 0x00A8;  // u8[20] (boss key/compass/map per scene)
inline constexpr std::uint32_t kOffInventoryDungeonKeys    = 0x00BC;  // s8[19] (small-key counts)
inline constexpr std::uint32_t kOffInventoryDefenseHearts  = 0x00CF;  // s8
inline constexpr std::uint32_t kOffInventoryGsTokens       = 0x00D0;  // s16 (gold skulltula count)

// ----- SaveInfo flag arrays (everything past Inventory) ---------------------
//
// Layout from oot-main/include/save.h:SaveInfo. Per-scene flag blocks come
// first, then gold-skulltula bitmasks, then event/item-get/inf-table arrays.
// All offsets are within SaveContext.

// SaveInfo.sceneFlags[124] — 124 × SavedSceneFlags (0x1C bytes each).
// SavedSceneFlags fields: chest, swch, clear, collect, unk, rooms, floors.
// Total span: 0x00D4 .. 0x0E48.
inline constexpr std::uint32_t kOffSceneFlagsBase     = 0x00D4;
inline constexpr std::size_t   kSceneFlagsStride      = 0x1C;
inline constexpr std::size_t   kSceneFlagsCount       = 124;
inline constexpr std::uint32_t kSceneFlagOffChest     = 0x00;
inline constexpr std::uint32_t kSceneFlagOffSwch      = 0x04;
inline constexpr std::uint32_t kSceneFlagOffClear     = 0x08;
inline constexpr std::uint32_t kSceneFlagOffCollect   = 0x0C;
inline constexpr std::uint32_t kSceneFlagOffUnk       = 0x10;  // scrub/misc one-time actor flags
inline constexpr std::uint32_t kSceneFlagOffRooms     = 0x14;
inline constexpr std::uint32_t kSceneFlagOffFloors    = 0x18;

// Gold-skulltula collection bitmasks. 6 × u32 = 192 bits, but only the
// low byte of each word is used per "skulltula scene" (see
// gGsFlagsMasks/Shifts in oot-main/src/code/z_inventory.c:83). Effectively
// 24 scenes × 8 token bits each.
inline constexpr std::uint32_t kOffGsFlags      = 0x0E9C;
inline constexpr std::size_t   kGsFlagsCount    = 6;
inline constexpr std::size_t   kGsScenesPerWord = 4;
inline constexpr std::size_t   kGsScenesCount   = kGsFlagsCount * kGsScenesPerWord;  // 24

// Event-check info bits — story/quest events, song-learned events,
// dungeon-cleared rewards. 14 × u16 = 224 bits.
inline constexpr std::uint32_t kOffEventChkInf = 0x0ED4;
inline constexpr std::size_t   kEventChkInfWords = 14;  // u16 each

// Item-get info bits — one-time NPC item pickups. 4 × u16 = 64 bits.
inline constexpr std::uint32_t kOffItemGetInf = 0x0EF0;
inline constexpr std::size_t   kItemGetInfWords = 4;  // u16 each

// InfTable bits — NPC interaction state. 30 × u16 = 480 bits.
inline constexpr std::uint32_t kOffInfTable = 0x0EF8;
inline constexpr std::size_t   kInfTableWords = 30;  // u16 each

// Scarecrow's Song "spawn song" (the 8-note song Pierre listens for).
// Set to non-zero by z_message.c after the second recording (as adult).
// Used as the completion signal for OoTR's "Pierre" location.
inline constexpr std::uint32_t kOffScarecrowSpawnSongSet = 0x12A9;  // u8

// HighScores array — 7 u32 entries. Indices per save.h HighScores enum:
//   0 = ARCHERY, 1 = POE_POINTS, 2 = FISHING, 3 = HORSE_RACE, ...
// HS_FISHING (index 2) holds a bitfield used as the fishing-prize signal:
//   bit 0x0400 = child prize received
//   bit 0x0800 = adult prize received
//   bit 0x8000 = OoTR Loach prize received (OoT-Randomizer-Dev/ASM/src/fishing.asm:64)
inline constexpr std::uint32_t kOffHighScores  = 0x0EB8;  // s32[7]
inline constexpr std::uint32_t kOffHsFishing   = kOffHighScores + 2 * 4;  // 0x0EC0

// ----- Bit layouts (cross-ref oot-main/src/code/z_inventory.c) --------------
//
// Equipment ownership bitmask (u16 at kOffInventoryEquipment). Each
// EquipmentType occupies a 4-bit nibble; bits within the nibble are the
// EquipInv* enum values.
inline constexpr std::uint8_t kEquipNibbleSword  = 0;   // bits 0..3
inline constexpr std::uint8_t kEquipNibbleShield = 4;   // bits 4..7
inline constexpr std::uint8_t kEquipNibbleTunic  = 8;   // bits 8..11
inline constexpr std::uint8_t kEquipNibbleBoots  = 12;  // bits 12..15

// Upgrade bitfield (u32 at kOffInventoryUpgrades). Per-type shifts and
// widths come from gUpgradeShifts and gUpgradeMasks. All widths are 3
// except wallet which is 2.
struct UpgradeField { std::uint8_t shift; std::uint8_t width; };
inline constexpr UpgradeField kUpgQuiver     { 0,  3};
inline constexpr UpgradeField kUpgBombBag    { 3,  3};
inline constexpr UpgradeField kUpgStrength   { 6,  3};
inline constexpr UpgradeField kUpgScale      { 9,  3};
inline constexpr UpgradeField kUpgWallet     {12,  2};
inline constexpr UpgradeField kUpgBulletBag  {14,  3};
inline constexpr UpgradeField kUpgDekuSticks {17,  3};
inline constexpr UpgradeField kUpgDekuNuts   {20,  3};

}  // namespace tpt::game::oot::save
