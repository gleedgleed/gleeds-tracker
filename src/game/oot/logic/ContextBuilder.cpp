#include "game/oot/logic/ContextBuilder.h"

#include <string_view>

namespace tpt::game::oot::logic {

namespace {

// OoT InventorySlot constants — values mirror item.h:InventorySlot.
// Kept local because we only need a handful of slot-to-name mappings;
// importing the full decomp item.h isn't worth the indirection.
constexpr std::size_t kSlotDekuStick   = 0x00;
constexpr std::size_t kSlotDekuNut     = 0x01;
constexpr std::size_t kSlotBomb        = 0x02;
constexpr std::size_t kSlotBow         = 0x03;
constexpr std::size_t kSlotArrowFire   = 0x04;
constexpr std::size_t kSlotDinsFire    = 0x05;
constexpr std::size_t kSlotSlingshot   = 0x06;
constexpr std::size_t kSlotOcarina     = 0x07;
constexpr std::size_t kSlotBombchu     = 0x08;
constexpr std::size_t kSlotHookshot    = 0x09;
constexpr std::size_t kSlotArrowIce    = 0x0A;
constexpr std::size_t kSlotFaroresWind = 0x0B;
constexpr std::size_t kSlotBoomerang   = 0x0C;
constexpr std::size_t kSlotLens        = 0x0D;
constexpr std::size_t kSlotMagicBean   = 0x0E;
constexpr std::size_t kSlotHammer      = 0x0F;
constexpr std::size_t kSlotArrowLight  = 0x10;
constexpr std::size_t kSlotNayrusLove  = 0x11;
constexpr std::size_t kSlotBottle1     = 0x12;
constexpr std::size_t kSlotBottle4     = 0x15;
constexpr std::size_t kSlotTradeAdult  = 0x16;
constexpr std::size_t kSlotTradeChild  = 0x17;

constexpr std::uint8_t kItemHookshot     = 0x0A;
constexpr std::uint8_t kItemLongshot     = 0x0B;
constexpr std::uint8_t kItemOcarinaFairy = 0x07;
constexpr std::uint8_t kItemOcarinaOoT   = 0x08;
constexpr std::uint8_t kItemNone         = 0xFF;

// Dungeon indexes for dungeonKeys[] / dungeonItems[]. Scene IDs from
// oot-main z_scene_table; used by OoTR for `Small_Key_<Dungeon>` and
// `Boss_Key_<Dungeon>` item names.
constexpr std::size_t kDungeonDekuTree     = 0x00;
constexpr std::size_t kDungeonDodongos     = 0x01;
constexpr std::size_t kDungeonJabuJabus    = 0x02;
constexpr std::size_t kDungeonForestTemple = 0x03;
constexpr std::size_t kDungeonFireTemple   = 0x04;
constexpr std::size_t kDungeonWaterTemple  = 0x05;
constexpr std::size_t kDungeonSpiritTemple = 0x06;
constexpr std::size_t kDungeonShadowTemple = 0x07;
constexpr std::size_t kDungeonBotW         = 0x08;
constexpr std::size_t kDungeonIceCavern    = 0x09;
constexpr std::size_t kDungeonGanonTower   = 0x0A;
constexpr std::size_t kDungeonGTG          = 0x0B;
constexpr std::size_t kDungeonThieves      = 0x0C;
constexpr std::size_t kDungeonGanonCastle  = 0x0D;

// Boss/Map/Compass packed bits per dungeon (Inventory.dungeonItems[d]).
// Bit positions from item.h:DungeonItem.
constexpr std::uint8_t kDungeonBitBossKey  = 1 << 0;
constexpr std::uint8_t kDungeonBitCompass  = 1 << 1;
constexpr std::uint8_t kDungeonBitMap      = 1 << 2;

int bottleCount(const save::Inventory& inv) {
    // OoT slots 0x12..0x15 each hold a bottle item ID if a bottle's
    // present (any of ITEM_BOTTLE_* including filled bottles).
    int n = 0;
    for (std::size_t s = kSlotBottle1; s <= kSlotBottle4; ++s) {
        if (inv.items[s] != kItemNone) ++n;
    }
    return n;
}

int hookshotTier(const save::Inventory& inv) {
    switch (inv.items[kSlotHookshot]) {
        case kItemLongshot: return 2;
        case kItemHookshot: return 1;
        default:            return 0;
    }
}

int slotHasItem(const save::Inventory& inv, std::size_t slot,
                std::uint8_t expectedId) {
    return inv.items[slot] == expectedId ? 1 : 0;
}

// Map an OoTR item name to its count. Returns 0 when the item isn't
// owned or the name isn't recognised. The list is deliberately not
// exhaustive — covers the common items used by early-game logic. Items
// we don't yet map appear as 0, which means rules requiring them
// evaluate to false. Add entries as gaps surface in testing.
int lookupItem(std::string_view n,
               const save::PlayerData& pd,
               const save::Inventory& inv) {
    // ----- Swords / Shields / Tunics / Boots -----
    if (n == "Kokiri_Sword")    return inv.swordKokiri    ? 1 : 0;
    if (n == "Master_Sword")    return inv.swordMaster    ? 1 : 0;
    if (n == "Biggoron_Sword")  return pd.hasBiggoronSword ? 1 : 0;
    if (n == "Giants_Knife")    return inv.swordBiggoron   ? 1 : 0;
    if (n == "Deku_Shield")     return inv.shieldDeku     ? 1 : 0;
    if (n == "Hylian_Shield")   return inv.shieldHylian   ? 1 : 0;
    if (n == "Mirror_Shield")   return inv.shieldMirror   ? 1 : 0;
    if (n == "Goron_Tunic")     return inv.tunicGoron     ? 1 : 0;
    if (n == "Zora_Tunic")      return inv.tunicZora      ? 1 : 0;
    if (n == "Iron_Boots")      return inv.bootsIron      ? 1 : 0;
    if (n == "Hover_Boots")     return inv.bootsHover     ? 1 : 0;

    // ----- Progressive upgrades (tier as count) -----
    if (n == "Progressive_Hookshot")          return hookshotTier(inv);
    if (n == "Progressive_Strength_Upgrade")  return inv.strength;
    if (n == "Progressive_Scale")             return inv.scale;
    if (n == "Progressive_Wallet")            return inv.wallet;

    // ----- Single-flag items mapped from inventory slots -----
    if (n == "Bow")            return inv.quiver    > 0 ? 1 : 0;
    if (n == "Slingshot")      return inv.bulletBag > 0 ? 1 : 0;
    if (n == "Bomb_Bag")       return inv.bombBag   > 0 ? 1 : 0;
    if (n == "Bombchus" || n == "Bombchu")
        return slotHasItem(inv, kSlotBombchu, 0x09);
    if (n == "Boomerang")      return slotHasItem(inv, kSlotBoomerang, 0x0E);
    if (n == "Lens_of_Truth")  return slotHasItem(inv, kSlotLens,      0x0F);
    if (n == "Magic_Bean")     return slotHasItem(inv, kSlotMagicBean, 0x10);
    if (n == "Megaton_Hammer") return slotHasItem(inv, kSlotHammer,    0x11);
    if (n == "Dins_Fire")      return slotHasItem(inv, kSlotDinsFire,  0x05);
    if (n == "Farores_Wind")   return slotHasItem(inv, kSlotFaroresWind, 0x0D);
    if (n == "Nayrus_Love")    return slotHasItem(inv, kSlotNayrusLove, 0x13);
    if (n == "Fire_Arrows")    return slotHasItem(inv, kSlotArrowFire,  0x04);
    if (n == "Ice_Arrows" || n == "Blue_Fire_Arrows")
        return slotHasItem(inv, kSlotArrowIce, 0x0C);
    if (n == "Light_Arrows")   return slotHasItem(inv, kSlotArrowLight, 0x12);

    // ----- Ocarina -----
    if (n == "Ocarina") {
        const auto v = inv.items[kSlotOcarina];
        return (v == kItemOcarinaFairy || v == kItemOcarinaOoT) ? 1 : 0;
    }
    if (n == "Ocarina_of_Time")
        return slotHasItem(inv, kSlotOcarina, kItemOcarinaOoT);
    if (n == "Fairy_Ocarina")
        return slotHasItem(inv, kSlotOcarina, kItemOcarinaFairy);

    // ----- Magic / Defense -----
    if (n == "Magic_Meter")      return pd.isMagicAcquired         ? 1 : 0;
    if (n == "Double_Magic")     return pd.isDoubleMagicAcquired   ? 1 : 0;
    if (n == "Double_Defense")   return pd.isDoubleDefenseAcquired ? 1 : 0;

    // ----- Bottles -----
    if (n == "Bottle")           return bottleCount(inv) > 0 ? 1 : 0;
    if (n == "Bottle_with_Anything") return bottleCount(inv) > 0 ? 1 : 0;

    // ----- Songs -----
    if (n == "Zeldas_Lullaby")      return inv.songLullaby ? 1 : 0;
    if (n == "Eponas_Song")         return inv.songEpona   ? 1 : 0;
    if (n == "Sarias_Song")         return inv.songSaria   ? 1 : 0;
    if (n == "Suns_Song")           return inv.songSun     ? 1 : 0;
    if (n == "Song_of_Time")        return inv.songTime    ? 1 : 0;
    if (n == "Song_of_Storms")      return inv.songStorms  ? 1 : 0;
    if (n == "Minuet_of_Forest")    return inv.songMinuet    ? 1 : 0;
    if (n == "Bolero_of_Fire")      return inv.songBolero    ? 1 : 0;
    if (n == "Serenade_of_Water")   return inv.songSerenade  ? 1 : 0;
    if (n == "Requiem_of_Spirit")   return inv.songRequiem   ? 1 : 0;
    if (n == "Nocturne_of_Shadow")  return inv.songNocturne  ? 1 : 0;
    if (n == "Prelude_of_Light")    return inv.songPrelude   ? 1 : 0;

    // ----- Stones + Medallions + Misc quest items -----
    if (n == "Kokiri_Emerald")      return inv.kokiriEmerald  ? 1 : 0;
    if (n == "Goron_Ruby")          return inv.goronRuby      ? 1 : 0;
    if (n == "Zora_Sapphire")       return inv.zoraSapphire   ? 1 : 0;
    if (n == "Forest_Medallion")    return inv.medallionForest ? 1 : 0;
    if (n == "Fire_Medallion")      return inv.medallionFire   ? 1 : 0;
    if (n == "Water_Medallion")     return inv.medallionWater  ? 1 : 0;
    if (n == "Spirit_Medallion")    return inv.medallionSpirit ? 1 : 0;
    if (n == "Shadow_Medallion")    return inv.medallionShadow ? 1 : 0;
    if (n == "Light_Medallion")     return inv.medallionLight  ? 1 : 0;
    if (n == "Stone_of_Agony")      return inv.stoneOfAgony    ? 1 : 0;
    if (n == "Gerudo_Membership_Card" || n == "Gerudos_Card")
        return inv.gerudosCard ? 1 : 0;

    // ----- Gold skulltulas (counted) -----
    if (n == "Gold_Skulltula_Token") return inv.gsTokens;

    // ----- Per-dungeon items -----
    auto dungeonItemBit = [&](std::size_t dungeon, std::uint8_t bit) {
        return (inv.dungeonItems[dungeon] & bit) ? 1 : 0;
    };
    auto smallKeyCount = [&](std::size_t dungeon) -> int {
        const auto v = inv.dungeonKeys[dungeon];
        return v < 0 ? 0 : v;
    };

    // Boss keys.
    if (n == "Boss_Key_Forest_Temple") return dungeonItemBit(kDungeonForestTemple, kDungeonBitBossKey);
    if (n == "Boss_Key_Fire_Temple")   return dungeonItemBit(kDungeonFireTemple,   kDungeonBitBossKey);
    if (n == "Boss_Key_Water_Temple")  return dungeonItemBit(kDungeonWaterTemple,  kDungeonBitBossKey);
    if (n == "Boss_Key_Spirit_Temple") return dungeonItemBit(kDungeonSpiritTemple, kDungeonBitBossKey);
    if (n == "Boss_Key_Shadow_Temple") return dungeonItemBit(kDungeonShadowTemple, kDungeonBitBossKey);
    if (n == "Boss_Key_Ganons_Castle") return dungeonItemBit(kDungeonGanonCastle,  kDungeonBitBossKey);

    // Small keys.
    if (n == "Small_Key_Forest_Temple")         return smallKeyCount(kDungeonForestTemple);
    if (n == "Small_Key_Fire_Temple")           return smallKeyCount(kDungeonFireTemple);
    if (n == "Small_Key_Water_Temple")          return smallKeyCount(kDungeonWaterTemple);
    if (n == "Small_Key_Spirit_Temple")         return smallKeyCount(kDungeonSpiritTemple);
    if (n == "Small_Key_Shadow_Temple")         return smallKeyCount(kDungeonShadowTemple);
    if (n == "Small_Key_Bottom_of_the_Well")    return smallKeyCount(kDungeonBotW);
    if (n == "Small_Key_Gerudo_Training_Ground")return smallKeyCount(kDungeonGTG);
    if (n == "Small_Key_Thieves_Hideout" ||
        n == "Small_Key_Gerudos_Fortress")      return smallKeyCount(kDungeonThieves);
    if (n == "Small_Key_Ganons_Castle")         return smallKeyCount(kDungeonGanonCastle);

    return 0;
}

}  // namespace

Context buildContext(const save::PlayerData& pd,
                     const save::Inventory&  inv,
                     const save::SaveFlags&  /*flags*/) {
    Context ctx;
    ctx.isAdult = pd.isAdult;
    // Permissive=false: unknown settings comparisons resolve to false
    // (rule fails). Underestimates reach when the user's seed actually
    // opens a setting we don't know about, but that's the safer
    // default — overestimating gives false-positive reachables which
    // confuses tracking. The opposite trade-off can be revisited once
    // settings-string parsing lands.
    ctx.permissive = false;

    // Define `is_adult` / `is_child` as binary-item presence too, so
    // rules can write them as Idents. The aliases in LogicHelpers
    // already redirect to `age == 'adult'` / `'child'`, but having the
    // direct names cuts one indirection.
    ctx.items["is_adult"] = pd.isAdult ? 1 : 0;
    ctx.items["is_child"] = pd.isAdult ? 0 : 1;

    // Mass-populate items from our lookup table. Iterating known names
    // here means `ctx.items` doesn't end up with thousands of entries
    // — the table is the source of truth, and evaluator fall-through
    // handles unknowns. We do skip items that lookup() returns 0 for
    // so the map stays small.
    static const std::string_view kKnownItems[] = {
        "Kokiri_Sword", "Master_Sword", "Biggoron_Sword", "Giants_Knife",
        "Deku_Shield", "Hylian_Shield", "Mirror_Shield",
        "Goron_Tunic", "Zora_Tunic",
        "Iron_Boots", "Hover_Boots",
        "Progressive_Hookshot", "Progressive_Strength_Upgrade",
        "Progressive_Scale", "Progressive_Wallet",
        "Bow", "Slingshot", "Bomb_Bag", "Bombchu",
        "Boomerang", "Lens_of_Truth", "Magic_Bean", "Megaton_Hammer",
        "Dins_Fire", "Farores_Wind", "Nayrus_Love",
        "Fire_Arrows", "Ice_Arrows", "Light_Arrows",
        "Ocarina", "Ocarina_of_Time", "Fairy_Ocarina",
        "Magic_Meter", "Double_Magic", "Double_Defense",
        "Bottle",
        "Zeldas_Lullaby", "Eponas_Song", "Sarias_Song",
        "Suns_Song", "Song_of_Time", "Song_of_Storms",
        "Minuet_of_Forest", "Bolero_of_Fire", "Serenade_of_Water",
        "Requiem_of_Spirit", "Nocturne_of_Shadow", "Prelude_of_Light",
        "Kokiri_Emerald", "Goron_Ruby", "Zora_Sapphire",
        "Forest_Medallion", "Fire_Medallion", "Water_Medallion",
        "Spirit_Medallion", "Shadow_Medallion", "Light_Medallion",
        "Stone_of_Agony", "Gerudo_Membership_Card",
        "Gold_Skulltula_Token",
        "Boss_Key_Forest_Temple", "Boss_Key_Fire_Temple",
        "Boss_Key_Water_Temple", "Boss_Key_Spirit_Temple",
        "Boss_Key_Shadow_Temple", "Boss_Key_Ganons_Castle",
        "Small_Key_Forest_Temple", "Small_Key_Fire_Temple",
        "Small_Key_Water_Temple", "Small_Key_Spirit_Temple",
        "Small_Key_Shadow_Temple", "Small_Key_Bottom_of_the_Well",
        "Small_Key_Gerudo_Training_Ground", "Small_Key_Thieves_Hideout",
        "Small_Key_Ganons_Castle",
    };
    for (auto name : kKnownItems) {
        const int n = lookupItem(name, pd, inv);
        if (n > 0) ctx.items[std::string(name)] = n;
    }

    // Settings — OoTR strict defaults. Without seed-string parsing
    // we can't know the user's actual seed, so we model the most-
    // closed configuration (matches `closed_forest`, `closed_kakariko`,
    // `closed_door_of_time`, etc.). Reach will underestimate when the
    // seed opens any of these; the right fix is settings-string
    // parsing. The values below are the OoTR generator defaults from
    // SettingsList.py.
    ctx.settings["starting_age"]                  = ctx.isAdult ? "adult" : "child";
    ctx.settings["age"]                           = ctx.isAdult ? "adult" : "child";
    ctx.settings["open_forest"]                   = "closed";
    ctx.settings["open_kakariko"]                 = "closed";
    ctx.settings["open_door_of_time"]             = "false";
    ctx.settings["zora_fountain"]                 = "closed";
    ctx.settings["gerudo_fortress"]               = "normal";
    ctx.settings["bridge"]                        = "medallions";
    ctx.settings["lacs_condition"]                = "vanilla";
    ctx.settings["damage_multiplier"]             = "normal";
    ctx.settings["shuffle_individual_ocarina_notes"] = "false";
    ctx.settings["shuffle_song_items"]            = "song";
    ctx.settings["plant_beans"]                   = "false";
    ctx.settings["free_bombchu_drops"]            = "false";
    ctx.settings["chicken_count"]                 = "7";
    ctx.settings["big_poe_count"]                 = "10";
    ctx.settings["correct_chest_appearances"]     = "off";
    ctx.settings["entrance_shuffle"]              = "off";
    ctx.settings["shuffle_overworld_entrances"]   = "false";
    ctx.settings["shuffle_hideout_entrances"]     = "false";
    ctx.settings["shuffle_gerudo_fortress_heart_piece"]   = "vanilla";
    ctx.settings["skip_child_zelda"]              = "false";
    ctx.settings["skip_reward_from_rauru"]        = "not_free";
    ctx.settings["triforce_hunt"]                 = "false";
    ctx.settings["adult_trade_shuffle"]           = "false";
    ctx.settings["shuffle_song_links"]            = "off";

    return ctx;
}

}  // namespace tpt::game::oot::logic
