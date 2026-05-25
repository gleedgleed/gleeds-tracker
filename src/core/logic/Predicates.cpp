#include "core/logic/Predicates.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <string>

namespace tpt::core::logic {

namespace {

// ----- inventory accessors ---------------------------------------------------

int count(const Context& c, const char* name) {
    const auto it = c.items.find(name);
    return it == c.items.end() ? 0 : it->second;
}

bool has(const Context& c, const char* name) {
    return count(c, name) > 0;
}

bool hasAll(const Context& c, std::initializer_list<const char*> names) {
    for (const auto* n : names) if (!has(c, n)) return false;
    return true;
}

bool hasAny(const Context& c, std::initializer_list<const char*> names) {
    for (const auto* n : names) if (has(c, n)) return true;
    return false;
}

// Look up another predicate by name and call it. Returns false if the
// predicate isn't registered (mirrors Python's "unknown -> False").
bool call(const Context& c, const char* name) {
    const auto it = c.predicates.find(name);
    if (it == c.predicates.end()) return false;
    try { return it->second(c); }
    catch (...) { return false; }
}

bool flag(const Context& c, const char* name) {
    const auto it = c.eventFlags.find(name);
    return it != c.eventFlags.end() && it->second;
}

const std::string* setting(const Context& c, const char* name) {
    const auto it = c.settings.find(name);
    return it == c.settings.end() ? nullptr : &it->second;
}

bool settingEq(const Context& c, const char* name, const char* value) {
    const auto* s = setting(c, name);
    return s != nullptr && *s == value;
}

int settingInt(const Context& c, const char* name, int fallback) {
    const auto* s = setting(c, name);
    if (!s) return fallback;
    try { return std::stoi(*s); }
    catch (...) { return fallback; }
}

// ----- core item helpers (mirror logic.py local lambdas) ---------------------

bool niche(const Context& c)     { return c.glitched; }
bool difficultCombat(const Context&) { return false; }
bool isWolf(const Context& c)    { return has(c, "Shadow_Crystal"); }
bool hasSword(const Context& c)  { return count(c, "Progressive_Sword") >= 1; }
bool hasBombs(const Context& c)  { return count(c, "Bomb_Bag") > 0; }
bool hasLantern(const Context& c){ return has(c, "Lantern"); }

bool backsliceAsSword(const Context& c) {
    return c.glitched && count(c, "Progressive_Hidden_Skill") >= 3;
}

bool hasBottle(const Context& c) {
    const bool anyBottle = count(c, "Empty_Bottle") >= 1
        || has(c, "Sera_Bottle") || has(c, "Jovani_Bottle") || has(c, "Coro_Bottle");
    return anyBottle && has(c, "Lantern");
}

bool hasBottles(const Context& c) {
    if (!has(c, "Lantern")) return false;
    int n = count(c, "Empty_Bottle");
    if (has(c, "Sera_Bottle"))   ++n;
    if (has(c, "Jovani_Bottle")) ++n;
    if (has(c, "Coro_Bottle"))   ++n;
    return n > 1;
}

bool hasRanged(const Context& c) {
    return has(c, "Ball_and_Chain") || has(c, "Slingshot")
        || count(c, "Progressive_Bow") >= 1
        || count(c, "Progressive_Clawshot") >= 1
        || has(c, "Boomerang");
}

bool hasDamagingItem(const Context& c) {
    return hasSword(c) || has(c, "Ball_and_Chain")
        || count(c, "Progressive_Bow") >= 1
        || hasBombs(c) || has(c, "Iron_Boots")
        || has(c, "Shadow_Crystal") || has(c, "Spinner");
}

bool hasHeavyMod(const Context& c) {
    return has(c, "Iron_Boots") || has(c, "Magic_Armor");
}

// HasShield: Hylian_Shield, OR reach a shop that sells it (only if shops
// aren't shuffled, since shuffled shops may not have one).
bool hasShield(const Context& c) {
    if (has(c, "Hylian_Shield")) return true;
    const bool shopUnshuffled = settingEq(c, "shuffleShopItems", "false");
    static const std::array<const char*, 3> kShops{
        "Kakariko Malo Mart", "Castle Town Goron House", "Death Mountain Hot Spring"};
    if (shopUnshuffled) {
        for (const auto* room : kShops) {
            if (c.reachedRooms.count(room)) return true;
        }
    } else if (c.reachedRooms.count("Death Mountain Hot Spring")) {
        return true;
    }
    return false;
}

// Generic enemy-defeat template.
bool genericEnemy(const Context& c) {
    return hasSword(c) || has(c, "Ball_and_Chain")
        || count(c, "Progressive_Bow") >= 1
        || (niche(c) && has(c, "Iron_Boots"))
        || has(c, "Spinner") || has(c, "Shadow_Crystal")
        || hasBombs(c) || backsliceAsSword(c);
}

// ----- specific predicates ---------------------------------------------------

bool canSmash(const Context& c) {
    return has(c, "Ball_and_Chain") || hasBombs(c);
}

bool canBurnWebs(const Context& c) {
    return hasLantern(c) || hasBombs(c) || has(c, "Ball_and_Chain");
}

bool canCutHangingWeb(const Context& c) {
    return count(c, "Progressive_Clawshot") >= 1
        || count(c, "Progressive_Bow") >= 1
        || has(c, "Boomerang") || has(c, "Ball_and_Chain");
}

bool canBreakWoodenDoor(const Context& c) {
    return has(c, "Shadow_Crystal") || hasSword(c) || canSmash(c) || backsliceAsSword(c);
}

bool canLaunchBombs(const Context& c) {
    return (has(c, "Boomerang") || count(c, "Progressive_Bow") >= 1) && hasBombs(c);
}

bool canKnockDownHangingBaba(const Context& c) {
    return count(c, "Progressive_Bow") >= 1
        || count(c, "Progressive_Clawshot") >= 1
        || has(c, "Boomerang") || has(c, "Slingshot");
}

bool canKnockDownHCPainting(const Context& c) {
    return count(c, "Progressive_Bow") >= 1
        || (niche(c) && (hasBombs(c)
                || (hasSword(c) && count(c, "Progressive_Hidden_Skill") >= 6)));
}

bool canBuyMagicArmor(const Context& c) {
    const auto* ws = setting(c, "walletSize");
    if (ws && *ws == "Large")    return true;
    if (ws && *ws == "Reduced")  return count(c, "Progressive_Wallet") >= 2;
    return count(c, "Progressive_Wallet") >= 1;
}

bool canStrikePedestal(const Context& c) {
    const auto* tier = setting(c, "totEntranceTier");
    if (!tier) return count(c, "Progressive_Sword") >= 1;
    int need = 1;
    try { need = std::stoi(*tier); } catch (...) { need = 1; }
    if (need == 0) return true;
    return count(c, "Progressive_Sword") >= need;
}

// Castle barrier requirements. Returns number of bosses currently defeated.
int dungeonsCleared(const Context& c) {
    static const std::array<const char*, 8> kBosses{
        "Diababa_Defeated", "Fyrus_Defeated", "Morpheel_Defeated",
        "Stallord_Defeated", "Blizzeta_Defeated", "Armogohma_Defeated",
        "Argorok_Defeated", "Zant_Defeated"};
    int n = 0;
    for (const auto* b : kBosses) if (has(c, b)) ++n;
    return n;
}

bool canBreakHCBarrier(const Context& c) {
    const auto* req = setting(c, "castleRequirements");
    const int countN = settingInt(c, "castleRequirementCount", 0);
    if (!req || *req == "Open")      return true;
    if (*req == "Vanilla")           return call(c, "CanCompletePalaceofTwilight");
    if (*req == "Fused_Shadows")     return count(c, "Progressive_Fused_Shadow") >= countN;
    if (*req == "Mirror_Shards")     return count(c, "Progressive_Mirror_Shard") >= countN;
    if (*req == "Dungeons")          return dungeonsCleared(c) >= countN;
    if (*req == "Poe_Souls")         return count(c, "Poe_Soul") >= countN;
    if (*req == "Hearts")            return true;  // hearts not modeled
    return true;
}

bool canOpenHCBKGate(const Context& c) {
    const auto* req = setting(c, "castleBKRequirements");
    const int countN = settingInt(c, "castleBKRequirementCount", 0);
    if (!req || *req == "None")      return true;
    if (*req == "Fused_Shadows")     return count(c, "Progressive_Fused_Shadow") >= countN;
    if (*req == "Mirror_Shards")     return count(c, "Progressive_Mirror_Shard") >= countN;
    if (*req == "Dungeons")          return dungeonsCleared(c) >= countN;
    if (*req == "Poe_Souls")         return count(c, "Poe_Soul") >= countN;
    return true;
}

bool canPressMinesSwitch(const Context& c) {
    return has(c, "Iron_Boots") || (niche(c) && has(c, "Ball_and_Chain"));
}

bool canSkipKeyToDekuToad(const Context& c) {
    return settingEq(c, "smallKeySettings", "Keysy")
        || count(c, "Progressive_Hidden_Skill") >= 3
        || (hasBombs(c) && (hasHeavyMod(c) || count(c, "Progressive_Hidden_Skill") >= 6));
}

bool hasCutsceneItem(const Context& c) {
    return count(c, "Progressive_Sky_Book") >= 1 || hasBottle(c) || has(c, "Horse_Call");
}

// ----- specific enemy / boss predicates --------------------------------------

bool canDefeatStalfos(const Context& c) { return canSmash(c); }
bool canDefeatFreezard(const Context& c){ return has(c, "Ball_and_Chain"); }
bool canDefeatGhoulRat(const Context& c){ return has(c, "Shadow_Crystal"); }
bool canDefeatPoe(const Context& c)     { return has(c, "Shadow_Crystal"); }
bool canDefeatShadowInsect(const Context& c)    { return has(c, "Shadow_Crystal"); }
bool canDefeatCarrierKargarok(const Context& c) { return has(c, "Shadow_Crystal"); }
bool canDefeatTwilitBloat(const Context& c)     { return has(c, "Shadow_Crystal"); }
bool canDefeatShadowBeast(const Context& c) {
    return hasSword(c) || (has(c, "Shadow_Crystal") && call(c, "CanMidnaCharge"));
}
bool canDefeatSkullKid(const Context& c)        { return count(c, "Progressive_Bow") >= 1; }
bool canDefeatKingBulblinBridge(const Context& c){ return count(c, "Progressive_Bow") >= 1; }
bool canDefeatDinalfos(const Context& c) {
    return hasSword(c) || has(c, "Ball_and_Chain") || has(c, "Shadow_Crystal");
}
bool canDefeatDekuLike(const Context& c){ return hasBombs(c); }
bool canDefeatBeamos(const Context& c) {
    return has(c, "Ball_and_Chain") || count(c, "Progressive_Bow") >= 1 || hasBombs(c);
}
bool canDefeatDarknut(const Context& c) {
    return hasSword(c) || (difficultCombat(c) && (hasBombs(c) || has(c, "Ball_and_Chain")));
}
bool canDefeatBari(const Context& c) {
    return call(c, "CanUseWaterBombs") || count(c, "Progressive_Clawshot") >= 1;
}
bool canDefeatHangingBabaSerpent(const Context& c) {
    return (has(c, "Boomerang") || count(c, "Progressive_Bow") >= 1) && genericEnemy(c);
}
bool canDefeatLeever(const Context& c) {
    return hasSword(c) || has(c, "Ball_and_Chain") || count(c, "Progressive_Bow") >= 1
        || (niche(c) && has(c, "Iron_Boots")) || has(c, "Spinner")
        || has(c, "Shadow_Crystal") || hasBombs(c);
}
bool canDefeatPoisonMite(const Context& c) {
    return hasSword(c) || has(c, "Ball_and_Chain") || count(c, "Progressive_Bow") >= 1
        || has(c, "Lantern") || has(c, "Spinner") || has(c, "Shadow_Crystal");
}
bool canDefeatShellBlade(const Context& c) {
    return call(c, "CanUseWaterBombs")
        || (hasSword(c) && (has(c, "Iron_Boots") || (niche(c) && has(c, "Magic_Armor"))));
}
bool canDefeatSkullfish(const Context& c) {
    return hasSword(c) || has(c, "Ball_and_Chain") || count(c, "Progressive_Bow") >= 1
        || (niche(c) && has(c, "Iron_Boots")) || has(c, "Spinner") || has(c, "Shadow_Crystal");
}
bool canDefeatTileWorm(const Context& c) {
    return genericEnemy(c) && has(c, "Boomerang");
}
bool canDefeatToado(const Context& c) {
    return hasSword(c) || has(c, "Ball_and_Chain") || count(c, "Progressive_Bow") >= 1
        || has(c, "Spinner") || has(c, "Shadow_Crystal");
}
bool canDefeatWaterToadpoli(const Context& c) {
    return hasSword(c) || has(c, "Ball_and_Chain") || count(c, "Progressive_Bow") >= 1
        || (hasShield(c) && count(c, "Progressive_Hidden_Skill") >= 2)
        || (difficultCombat(c) && has(c, "Shadow_Crystal"));
}
bool canDefeatFireToadpoli(const Context& c) {
    return hasSword(c) || has(c, "Ball_and_Chain") || count(c, "Progressive_Bow") >= 1
        || (has(c, "Hylian_Shield") && count(c, "Progressive_Hidden_Skill") >= 2)
        || (difficultCombat(c) && has(c, "Shadow_Crystal"));
}
bool canDefeatChuWorm(const Context& c) {
    return genericEnemy(c) && (hasBombs(c) || count(c, "Progressive_Clawshot") >= 1);
}
bool canDefeatBombfish(const Context& c) {
    const bool stand = has(c, "Iron_Boots") || (c.glitched && has(c, "Magic_Armor"));
    return stand && (hasSword(c) || count(c, "Progressive_Clawshot") >= 1
                     || (hasShield(c) && count(c, "Progressive_Hidden_Skill") >= 2));
}
bool canDefeatGoron(const Context& c) {
    return hasSword(c) || has(c, "Ball_and_Chain") || count(c, "Progressive_Bow") >= 1
        || (niche(c) && has(c, "Iron_Boots")) || has(c, "Spinner")
        || (hasShield(c) && count(c, "Progressive_Hidden_Skill") >= 2)
        || has(c, "Slingshot") || (difficultCombat(c) && has(c, "Lantern"))
        || count(c, "Progressive_Clawshot") >= 1 || hasBombs(c) || backsliceAsSword(c);
}
bool canDefeatGuay(const Context& c) {
    return hasSword(c) || has(c, "Ball_and_Chain") || count(c, "Progressive_Bow") >= 1
        || (niche(c) && has(c, "Iron_Boots"))
        || (difficultCombat(c) && has(c, "Spinner"))
        || has(c, "Shadow_Crystal") || has(c, "Slingshot");
}
bool canDefeatWalltula(const Context& c) {
    return has(c, "Ball_and_Chain") || has(c, "Slingshot")
        || count(c, "Progressive_Bow") >= 1 || has(c, "Boomerang")
        || count(c, "Progressive_Clawshot") >= 1;
}
bool canDefeatAeralfos(const Context& c) {
    return count(c, "Progressive_Clawshot") >= 1
        && (hasSword(c) || has(c, "Ball_and_Chain") || has(c, "Shadow_Crystal")
            || (niche(c) && has(c, "Iron_Boots")));
}
bool canDefeatDangoro(const Context& c) {
    if (!has(c, "Iron_Boots")) return false;
    return hasSword(c) || has(c, "Shadow_Crystal")
        || (niche(c) && has(c, "Ball_and_Chain"))
        || (count(c, "Progressive_Bow") >= 1 && hasBombs(c));
}
bool canDefeatDeathSword(const Context& c) {
    return hasSword(c)
        && (has(c, "Boomerang") || count(c, "Progressive_Bow") >= 1
            || count(c, "Progressive_Clawshot") >= 1)
        && has(c, "Shadow_Crystal");
}
bool canDefeatDarkhammer(const Context& c) {
    return hasSword(c) || has(c, "Ball_and_Chain") || count(c, "Progressive_Bow") >= 1
        || (niche(c) && has(c, "Iron_Boots")) || has(c, "Shadow_Crystal") || hasBombs(c)
        || (difficultCombat(c) && backsliceAsSword(c));
}
bool canDefeatPhantomZant(const Context& c) {
    return has(c, "Shadow_Crystal") || hasSword(c);
}
bool canDefeatZantHead(const Context& c) {
    return has(c, "Shadow_Crystal") || hasSword(c) || backsliceAsSword(c);
}
bool canDefeatKingBulblinDesert(const Context& c) {
    return hasSword(c) || has(c, "Ball_and_Chain") || has(c, "Shadow_Crystal")
        || count(c, "Progressive_Bow") > 2 || backsliceAsSword(c)
        || (difficultCombat(c) && (
            has(c, "Spinner") || has(c, "Iron_Boots") || hasBombs(c)
            || count(c, "Progressive_Bow") >= 2));
}
bool canDefeatKingBulblinCastle(const Context& c) {
    return hasSword(c) || has(c, "Ball_and_Chain") || has(c, "Shadow_Crystal")
        || count(c, "Progressive_Bow") > 2
        || (difficultCombat(c) && (has(c, "Spinner") || has(c, "Iron_Boots")
                                   || hasBombs(c) || backsliceAsSword(c)));
}

// ----- bosses ----------------------------------------------------------------

bool canDefeatDiababa(const Context& c) {
    return canLaunchBombs(c) || (has(c, "Boomerang") && (
        hasSword(c) || has(c, "Ball_and_Chain")
        || (niche(c) && has(c, "Iron_Boots")) || has(c, "Shadow_Crystal")
        || hasBombs(c) || (difficultCombat(c) && backsliceAsSword(c))));
}
bool canDefeatFyrus(const Context& c) {
    return count(c, "Progressive_Bow") >= 1 && has(c, "Iron_Boots")
        && (hasSword(c) || (difficultCombat(c) && backsliceAsSword(c)));
}
bool canDefeatMorpheel(const Context& c) {
    return (has(c, "Zora_Armor") && has(c, "Iron_Boots") && hasSword(c)
            && count(c, "Progressive_Clawshot") >= 1)
        || (niche(c) && count(c, "Progressive_Clawshot") >= 1
            && call(c, "CanDoAirRefill") && hasSword(c));
}
bool canDefeatStallord(const Context& c) {
    return (has(c, "Spinner") && hasSword(c)) || (difficultCombat(c) && has(c, "Spinner"));
}
bool canDefeatBlizzeta(const Context& c) { return has(c, "Ball_and_Chain"); }
bool canDefeatArmogohma(const Context& c) {
    return count(c, "Progressive_Bow") >= 1 && count(c, "Progressive_Dominion_Rod") >= 1;
}
bool canDefeatArgorok(const Context& c) {
    return count(c, "Progressive_Clawshot") >= 2
        && count(c, "Progressive_Sword") >= 2
        && (has(c, "Iron_Boots") || (niche(c) && has(c, "Magic_Armor")));
}
bool canDefeatZant(const Context& c) {
    return count(c, "Progressive_Sword") >= 3
        && has(c, "Boomerang") && count(c, "Progressive_Clawshot") >= 1
        && has(c, "Ball_and_Chain")
        && (has(c, "Iron_Boots") || (niche(c) && has(c, "Magic_Armor")))
        && (has(c, "Zora_Armor") || (c.glitched && call(c, "CanDoAirRefill")));
}
bool canDefeatGanondorf(const Context& c) {
    return has(c, "Shadow_Crystal")
        && count(c, "Progressive_Sword") >= 3
        && count(c, "Progressive_Hidden_Skill") >= 1;
}

// ----- twilight / story / dungeon clears -------------------------------------

// darkClearLevel is a BITMASK (see QuestState.h): 0x1 Faron, 0x2 Eldin,
// 0x4 Lanayru cleared, 0x8 MDH completed — NOT an ordinal level. The event
// flag is the primary signal; the bit is the fallback for seeds that pre-clear
// a province at file creation without setting the event flag.
bool canCompleteFaronTwilight(const Context& c) {
    return flag(c, "CLEARED_FARON_TWILIGHT") || (c.darkClearLevel & 0x1) != 0;
}
bool canCompleteEldinTwilight(const Context& c) {
    return flag(c, "CLEARED_ELDIN_TWILIGHT") || (c.darkClearLevel & 0x2) != 0;
}
bool canCompleteLanayruTwilight(const Context& c) {
    return flag(c, "CLEARED_LANAYRU_TWILIGHT") || (c.darkClearLevel & 0x4) != 0;
}
bool canCompleteAllTwilight(const Context& c) {
    return canCompleteFaronTwilight(c) && canCompleteEldinTwilight(c) && canCompleteLanayruTwilight(c);
}
bool canCompletePrologue(const Context& c) {
    if (flag(c, "FINISHED_SEWERS") || flag(c, "CLEARED_FARON_TWILIGHT")
        || c.darkClearLevel != 0) return true;  // any twilight progress ⇒ prologue done
    return c.reachedRooms.count("North Faron Woods") && call(c, "CanDefeatBokoblin");
}
bool canCompleteMDH(const Context& c) {
    return flag(c, "MIDNAS_DESPERATE_HOUR_COMPLETED")
        || (c.darkClearLevel & 0x8) != 0
        || settingEq(c, "skipMdh", "true")
        || call(c, "CanCompleteLakebedTemple");
}

bool canCompleteForestTemple(const Context& c)    { return has(c, "Diababa_Defeated"); }
bool canCompleteGoronMines(const Context& c)      { return has(c, "Fyrus_Defeated"); }
bool canCompleteLakebedTemple(const Context& c)   { return has(c, "Morpheel_Defeated"); }
bool canCompleteArbitersGrounds(const Context& c) { return has(c, "Stallord_Defeated"); }
bool canCompleteSnowpeakRuins(const Context& c)   { return has(c, "Blizzeta_Defeated"); }
bool canCompleteTempleofTime(const Context& c)    { return has(c, "Armogohma_Defeated"); }
bool canCompleteCityinTheSky(const Context& c)    { return has(c, "Argorok_Defeated"); }
bool canCompletePalaceofTwilight(const Context& c){ return has(c, "Zant_Defeated"); }
bool canCompleteHyruleCastle(const Context& c)    { return canDefeatGanondorf(c); }
bool canCompleteAllDungeons(const Context& c) {
    return canCompleteForestTemple(c) && canCompleteGoronMines(c)
        && canCompleteLakebedTemple(c) && canCompleteArbitersGrounds(c)
        && canCompleteSnowpeakRuins(c) && canCompleteTempleofTime(c)
        && canCompleteCityinTheSky(c) && canCompletePalaceofTwilight(c);
}

bool canClearForest(const Context& c) {
    return (canCompleteForestTemple(c) || settingEq(c, "faronWoodsLogic", "Open"))
        && canCompletePrologue(c) && canCompleteFaronTwilight(c);
}

bool canCompleteGoats1(const Context& c) {
    return c.reachedRooms.count("Ordon Ranch") || canCompletePrologue(c);
}

bool canMidnaCharge(const Context& c) {
    return canCompleteMDH(c) && canCompleteAllTwilight(c);
}

// ----- monkeys / forest specifics --------------------------------------------

bool canBreakMonkeyCage(const Context& c) {
    return hasSword(c) || has(c, "Iron_Boots") || has(c, "Spinner")
        || has(c, "Ball_and_Chain") || has(c, "Shadow_Crystal") || hasBombs(c)
        || count(c, "Progressive_Bow") >= 1 || count(c, "Progressive_Clawshot") >= 1
        || (niche(c) && hasShield(c) && count(c, "Progressive_Hidden_Skill") >= 2);
}
bool canFreeAllMonkeys(const Context& c) {
    const bool keysy = settingEq(c, "smallKeySettings", "Keysy");
    const bool hasKeys = count(c, "Forest_Temple_Small_Key") >= 4 || keysy;
    const bool lanternOrKeysyBombs = has(c, "Lantern")
        || (keysy && (hasBombs(c) || has(c, "Iron_Boots")));
    return canBreakMonkeyCage(c) && lanternOrKeysyBombs && canBurnWebs(c)
        && has(c, "Boomerang") && call(c, "CanDefeatBokoblin") && hasKeys;
}

bool canWarpMeteor(const Context& c) {
    return flag(c, "CAN_NOW_WARP_METEOR") || c.darkClearLevel != 0;
}

}  // namespace

void registerPredicates(Context& ctx) {
    auto& p = ctx.predicates;
    p.clear();

    // Helper meta-predicates.
    p["CanDoNicheStuff"]        = niche;
    p["CanDoDifficultCombat"]   = difficultCombat;
    p["CanUseBacksliceAsSword"] = backsliceAsSword;
    p["HasSword"]        = hasSword;
    p["HasShield"]       = hasShield;
    p["HasBombs"]        = hasBombs;
    p["HasBottle"]       = hasBottle;
    p["HasBottles"]      = hasBottles;
    p["HasRangedItem"]   = hasRanged;
    p["HasDamagingItem"] = hasDamagingItem;
    p["HasHeavyMod"]     = hasHeavyMod;
    p["NoWrestling"]     = [](const Context&) { return true; };  // setting-gated; permissive

    // Simple ones.
    p["CanSmash"]      = canSmash;
    p["CanBurnWebs"]   = canBurnWebs;
    p["CanDestroyWebsWithoutLantern"] = [](const Context& c) {
        return hasBombs(c) || has(c, "Ball_and_Chain");
    };
    p["CanWarp"]       = isWolf;
    p["CanGetArrows"]  = [](const Context& c) { return count(c, "Progressive_Bow") >= 1; };
    p["CanRefillOil"]  = [](const Context& c) { return hasLantern(c); };
    p["CanCutHangingWeb"]   = canCutHangingWeb;
    p["CanBreakWoodenDoor"] = canBreakWoodenDoor;
    p["CanLaunchBombs"]     = canLaunchBombs;
    p["CanUseWaterBombs"]   = hasBombs;            // simplified
    p["CanGetHotSpringWater"] = hasBottle;
    p["CanBuyMagicArmor"]   = canBuyMagicArmor;
    p["CanUseBottledFairy"] = hasBottle;
    p["CanUseBottledFairies"] = hasBottles;
    p["CanUseOilBottle"]    = [](const Context& c) {
        return hasLantern(c) && has(c, "Coro_Bottle");
    };
    p["CanStrikePedestal"]  = canStrikePedestal;
    p["CanKnockDownHangingBaba"] = canKnockDownHangingBaba;
    p["CanKnockDownHCPainting"]  = canKnockDownHCPainting;
    p["CanBreakHCBarrier"]  = canBreakHCBarrier;
    p["CanOpenHCBKGate"]    = canOpenHCBKGate;
    p["CanPressMinesSwitch"]= canPressMinesSwitch;
    p["CanSkipKeyToDekuToad"] = canSkipKeyToDekuToad;
    p["HasCutsceneItem"]    = hasCutsceneItem;
    p["CanTransformIntoWolf"]= isWolf;

    // Glitches — all False (mirrors logic.py's stub list).
    for (const auto* g : {"CanDoLJA", "CanDoMoonBoots", "CanDoJSLJA", "CanDoJSMoonBoots",
                          "CanDoBSMoonBoots", "CanDoEBMoonBoots", "CanDoHSMoonBoots",
                          "CanDoStorage", "CanDoMapGlitch", "CanDoAirRefill",
                          "CanStepClip", "CanDoHiddenVillageGlitched",
                          "CanClearForestGlitched"}) {
        p[g] = [](const Context&) { return false; };
    }

    // Generic-pattern enemies.
    for (const auto* enemy : {"CanDefeatArmos", "CanDefeatBabaSerpent", "CanDefeatBabyGohma",
                              "CanDefeatBigBaba", "CanDefeatBokoblin", "CanDefeatBombling",
                              "CanDefeatBomskit", "CanDefeatBubble", "CanDefeatBulblin",
                              "CanDefeatChilfos", "CanDefeatChu", "CanDefeatDekuBaba",
                              "CanDefeatDodongo", "CanDefeatFireBubble", "CanDefeatFireKeese",
                              "CanDefeatHelmasaur", "CanDefeatHelmasaurus", "CanDefeatIceBubble",
                              "CanDefeatIceKeese", "CanDefeatKargarok", "CanDefeatKeese",
                              "CanDefeatLizalfos", "CanDefeatMiniFreezard", "CanDefeatOok",
                              "CanDefeatPuppet", "CanDefeatRat", "CanDefeatRedeadKnight",
                              "CanDefeatShadowBulblin", "CanDefeatShadowDekuBaba",
                              "CanDefeatShadowKargarok", "CanDefeatShadowKeese",
                              "CanDefeatShadowVermin", "CanDefeatSkulltula",
                              "CanDefeatStalchild", "CanDefeatStalhound", "CanDefeatTektite",
                              "CanDefeatTorchSlug", "CanDefeatDekuToad",
                              "CanDefeatWhiteWolfos", "CanDefeatYoungGohma",
                              "CanDefeatBokoblinRed"}) {
        p[enemy] = genericEnemy;
    }

    // Specific enemy deviations.
    p["CanAttackWithSword"] = hasSword;
    p["CanDefeatStalfos"]   = canDefeatStalfos;
    p["CanDefeatFreezard"]  = canDefeatFreezard;
    p["CanDefeatGhoulRat"]  = canDefeatGhoulRat;
    p["CanDefeatPoe"]       = canDefeatPoe;
    p["CanDefeatShadowInsect"]    = canDefeatShadowInsect;
    p["CanDefeatCarrierKargarok"] = canDefeatCarrierKargarok;
    p["CanDefeatTwilitBloat"]     = canDefeatTwilitBloat;
    p["CanDefeatShadowBeast"]     = canDefeatShadowBeast;
    p["CanDefeatSkullKid"]        = canDefeatSkullKid;
    p["CanDefeatKingBulblinBridge"]= canDefeatKingBulblinBridge;
    p["CanDefeatDinalfos"]   = canDefeatDinalfos;
    p["CanDefeatDekuLike"]   = canDefeatDekuLike;
    p["CanDefeatBeamos"]     = canDefeatBeamos;
    p["CanDefeatDarknut"]    = canDefeatDarknut;
    p["CanDefeatBari"]       = canDefeatBari;
    p["CanDefeatHangingBabaSerpent"] = canDefeatHangingBabaSerpent;
    p["CanDefeatLeever"]     = canDefeatLeever;
    p["CanDefeatMoldorm"]    = canDefeatLeever;
    p["CanDefeatPoisonMite"] = canDefeatPoisonMite;
    p["CanDefeatShellBlade"] = canDefeatShellBlade;
    p["CanDefeatSkullfish"]  = canDefeatSkullfish;
    p["CanDefeatTileWorm"]   = canDefeatTileWorm;
    p["CanDefeatToado"]      = canDefeatToado;
    p["CanDefeatWaterToadpoli"] = canDefeatWaterToadpoli;
    p["CanDefeatFireToadpoli"]  = canDefeatFireToadpoli;
    p["CanDefeatChuWorm"]    = canDefeatChuWorm;
    p["CanDefeatBombfish"]   = canDefeatBombfish;
    p["CanDefeatGoron"]      = canDefeatGoron;
    p["CanDefeatGuay"]       = canDefeatGuay;
    p["CanDefeatWalltula"]   = canDefeatWalltula;
    p["CanDefeatAeralfos"]   = canDefeatAeralfos;
    p["CanDefeatDangoro"]    = canDefeatDangoro;
    p["CanDefeatDeathSword"] = canDefeatDeathSword;
    p["CanDefeatDarkhammer"] = canDefeatDarkhammer;
    p["CanDefeatPhantomZant"]= canDefeatPhantomZant;
    p["CanDefeatZantHead"]   = canDefeatZantHead;
    p["CanDefeatKingBulblinDesert"] = canDefeatKingBulblinDesert;
    p["CanDefeatKingBulblinCastle"] = canDefeatKingBulblinCastle;

    // Bosses.
    p["CanDefeatDiababa"]   = canDefeatDiababa;
    p["CanDefeatFyrus"]     = canDefeatFyrus;
    p["CanDefeatMorpheel"]  = canDefeatMorpheel;
    p["CanDefeatStallord"]  = canDefeatStallord;
    p["CanDefeatBlizzeta"]  = canDefeatBlizzeta;
    p["CanDefeatArmogohma"] = canDefeatArmogohma;
    p["CanDefeatArgorok"]   = canDefeatArgorok;
    p["CanDefeatZant"]      = canDefeatZant;
    p["CanDefeatGanondorf"] = canDefeatGanondorf;

    // Twilight / story / dungeon clears.
    p["CanCompleteFaronTwilight"]   = canCompleteFaronTwilight;
    p["CanCompleteEldinTwilight"]   = canCompleteEldinTwilight;
    p["CanCompleteLanayruTwilight"] = canCompleteLanayruTwilight;
    p["CanCompleteMDH"]             = canCompleteMDH;
    p["CanCompleteAllTwilight"]     = canCompleteAllTwilight;
    p["CanCompletePrologue"]        = canCompletePrologue;
    p["CanCompleteGoats1"]          = canCompleteGoats1;
    p["CanCompleteGoats2"]          = canCompleteGoats1;
    p["CanMidnaCharge"]             = canMidnaCharge;

    p["CanCompleteForestTemple"]    = canCompleteForestTemple;
    p["CanCompleteGoronMines"]      = canCompleteGoronMines;
    p["CanCompleteLakebedTemple"]   = canCompleteLakebedTemple;
    p["CanCompleteArbitersGrounds"] = canCompleteArbitersGrounds;
    p["CanCompleteSnowpeakRuins"]   = canCompleteSnowpeakRuins;
    p["CanCompleteTempleofTime"]    = canCompleteTempleofTime;
    p["CanCompleteCityinTheSky"]    = canCompleteCityinTheSky;
    p["CanCompletePalaceofTwilight"]= canCompletePalaceofTwilight;
    p["CanCompletePalaceOfTwilight"]= canCompletePalaceofTwilight;  // alias
    p["CanCompleteHyruleCastle"]    = canCompleteHyruleCastle;
    p["CanCompleteAllDungeons"]     = canCompleteAllDungeons;
    p["CanClearForest"]             = canClearForest;

    // Forest Temple monkey logic.
    p["CanBreakMonkeyCage"] = canBreakMonkeyCage;
    p["CanFreeAllMonkeys"]  = canFreeAllMonkeys;

    p["CanWarpMeteor"]      = canWarpMeteor;
}

}  // namespace tpt::core::logic
