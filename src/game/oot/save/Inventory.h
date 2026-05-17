#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace tpt::game::oot::save {

// Decoded Inventory struct. Bitmasks (equipment, upgrades, questItems)
// are unpacked into per-piece booleans / per-type tier integers so the
// rest of the tracker doesn't have to know the on-disk layout.
//
// Field semantics cross-referenced against the canonical decomp:
//   oot-main/include/save.h  (struct Inventory)
//   oot-main/include/item.h  (EquipInv*, UpgradeType, QuestItem enums)
//   oot-main/src/code/z_inventory.c  (gUpgradeMasks, gEquipShifts, etc.)
struct Inventory {
    // Raw slot arrays. items[slot] holds the active ItemID in that slot
    // (or 0xFF if empty); slot indices follow the InventorySlot enum
    // (SLOT_DEKU_STICK=0 .. SLOT_TRADE_CHILD=0x17). ammo[] mirrors only
    // the slots that carry counted ammo (bow, slingshot, bombs, …).
    std::array<std::uint8_t, 24> items{};
    std::array<std::int8_t,  16> ammo{};

    // Equipment ownership (one bool per piece). Active equipment is
    // tracked separately in ItemEquips.equipment — this struct records
    // which pieces the player owns at all.
    bool swordKokiri      = false, swordMaster      = false;
    bool swordBiggoron    = false, swordBrokenGiant = false;
    bool shieldDeku       = false, shieldHylian     = false, shieldMirror = false;
    bool tunicKokiri      = false, tunicGoron       = false, tunicZora    = false;
    bool bootsKokiri      = false, bootsIron        = false, bootsHover   = false;

    // Upgrade tiers. Capacity mapping (from gUpgradeCapacities):
    //   quiver:     0/30/40/50 arrows
    //   bombBag:    0/20/30/40 bombs
    //   strength:   none/Goron Bracelet/Silver/Gold Gauntlets
    //   scale:      none/Silver/Golden
    //   wallet:     99/200/500/(500 vanilla, 999 Tycoon in OoTR)
    //   bulletBag:  0/30/40/50 seeds
    //   dekuSticks: 0/10/20/30 capacity
    //   dekuNuts:   0/20/30/40 capacity
    std::uint8_t quiver     = 0;
    std::uint8_t bombBag    = 0;
    std::uint8_t strength   = 0;
    std::uint8_t scale      = 0;
    std::uint8_t wallet     = 0;
    std::uint8_t bulletBag  = 0;
    std::uint8_t dekuSticks = 0;
    std::uint8_t dekuNuts   = 0;

    // Quest items — one bool per QuestItem enum value (medallions, songs,
    // spiritual stones, plus Stone of Agony and Gerudo's Card).
    bool medallionForest = false, medallionFire    = false, medallionWater = false;
    bool medallionSpirit = false, medallionShadow  = false, medallionLight = false;
    bool songMinuet      = false, songBolero       = false, songSerenade   = false;
    bool songRequiem     = false, songNocturne     = false, songPrelude    = false;
    bool songLullaby     = false, songEpona        = false, songSaria      = false;
    bool songSun         = false, songTime         = false, songStorms     = false;
    bool kokiriEmerald   = false, goronRuby        = false, zoraSapphire   = false;
    bool stoneOfAgony    = false, gerudosCard      = false;

    // Gold-skulltula token count maintained by the game (sum across all
    // gsFlags bitmasks). Capped at 100 in vanilla.
    std::int16_t gsTokens = 0;

    // Per-dungeon item bitmasks (boss key/compass/map), one byte per
    // scene id. See item.h:DungeonItem for bit positions.
    std::array<std::uint8_t, 20> dungeonItems{};

    // Per-dungeon small-key counts (signed because the game uses -1 as
    // "no keys exist for this dungeon" in some contexts).
    std::array<std::int8_t, 19> dungeonKeys{};
};

// Decode the Inventory struct from a SaveContext slice. Caller must have
// already verified the newf magic (PlayerData::saveLoaded).
Inventory readInventory(std::span<const std::uint8_t> saveContext);

// Short labels for the right pane. Return "-" for tier 0 / unowned so
// the caller can format columns uniformly. swordLabel reports the
// highest-tier owned sword; similarly for shield/tunic/boots.
std::string_view swordLabel   (const Inventory&);
std::string_view shieldLabel  (const Inventory&);
std::string_view tunicLabel   (const Inventory&);
std::string_view bootsLabel   (const Inventory&);
std::string_view walletLabel  (const Inventory&);
std::string_view strengthLabel(const Inventory&);
std::string_view scaleLabel   (const Inventory&);

}  // namespace tpt::game::oot::save
