#include "game/oot/save/Inventory.h"

#include <cstring>

#include "game/oot/save/SaveOffsets.h"

namespace tpt::game::oot::save {

namespace {

inline std::uint16_t rdU16(std::span<const std::uint8_t> b, std::uint32_t o) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(b[o]) << 8) | b[o + 1]);
}
inline std::int16_t rdS16(std::span<const std::uint8_t> b, std::uint32_t o) {
    return static_cast<std::int16_t>(rdU16(b, o));
}
inline std::uint32_t rdU32(std::span<const std::uint8_t> b, std::uint32_t o) {
    return (static_cast<std::uint32_t>(b[o])     << 24) |
           (static_cast<std::uint32_t>(b[o + 1]) << 16) |
           (static_cast<std::uint32_t>(b[o + 2]) << 8)  |
            static_cast<std::uint32_t>(b[o + 3]);
}

inline std::uint8_t extractField(std::uint32_t value, UpgradeField f) {
    return static_cast<std::uint8_t>(
        (value >> f.shift) & ((1u << f.width) - 1u));
}

}  // namespace

Inventory readInventory(std::span<const std::uint8_t> sc) {
    Inventory inv;
    if (sc.size() < kOffInventoryGsTokens + 2) return inv;

    std::memcpy(inv.items.data(), &sc[kOffInventoryItems], inv.items.size());
    std::memcpy(inv.ammo.data(),  &sc[kOffInventoryAmmo],  inv.ammo.size());

    // Equipment ownership: u16, four nibbles. Within each nibble, bit N
    // = (1 << EquipInv*) — bit 0 of the sword nibble = Kokiri, bit 1 =
    // Master, etc. See item.h:EquipInvSword / EquipInvShield / …
    const std::uint16_t eq = rdU16(sc, kOffInventoryEquipment);
    const auto sword  = (eq >> kEquipNibbleSword)  & 0xFu;
    const auto shield = (eq >> kEquipNibbleShield) & 0xFu;
    const auto tunic  = (eq >> kEquipNibbleTunic)  & 0xFu;
    const auto boots  = (eq >> kEquipNibbleBoots)  & 0xFu;
    inv.swordKokiri      = (sword  & 0x1u) != 0;
    inv.swordMaster      = (sword  & 0x2u) != 0;
    inv.swordBiggoron    = (sword  & 0x4u) != 0;
    inv.swordBrokenGiant = (sword  & 0x8u) != 0;
    inv.shieldDeku       = (shield & 0x1u) != 0;
    inv.shieldHylian     = (shield & 0x2u) != 0;
    inv.shieldMirror     = (shield & 0x4u) != 0;
    inv.tunicKokiri      = (tunic  & 0x1u) != 0;
    inv.tunicGoron       = (tunic  & 0x2u) != 0;
    inv.tunicZora        = (tunic  & 0x4u) != 0;
    inv.bootsKokiri      = (boots  & 0x1u) != 0;
    inv.bootsIron        = (boots  & 0x2u) != 0;
    inv.bootsHover       = (boots  & 0x4u) != 0;

    // Upgrades: u32, per-type bitfield. Widths come from gUpgradeMasks
    // — all 3 bits except wallet which is 2 bits.
    const std::uint32_t up = rdU32(sc, kOffInventoryUpgrades);
    inv.quiver     = extractField(up, kUpgQuiver);
    inv.bombBag    = extractField(up, kUpgBombBag);
    inv.strength   = extractField(up, kUpgStrength);
    inv.scale      = extractField(up, kUpgScale);
    inv.wallet     = extractField(up, kUpgWallet);
    inv.bulletBag  = extractField(up, kUpgBulletBag);
    inv.dekuSticks = extractField(up, kUpgDekuSticks);
    inv.dekuNuts   = extractField(up, kUpgDekuNuts);

    // Quest items: u32, one bit per QuestItem enum value (0..22).
    const std::uint32_t q = rdU32(sc, kOffInventoryQuestItems);
    const auto bit = [&](std::uint32_t i) { return (q & (1u << i)) != 0; };
    inv.medallionForest = bit(0);
    inv.medallionFire   = bit(1);
    inv.medallionWater  = bit(2);
    inv.medallionSpirit = bit(3);
    inv.medallionShadow = bit(4);
    inv.medallionLight  = bit(5);
    inv.songMinuet      = bit(6);
    inv.songBolero      = bit(7);
    inv.songSerenade    = bit(8);
    inv.songRequiem     = bit(9);
    inv.songNocturne    = bit(10);
    inv.songPrelude     = bit(11);
    inv.songLullaby     = bit(12);
    inv.songEpona       = bit(13);
    inv.songSaria       = bit(14);
    inv.songSun         = bit(15);
    inv.songTime        = bit(16);
    inv.songStorms      = bit(17);
    inv.kokiriEmerald   = bit(18);
    inv.goronRuby       = bit(19);
    inv.zoraSapphire    = bit(20);
    inv.stoneOfAgony    = bit(21);
    inv.gerudosCard     = bit(22);

    inv.gsTokens = rdS16(sc, kOffInventoryGsTokens);

    // Per-dungeon arrays. dungeonItems[20] holds a bitmask per scene
    // (boss key / compass / map bits); dungeonKeys[19] holds signed
    // small-key counts.
    std::memcpy(inv.dungeonItems.data(), &sc[kOffInventoryDungeonItems],
                inv.dungeonItems.size());
    std::memcpy(inv.dungeonKeys.data(),  &sc[kOffInventoryDungeonKeys],
                inv.dungeonKeys.size());
    return inv;
}

std::string_view swordLabel(const Inventory& i) {
    if (i.swordBiggoron) return "Biggoron";
    if (i.swordMaster)   return "Master";
    if (i.swordKokiri)   return "Kokiri";
    return "-";
}
std::string_view shieldLabel(const Inventory& i) {
    if (i.shieldMirror) return "Mirror";
    if (i.shieldHylian) return "Hylian";
    if (i.shieldDeku)   return "Deku";
    return "-";
}
std::string_view tunicLabel(const Inventory& i) {
    // Highest-tier owned tunic. Active tunic is in ItemEquips.equipment
    // (not yet decoded); when that lands, this becomes "highest of {owned}
    // unless active equips says otherwise".
    if (i.tunicZora)   return "Zora";
    if (i.tunicGoron)  return "Goron";
    if (i.tunicKokiri) return "Kokiri";
    return "-";
}
std::string_view bootsLabel(const Inventory& i) {
    if (i.bootsHover)  return "Hover";
    if (i.bootsIron)   return "Iron";
    if (i.bootsKokiri) return "Kokiri";
    return "-";
}
std::string_view walletLabel(const Inventory& i) {
    switch (i.wallet) {
        case 1:  return "Adult (200)";
        case 2:  return "Giant (500)";
        case 3:  return "Tycoon (999)";  // OoTR-only; vanilla caps at tier 2
        default: return "Child (99)";
    }
}
std::string_view strengthLabel(const Inventory& i) {
    switch (i.strength) {
        case 1:  return "Goron Bracelet";
        case 2:  return "Silver Gauntlets";
        case 3:  return "Gold Gauntlets";
        default: return "-";
    }
}
std::string_view scaleLabel(const Inventory& i) {
    switch (i.scale) {
        case 1:  return "Silver";
        case 2:  return "Golden";
        default: return "-";
    }
}

}  // namespace tpt::game::oot::save
