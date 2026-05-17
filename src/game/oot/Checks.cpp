#include "game/oot/Checks.h"

#include <fstream>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace tpt::game::oot {

namespace {

// String → CheckType. Names from OoTR's location_table tuples. Unknown
// types fall through to Unsupported — they're still loaded into the
// list and displayed, just stay [?] until we add their completion
// mapping. Doing it as a map (not a switch) so adding a new type to the
// canonical list is a one-line change here.
CheckType classifyType(const std::string& s) {
    static const std::unordered_map<std::string, CheckType> kMap{
        // Vanilla flag-table types where `default` is genuinely a flat
        // bit index into the table.
        {"Chest",         CheckType::Chest},
        {"Collectable",   CheckType::Collectable},
        // ActorOverride uses sceneFlags.collect just like Collectable per
        // OoTR/Patches.py:2147 — same handler, same flag.
        {"ActorOverride", CheckType::Collectable},
        {"GS Token",      CheckType::GSToken},

        // OoTR xflag-system collectibles. Completion lives in
        // `collectible_override_flags`, indexed via xflag tables. See
        // save/Xflags.{h,cpp} for the lookup implementation.
        {"Pot",           CheckType::Xflag},
        {"Crate",         CheckType::Xflag},
        {"SmallCrate",    CheckType::Xflag},
        {"FlyingPot",     CheckType::Xflag},
        {"Beehive",       CheckType::Xflag},
        {"Wonderitem",    CheckType::Xflag},
        {"SilverRupee",   CheckType::Xflag},
        {"Freestanding",  CheckType::Xflag},
        {"RupeeTower",    CheckType::Xflag},
        {"Drop",          CheckType::Xflag},

        // Boss — eventChkInf bit set by z_door_warp1 when the blue warp
        // is taken. Distinct from BossHeart (heart container pickup).
        // 6 of 8 bosses have a per-warp flag; Twinrova/Bongo Bongo use
        // vanilla CHECK_QUEST_ITEM with no persistent flag, so they
        // stay [?]. See bossEventChkInfBit() for the mapping.
        {"Boss",          CheckType::Boss},

        // BossHeart — ovl_Item_B_Heart sets sceneFlags[currScene].collect
        // bit 31 when the heart container is picked up. Distinct from
        // the warp flag: the heart can be skipped and collected later.
        // Read from the OoTR `scene` field (boss-room scene 17..24).
        {"BossHeart",     CheckType::BossHeart},

        // Scrub / GrottoScrub — OoTR's Deku_Set_Sold_Out (ASM/src/shop.asm)
        // stores per-scrub purchase bits in sceneFlags[scene].unk. Bit
        // position = (actorParams + 1) mod 32; `default` carries the
        // actor params value. Vanilla z_en_dns does NOT set these bits
        // — scrubs are repeatable in pure vanilla — but the storage lives
        // in vanilla scene-flag space so no OoTR symbol resolution is
        // needed to read it. GrottoScrub additionally maps grotto_id to
        // a "free scene" via `scene - 0xD6` (the scene field in the
        // location is the grotto_id directly).
        {"Scrub",         CheckType::Scrub},
        {"GrottoScrub",   CheckType::GrottoScrub},

        // Song — eventChkInf bit per song teacher, set BEFORE the
        // give-song path in the teacher's actor (so it survives OoTR
        // item substitution). 8 of 12 songs have a clean signal; the
        // other 4 (Malon's, Saria's, Sun's Song, Requiem) award via the
        // ocarinaMode message path with no per-teacher flag. See
        // songEventBit() for the mapping.
        {"Song",          CheckType::Song},

        // NPC — name-based dispatch via npcByName(). Each NPC sets a
        // distinct vanilla flag (eventChkInf / itemGetInf / infTable),
        // identified by reading the actor's source in oot-main. Roughly
        // 25 of 72 entries resolved; the rest stay [?] (no clean
        // vanilla signal — see doc/oot-todo.md §2).
        {"NPC",           CheckType::NPC},

        // Event — entries have `default: null` in OoTR's location_table,
        // so we dispatch by location name in eventByName() rather than via
        // the type/flagId switch. Only locations with a verified vanilla
        // flag are wired up; the rest stay [?].
        {"Event",         CheckType::Event},

        // Cutscene — per-`default` dispatch. OoTR uses `default` as its
        // OVR_DELAYED key.flag, so each value needs its own vanilla flag
        // lookup. Supported defaults: 1,2,16,17,18 (ToT Light Arrows, LW
        // Gift from Saria, the 3 spell Great Fairies). Defaults 3,19,20,21
        // (Gift from Sages, magic Great Fairies) currently return nullopt
        // — no per-location flag exists in vanilla for the magic GFs (they
        // only touch shared isMagicAcquired/isDoubleMagicAcquired/
        // isDoubleDefenseAcquired). See the Cutscene arm in
        // isCheckComplete() for details and doc/oot-completion-tracking-
        // handoff.md §4.3.
        {"Cutscene",      CheckType::Cutscene},

        // (Song / NPC moved up — both classified now via name/default
        //  dispatch. See npcByName / songEventBit.)

        // Shop / MaskShop remain Unsupported (OoTR-specific slot
        // ordering — see doc/oot-todo.md §4). BossHeart was reclassified
        // above; Scrub / GrottoScrub / Song / NPC are now wired up.
    };
    if (auto it = kMap.find(s); it != kMap.end()) return it->second;
    return CheckType::Unsupported;
}

// Pick the user-facing area from OoTR's filter_tags list. The first tag
// that isn't a "kind" or generic-region label tends to be the actual
// region name (e.g. "Kokiri Forest"). Failing that, fall back to the
// first tag or "Other".
const std::unordered_set<std::string>& skippableCategories() {
    static const std::unordered_set<std::string> kSkip{
        "Chests", "Cows", "Freestandings", "Gold Skulltulas", "Shops",
        "Songs", "NPCs", "Dungeon Rewards", "Grottos", "Pots", "Crates",
        "Beehives", "Flying Pots", "Small Crates", "Rupee Towers",
        "Wonderitems", "Silver Rupees", "Hints", "Minigames",
        "Vanilla Dungeons", "Master Quest",
        "Need Spiritual Stones",
    };
    return kSkip;
}

std::string deriveArea(const std::vector<std::string>& cats) {
    const auto& skip = skippableCategories();
    for (const auto& c : cats) {
        if (!skip.count(c)) return c;
    }
    if (!cats.empty()) return cats.front();
    return "Other";
}

// Event entries have `default: null`; dispatch by location name. Only
// names with a verified vanilla flag belong here — unhandled names
// return nullopt so the UI shows [?] rather than guessing.
std::optional<bool> eventByName(const Check& chk,
                                const save::PlayerData& pd,
                                const save::SaveFlags& flags) {
    if (chk.name == "Deliver Rutos Letter") {
        // EVENTCHKINF_GAVE_LETTER_TO_KING_ZORA (0x33) — set by
        // ovl_En_Kz when Link hands over the letter. Fires before any
        // give-item path so OoTR substitution doesn't affect it.
        return save::eventBit(flags, 0x33);
    }
    if (chk.name == "Pierre") {
        // playerData.scarecrowSpawnSongSet — set by z_message.c after
        // the adult recording completes the 8-note song. The flag is
        // set BEFORE any give-item path, so it survives OoTR rando.
        return pd.scarecrowSpawnSongSet;
    }
    // Master Sword Pedestal: EVENTCHKINF_REVEALED_MASTER_SWORD (0x4F)
    //   is "entered the chamber", not "pulled the sword". No clean
    //   persistent "pulled" indicator — defer.
    // Ganon: terminal state, low priority — defer.
    return std::nullopt;
}

// OoTR `default` (5..12) → vanilla blue-warp eventChkInf bit. Mapping
// extracted from oot-main/src/overlays/actors/ovl_Door_Warp1/z_door_warp1.c
// — the SET_EVENTCHKINF call right before Item_Give in each boss-scene
// branch. Twinrova (11) and Bongo Bongo (12) use CHECK_QUEST_ITEM with
// no persistent eventChkInf, so they return nullopt → [?]. The bit is
// set before the substitutable Item_Give, so OoTR rando does not mask it.
std::optional<std::uint16_t> bossEventChkInfBit(std::uint16_t flagId) {
    switch (flagId) {
        case 5:  return 0x07;  // Queen Gohma     (Deku Tree blue warp)
        case 6:  return 0x25;  // King Dodongo    (Dodongo's Cavern)
        case 7:  return 0x37;  // Barinade        (Jabu-Jabu Ruto warp)
        case 8:  return 0x48;  // Phantom Ganon   (Forest Temple)
        case 9:  return 0x49;  // Volvagia        (Fire Temple)
        case 10: return 0x4A;  // Morpha          (Water Temple)
        default: return std::nullopt;
    }
}

// OoTR `default` (0x20..0x2B) → vanilla eventChkInf bit for song-teacher
// cutscenes. Bits identified from the teacher actor (or z_demo.c for
// cutscene-trigger-dispatched songs). All set BEFORE the give-item path
// so OoTR substitution does not mask them. 4 of 12 songs have no
// vanilla pre-give flag — they go through the ocarinaMode message path
// directly — and stay [?].
// Songs use a mix of eventChkInf bits and scene-flag bits. The
// dispatch is inlined in the Song arm of isCheckComplete; this header
// just documents the layout. Mapping derived from oot-main actor sources.
//
// Nocturne/Requiem use the cutscene-entry trigger bit rather than the
// per-give bit because OoTR's cutscene patches make the entry trigger
// more reliable in practice.
// Saria's/Epona's use scene-flag bits that OoTR custom-sets in
// unused high bits of vanilla sceneFlags[].chest — pure-vanilla
// OoT will never set these, but the storage is in vanilla scene-flag
// space so no OoTR symbol resolution is needed.

// NPC checks dispatched by name. Each entry below was verified against
// the corresponding actor's source in oot-main. The flag is the one
// the actor sets either before Actor_OfferGetItem or in the
// TEXT_STATE_DONE / Actor_HasParent callback — both fire before OoTR's
// give-item substitution kicks in. Unhandled names return nullopt →
// [?]. See doc/oot-todo.md §2 for the catalogue of remaining names.
std::optional<bool> npcByName(const Check& chk,
                              const save::PlayerData& pd,
                              const save::Inventory& inv,
                              const save::SaveFlags& flags) {
    const auto& n = chk.name;

    // ZR Frogs (7 entries) — ovl_En_Fr sets eventChkInf bits 0xD0..0xD6
    // in EnFr_SetReward before Actor_OfferGetItem.
    if (n == "ZR Frogs Ocarina Game")    return save::eventBit(flags, 0xD0);
    if (n == "ZR Frogs Zeldas Lullaby")  return save::eventBit(flags, 0xD1);
    if (n == "ZR Frogs Eponas Song")     return save::eventBit(flags, 0xD2);
    if (n == "ZR Frogs Suns Song")       return save::eventBit(flags, 0xD3);
    if (n == "ZR Frogs Sarias Song")     return save::eventBit(flags, 0xD4);
    if (n == "ZR Frogs Song of Time")    return save::eventBit(flags, 0xD5);
    if (n == "ZR Frogs in the Rain")     return save::eventBit(flags, 0xD6);

    // Skulltula House rewards 10/20/30/40/50 — ovl_En_Sth.
    // The 100-token reward has no vanilla flag (sEventFlags[0] = 0).
    if (n == "Kak 10 Gold Skulltula Reward")  return save::eventBit(flags, 0xDA);
    if (n == "Kak 20 Gold Skulltula Reward")  return save::eventBit(flags, 0xDB);
    if (n == "Kak 30 Gold Skulltula Reward")  return save::eventBit(flags, 0xDC);
    if (n == "Kak 40 Gold Skulltula Reward")  return save::eventBit(flags, 0xDD);
    if (n == "Kak 50 Gold Skulltula Reward")  return save::eventBit(flags, 0xDE);

    // Bombchu Bowling — ovl_En_Bom_Bowl_Pit (only first two prizes
    // have a vanilla itemGetInf bit; the chu/bomb cycles are infinite
    // in vanilla and OoTR adds custom scene-flag bits for them).
    if (n == "Market Bombchu Bowling First Prize")
        return save::itemGetBit(flags, 0x11);
    if (n == "Market Bombchu Bowling Second Prize")
        return save::itemGetBit(flags, 0x12);

    // LW Skull Kid heart piece — ovl_En_Skj. 0x16 fires after the song.
    if (n == "LW Skull Kid")  return save::itemGetBit(flags, 0x16);

    // Deku Theater rewards — ovl_En_Dnt_Jiji (Stick / Nut upgrades).
    if (n == "Deku Theater Skull Mask")     return save::itemGetBit(flags, 0x1E);
    if (n == "Deku Theater Mask of Truth")  return save::itemGetBit(flags, 0x1F);

    // Shooting Galleries — ovl_En_Syateki_Man.
    if (n == "Market Shooting Gallery Reward")  return save::itemGetBit(flags, 0x0D);
    if (n == "Kak Shooting Gallery Reward")     return save::itemGetBit(flags, 0x0E);

    // LW Ocarina Memory Game — ovl_En_Skj, FinishOcarinaGameRound round 3.
    if (n == "LW Ocarina Memory Game")  return save::itemGetBit(flags, 0x17);

    // Hyrule Castle Malon — EVENTCHKINF_RECEIVED_WEIRD_EGG.
    if (n == "HC Malon Egg")  return save::eventBit(flags, 0x12);

    // Talon's chickens — ITEMGETINF_TALON_BOTTLE (0x02).
    if (n == "LLR Talons Chickens")  return save::itemGetBit(flags, 0x02);

    // Cucco Lady — ovl_En_Niw_Lady; 0x0C = pocket-cucco-given to child,
    // 0x2C = first adult interaction.
    if (n == "Kak Anju as Child")  return save::itemGetBit(flags, 0x0C);
    if (n == "Kak Anju as Adult")  return save::itemGetBit(flags, 0x2C);

    // DMT Biggoron — bgsFlag is set when the Biggoron Sword is finally
    // received. This is the terminal trade-quest step.
    if (n == "DMT Biggoron")  return pd.hasBiggoronSword;

    // Darunia's reaction to Saria's Song — ovl_En_Du:148.
    if (n == "GC Darunias Joy")  return save::eventBit(flags, 0x22);

    // Adult Rolling Goron — INFTABLE_11E set before Message_CloseTextbox.
    if (n == "GC Rolling Goron as Adult")  return save::infTableBit(flags, 0x11E);

    // ZD Diving Minigame — EVENTCHKINF_OBTAINED_SILVER_SCALE (0x38).
    if (n == "ZD Diving Minigame")  return save::eventBit(flags, 0x38);

    // GF Horseback Archery — ovl_En_Ge1.
    if (n == "GF HBA 1000 Points")  return save::itemGetBit(flags, 0x0F);
    if (n == "GF HBA 1500 Points")  return save::infTableBit(flags, 0x190);

    // Lake Hylia Fishing — ovl_Fishing sets bits in HighScores[HS_FISHING]
    // when prizes are awarded. Child / Adult are vanilla; Loach is added
    // by OoTR's fishing.asm:64 (give_loach_reward), bit 0x8000.
    if (n == "LH Child Fishing")  return (pd.hsFishing & 0x00000400u) != 0;
    if (n == "LH Adult Fishing")  return (pd.hsFishing & 0x00000800u) != 0;
    if (n == "LH Loach Fishing")  return (pd.hsFishing & 0x00008000u) != 0;

    // Bombchu Bowling cycle prizes — vanilla cycles infinitely (no flag).
    // OoTR's bombchu_bowling.c uses sceneFlags[0x4B].unk bits 2 / 3 to
    // track that the cycle prize has been awarded once.
    if (n == "Market Bombchu Bowling Bombchus")
        return (flags.scenes[75].unk & (1u << 2)) != 0;
    if (n == "Market Bombchu Bowling Bomb")
        return (flags.scenes[75].unk & (1u << 3)) != 0;

    // Kak 100 Gold Skulltula Reward — vanilla writes no per-reward flag
    // (sEventFlags[0] = 0 in ovl_En_Sth). Use the gsTokens count as a
    // heuristic: the reward is given when the player talks to the
    // Skulltula House guy with 100+ tokens. Can false-positive if the
    // player has 100 tokens but hasn't claimed the reward, false-negative
    // if OoTR alters the threshold.
    if (n == "Kak 100 Gold Skulltula Reward")
        return inv.gsTokens >= 100;

    // HF Ocarina of Time Item — the Hyrule Field cutscene where Zelda
    // throws the OoT into the moat. z_demo.c:118 sets eventChkInf 0xA9
    // on the cutscene-trigger entry (ENTR_HYRULE_FIELD_16). Same bit
    // used for Song of Time (default 0x2A) — both items are given by
    // the same cutscene under vanilla; under OoTR shuffle they're
    // separate rando placements but tied to the same trigger.
    if (n == "HF Ocarina of Time Item")  return save::eventBit(flags, 0xA9);

    // HC Zeldas Letter — given as part of the Zelda-courtyard cutscene
    // that also teaches Lullaby. ovl_Demo_Im:938 sets eventChkInf 0x59
    // before the Item_Give chain (Letter + Song of Lullaby). Same bit
    // used for "Song from Impa" (default 0x26) — both items come from
    // the same cutscene; firing together is correct.
    if (n == "HC Zeldas Letter")  return save::eventBit(flags, 0x59);

    // Hideout Gerudo Membership Card — the card is given by the Gerudo
    // guards after all 4 carpenters are rescued. Use the carpenter-
    // rescued mask (eventChkInf[9] bits 0..3 all set) as the signal —
    // this fires as soon as the rescue is complete and the card-give
    // event runs.
    if (n == "Hideout Gerudo Membership Card") {
        constexpr std::uint16_t kAllCarpenters = 0x000F;  // bits 0..3 of word 9
        return (flags.eventChkInf[9] & kAllCarpenters) == kAllCarpenters;
    }

    return std::nullopt;
}

}  // namespace

bool loadChecks(const std::filesystem::path& jsonPath,
                std::vector<Check>& out,
                std::ostream& errlog) {
    out.clear();
    std::ifstream f(jsonPath);
    if (!f) {
        errlog << "OoT checks: cannot open " << jsonPath.string() << "\n";
        return false;
    }
    nlohmann::json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        errlog << "OoT checks: JSON parse error in " << jsonPath.string()
               << ": " << e.what() << "\n";
        return false;
    }
    if (!j.is_array()) {
        errlog << "OoT checks: " << jsonPath.string() << " is not a JSON array\n";
        return false;
    }
    out.reserve(j.size());
    try {
        for (const auto& entry : j) {
            Check c;
            if (entry.contains("name") && entry["name"].is_string()) {
                c.name = entry["name"].get<std::string>();
            }
            if (entry.contains("type") && entry["type"].is_string()) {
                c.typeRaw = entry["type"].get<std::string>();
            }
            c.type = classifyType(c.typeRaw);
            if (entry.contains("scene") && entry["scene"].is_number_integer()) {
                const auto s = entry["scene"].get<int>();
                c.scene = (s < 0) ? 0xFF : static_cast<std::uint16_t>(s);
            }
            if (entry.contains("default") && entry["default"].is_number_integer()) {
                c.flagId = static_cast<std::uint16_t>(entry["default"].get<int>());
            } else if (c.type == CheckType::Xflag &&
                       entry.contains("default_complex") &&
                       entry["default_complex"].is_array()) {
                // xflag types carry their bit info in default_complex,
                // a 3- or 4-element list: [room, setup_or_grottoId,
                // actor, subflag?].
                const auto& arr = entry["default_complex"];
                auto getU8 = [&](std::size_t i) -> std::uint8_t {
                    if (i >= arr.size() || !arr[i].is_number_integer()) return 0;
                    return static_cast<std::uint8_t>(arr[i].get<int>() & 0xFF);
                };
                c.xflagRoom    = getU8(0);
                c.xflagSetup   = getU8(1);
                c.xflagFlag    = getU8(2);
                c.xflagSubflag = getU8(3);
            } else if (c.type != CheckType::Event) {
                // No scalar default and no usable complex form — mark
                // Unsupported so the UI shows [?] instead of guessing.
                // Event keeps its type: it dispatches by name (see
                // eventByName) and doesn't need flagId.
                c.type = CheckType::Unsupported;
            }
            if (entry.contains("vanilla_item") && entry["vanilla_item"].is_string()) {
                c.vanillaItem = entry["vanilla_item"].get<std::string>();
            }
            if (entry.contains("categories") && entry["categories"].is_array()) {
                for (const auto& cat : entry["categories"]) {
                    if (cat.is_string()) c.categories.push_back(cat.get<std::string>());
                }
            }
            c.area = deriveArea(c.categories);
            out.push_back(std::move(c));
        }
    } catch (const std::exception& e) {
        errlog << "OoT checks: error iterating " << jsonPath.string()
               << ": " << e.what() << "\n";
        out.clear();
        return false;
    }
    return true;
}

std::optional<bool> isCheckComplete(const Check& chk,
                                    const save::PlayerData& pd,
                                    const save::Inventory& inv,
                                    const save::SaveFlags& flags,
                                    const save::XflagState& xst) {
    switch (chk.type) {
        case CheckType::Chest:
            return save::chestBit(flags, static_cast<std::uint8_t>(chk.scene),
                                  static_cast<std::uint8_t>(chk.flagId & 0x1F));
        case CheckType::Collectable:
            return save::collectBit(flags, static_cast<std::uint8_t>(chk.scene),
                                    static_cast<std::uint8_t>(chk.flagId & 0x1F));
        case CheckType::GSToken:
            return save::gsTokenBit(flags, static_cast<std::uint8_t>(chk.scene),
                                    static_cast<std::uint8_t>(chk.flagId & 0xFF));
        case CheckType::Boss: {
            // 6 of 8 dungeon bosses set a per-warp eventChkInf bit in
            // vanilla z_door_warp1.c right before Item_Give. OoTR's
            // item substitution preserves the SET, so the same bit
            // works under both vanilla and rando play.
            if (auto bit = bossEventChkInfBit(chk.flagId); bit) {
                return save::eventBit(flags, *bit);
            }
            // Twinrova / Bongo Bongo skip that vanilla code path —
            // z_door_warp1.c calls CHECK_QUEST_ITEM directly with no
            // SET_EVENTCHKINF. Use the boss-killed sceneFlags.clear
            // bit instead: each boss actor's death calls
            // Flags_SetClear(play, curRoom.num) (ovl_Boss_Tw:2845,
            // ovl_Boss_Sst:1225). Room numbers are stable in vanilla
            // scene layouts:
            //   Twinrova   — Spirit Temple Boss (scene 23) room 3
            //   Bongo Bongo — Shadow Temple Boss (scene 24) room 1
            // This is distinct from BossHeart (collect bit 31, heart
            // container pickup) — the boss-killed and heart-pickup
            // events fire on different player actions.
            if (chk.flagId == 11) return save::clearBit(flags, 23, 3);
            if (chk.flagId == 12) return save::clearBit(flags, 24, 1);
            // Rauru (default 4) — "ToT Reward from Rauru" is the Light
            // Medallion give in the Chamber of Sages cutscene that
            // plays after the first child Master Sword pull. Vanilla
            // only writes inventory.questItems for the medallion; OoTR
            // adds an explicit per-location signal via
            // sage_gifts.c:give_sage_gifts which sets EVENTCHKINF_45
            // when the player returns to the Master Sword chamber
            // (scene 0x43, room 0) after the cutscene completes.
            //
            // Same pattern as the OoTR-set song bits: use OoTR's bit
            // when present, fall back to inventory for pure vanilla.
            if (chk.flagId == 4) {
                if (save::eventBit(flags, 0x45)) return true;
                if (xst.ootrAddrs.valid) return false;
                return inv.medallionLight;
            }
            return std::nullopt;
        }
        case CheckType::BossHeart: {
            // sceneFlags[scene].collect bit 31 — set by ovl_Item_B_Heart
            // when the heart container is picked up. The OoTR `scene`
            // field is the boss room (17..24).
            if (chk.scene >= 0xFF) return std::nullopt;
            return save::collectBit(flags,
                                    static_cast<std::uint8_t>(chk.scene), 31);
        }
        case CheckType::Scrub: {
            if (chk.scene >= flags.scenes.size()) return std::nullopt;
            const std::uint8_t bit =
                static_cast<std::uint8_t>((chk.flagId + 1) & 0x1F);
            return (flags.scenes[chk.scene].unk &
                    (std::uint32_t{1} << bit)) != 0;
        }
        case CheckType::GrottoScrub: {
            // The location's `scene` is the grotto_id. OoTR remaps it to
            // a normally-unused scene's flag entry via `grotto_id - 0xD6`.
            // Grotto IDs in locations.json range from 230..253 → storage
            // scenes 16..39, all within our 124-entry array.
            if (chk.scene < 0xD6) return std::nullopt;
            const std::uint16_t storageScene = chk.scene - 0xD6;
            if (storageScene >= flags.scenes.size()) return std::nullopt;
            const std::uint8_t bit =
                static_cast<std::uint8_t>((chk.flagId + 1) & 0x1F);
            return (flags.scenes[storageScene].unk &
                    (std::uint32_t{1} << bit)) != 0;
        }
        case CheckType::Cutscene: {
            // Magic Great Fairies (defaults 19/20/21) — DMT/DMC/OGC.
            // ovl_Bg_Dy_Yoseizo itself only writes shared playerData
            // bits (isMagicAcquired / isDoubleMagicAcquired /
            // isDoubleDefenseAcquired), but each fountain entrance in
            // SCENE_GREAT_FAIRYS_FOUNTAIN_MAGIC (scene 0x3B = 59)
            // triggers a SwitchFlag actor that sets a distinct bit
            // of sceneFlags[59].swch on visit. The mapping:
            //   bit  8 — OGC Great Fairy (Double Defense)
            //   bit 16 — DMC Great Fairy (Double Magic)
            //   bit 24 — DMT Great Fairy (Magic)
            // Works under both vanilla and OoTR — the switch fires on
            // entering the fountain, independent of which upgrade was
            // given (so OoTR's item substitution doesn't affect it).
            if (chk.flagId == 19) return (flags.scenes[59].swch & (1u << 24)) != 0;
            if (chk.flagId == 20) return (flags.scenes[59].swch & (1u << 16)) != 0;
            if (chk.flagId == 21) return (flags.scenes[59].swch & (1u <<  8)) != 0;
            // Per-`default` dispatch. OoTR's `default` is a key.flag id;
            // each value maps to a distinct vanilla post-cutscene flag.
            //
            // 0xC4 (ToT Light Arrows) and 0xC1 (LW Gift from Saria) are
            //   set by z_demo.c's cutscene-trigger dispatcher; both are
            //   absent from Sram_InitNewSave so they're clean signals.
            // 0x18/0x19/0x1A (ITEMGETINF_18..1A) are set by the Great
            //   Fairy actor (ovl_Bg_Dy_Yoseizo z_bg_dy_yoseizo.c:828)
            //   for the 3 spell-giving fairies (ZF Farore's Wind, HC
            //   Din's Fire, Colossus Nayru's Love).
            //
            // Unsupported defaults (return nullopt → [?]):
            //   3  (Gift from Sages)        — semantics unclear, defer
            //   19 (DMT Great Fairy reward) — magic upgrade fairy; only
            //   20 (DMC Great Fairy reward)   sets isMagicAcquired /
            //   21 (OGC Great Fairy reward)   isDoubleMagicAcquired /
            //                                 isDoubleDefenseAcquired,
            //                                 all shared with other
            //                                 rando-placed items so
            //                                 they can't identify which
            //                                 specific fairy was visited.
            switch (chk.flagId) {
                case 1:  return save::eventBit(flags, 0xC4);
                case 2:  return save::eventBit(flags, 0xC1);
                case 16: return save::itemGetBit(flags, 0x18);
                case 17: return save::itemGetBit(flags, 0x19);
                case 18: return save::itemGetBit(flags, 0x1A);
                default: return std::nullopt;
            }
        }
        case CheckType::Event:
            return eventByName(chk, pd, flags);
        case CheckType::Song:
            switch (chk.flagId) {
                // ----- Reliable under both vanilla and OoTR -----
                // The eventChkInf bit is set by the per-teacher actor
                // (or z_demo.c cutscene-trigger dispatcher) at or near
                // the Item_Give call. OoTR's item substitution swaps the
                // item id only — the surrounding SET_EVENTCHKINF still
                // runs, so these fire under both vanilla and OoTR play
                // regardless of which song actually got placed there.
                case 0x20: return save::eventBit(flags, 0x50);  // Minuet   (ovl_En_Xc:329)
                case 0x21: return save::eventBit(flags, 0x51);  // Bolero   (ovl_En_Xc:364)
                case 0x22: return save::eventBit(flags, 0x52);  // Serenade (ovl_En_Xc:397)
                case 0x23: return save::eventBit(flags, 0xAC);  // Requiem  (z_demo.c:2427 — Desert Colossus adult-entry trigger fires the teach cutscene + sets the bit, no extra preconditions)
                case 0x24: return save::eventBit(flags, 0xAA);  // Nocturne (z_demo.c:2433 — Kakariko adult-entry trigger fires the teach cutscene + sets the bit; under OoTR boss/item shuffle the precondition bits 0x48/0x49/0x4A may never set, in which case the check is genuinely unreachable and "pending" is correct)
                case 0x25: return save::eventBit(flags, 0x55);  // Prelude  (ovl_En_Xc:2211)
                case 0x26: return save::eventBit(flags, 0x59);  // Lullaby  (ovl_Demo_Im:938)
                case 0x2A: return save::eventBit(flags, 0xA9);  // Song of Time (z_demo.c:118 — HF child-entry trigger after Zelda escape; same trigger plays the cutscene AND sets the bit, so bit-clear = check-not-done)
                case 0x2B: return save::eventBit(flags, 0x5B);  // Storms   (ovl_En_Fu:168)

                // ----- Reliable in OoTR via OoTR-only hooks (NOT reverted) -----
                // OoTR's ASM hooks explicitly set these bits and the
                // hooks survive in OoTR's default mode (songs at song-
                // only spots). Under pure vanilla the bits aren't set
                // on first teach, so fall back to the questItems
                // inventory bit (which vanilla itself reads to gate
                // the teach cutscene — see ovl_En_Sa:408 CHECK_QUEST_ITEM).
                //
                //   Saria — set_saria_song_flag (cutscenes.asm:180-185, hooked at hacks.asm:1400)
                //   Malon — malon_reload         (malon_hooks.asm:130-134, hooked at hacks.asm:3495)
                //
                // OoTR-detected gate prevents false-positives: under OoTR
                // the questItems bit fires whenever the song is obtained
                // from any shuffled location, not just this one.
                case 0x27:  // Malon (Epona's)
                    if (save::eventBit(flags, 0x58)) return true;
                    if (xst.ootrAddrs.valid)         return false;
                    return inv.songEpona;
                case 0x28:  // Saria's
                    if (save::eventBit(flags, 0x57)) return true;
                    if (xst.ootrAddrs.valid)         return false;
                    return inv.songSaria;

                // ----- Sun's Song / Royal Family Tomb (0x29) — unreliable in OoTR -----
                // The unique outlier among entry-trigger songs: the teach
                // cutscene (gSunSongGraveSunSongTeachCs) drops the player
                // at ENTR_GRAVEYARD_0 (outside the tomb), NOT back at the
                // tomb. EVENTCHKINF_5A is only set when the player walks
                // BACK INTO ENTR_ROYAL_FAMILYS_TOMB_1, which fires the
                // Part 2 cutscene via the z_demo.c:129 entry trigger.
                //
                // Compare with the other entry-trigger songs above (Song
                // of Time, Nocturne, Requiem): for those, the entry that
                // fires the bit IS the entry where the teach cutscene
                // happens, so "no bit" reliably means "no check done."
                // Sun's Song requires a separate optional re-entry —
                // most OoTR runners walk away from the graveyard without
                // bothering to go back in.
                //
                // In addition, OoTR's `override_suns_song` hook
                // (cutscenes.asm:122-138, hacks.asm:1378) — which WOULD
                // set the bit on first teach — is REVERTED in OoTR's
                // default mode (Patches.py:1325, the `if not
                // songs_as_items` block). Default-mode shuffle works by
                // direct ROM song_id patching at 0xE09F66 instead, so
                // the vanilla give-item path runs unchanged and no
                // per-location flag is set on first teach. The bit only
                // sets reliably under (a) pure vanilla play with grave
                // revisit, or (b) OoTR `songs_as_items=true` mode.
                //
                // Under OoTR in default mode we return std::nullopt →
                // [?] rather than false → [ ], since bit-clear genuinely
                // doesn't tell us whether the check was done. The
                // inventory fallback (inv.songSun) isn't useful in OoTR:
                // the song could come from any shuffled location.
                //
                // Pure vanilla play falls through to the inventory bit,
                // which covers the rare pre-revisit window.
                case 0x29:  // Sun's / Royal Family Tomb
                    if (save::eventBit(flags, 0x5A)) return true;
                    if (xst.ootrAddrs.valid)         return std::nullopt;
                    return inv.songSun;
                default:   return std::nullopt;
            }
        case CheckType::NPC:
            return npcByName(chk, pd, inv, flags);
        case CheckType::Xflag: {
            if (!xst.valid) return std::nullopt;
            save::Xflag xf;
            xf.scene    = static_cast<std::uint8_t>(chk.scene);
            xf.room     = chk.xflagRoom;
            if (chk.scene == 0x3E) {
                // Grotto encoding — `setup` field holds the grotto_id.
                xf.grottoId = chk.xflagSetup;
            } else {
                xf.setup = chk.xflagSetup;
            }
            xf.flag    = chk.xflagFlag;
            xf.subflag = chk.xflagSubflag;
            return save::isXflagSet(xf, xst);
        }
        case CheckType::Unsupported:
        default:
            return std::nullopt;
    }
}

}  // namespace tpt::game::oot
