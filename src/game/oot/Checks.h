#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "game/oot/save/Inventory.h"
#include "game/oot/save/PlayerData.h"
#include "game/oot/save/SaveFlags.h"
#include "game/oot/save/Xflags.h"

namespace tpt::game::oot {

// Type tag from OoTR's location_table. Stored as the original string
// from LocationList.py so future types (added by OoTR upstream) can be
// loaded without code changes; only completion mapping has to be
// extended. Common values: "Chest", "Collectable", "GS Token", "Boss",
// "Song", "Cutscene", "Event", "BossHeart", "NPC", "Scrub", "Shop",
// "MaskShop", "GrottoScrub", "Freestanding", "Pot", "Crate", …
//
// CheckType captures the *interpretation* — how to look up completion.
// Many of OoTR's type strings map to the same interpretation
// (Collectable + ActorOverride both use sceneFlags.collect, etc.).
enum class CheckType {
    Chest,            // sceneFlags[scene].chest, bit `default`
    Collectable,      // sceneFlags[scene].collect, bit `default`
    GSToken,          // gsFlags[scene], mask `default`
    Event,            // dispatched by name (default is null) — see eventByName
    Song,             // currently Unsupported — OoTR substitution masks vanilla flag
    Boss,             // sceneFlags[default+12].collect bit 31 (blue warp)
    Cutscene,         // per-`default` dispatch — see Cutscene arm
    BossHeart,        // sceneFlags[scene].collect bit 31 (heart container pickup)
    NPC,              // itemGetInf, flat bit `default` — best-effort
    Scrub,            // sceneFlags[scene].unk bit (default+1)&0x1F
    GrottoScrub,      // sceneFlags[scene-0xD6].unk bit (default+1)&0x1F
    // OoTR-shuffle collectibles whose completion lives in the rando's
    // `collectible_override_flags` byte array, indexed via xflag tables.
    // Covers Pot / Crate / SmallCrate / FlyingPot / Beehive / Wonderitem /
    // SilverRupee / Freestanding / RupeeTower / Drop. The xflag fields
    // below carry the (room, setup, actor, subflag) tuple from OoTR's
    // location_table; lookup uses save::isXflagSet().
    Xflag,
    Unsupported,      // type recognised but completion mapping not yet implemented
};

struct Check {
    std::string name;             // OoTR location name, e.g. "KF Midos Top Left Chest"
    std::string typeRaw;          // original OoTR type string ("Chest", "GS Token", …)
    CheckType   type = CheckType::Unsupported;
    std::uint16_t scene  = 0xFF;   // scene id; 0xFF for non-scene-bound types
    std::uint16_t flagId = 0;      // type-specific: chest bit / collect bit / event index / GS mask
    // Xflag-only fields. Populated from JSON `default_complex` (a 3- or
    // 4-element list `[room, setup_or_grotto_id, actor_index, subflag?]`)
    // for OoTR's collectible-shuffle types. Unused for other types.
    std::uint8_t xflagRoom    = 0;
    std::uint8_t xflagSetup   = 0;
    std::uint8_t xflagFlag    = 0;
    std::uint8_t xflagSubflag = 0;
    std::string vanillaItem;       // what the vanilla game places here (display only)
    std::vector<std::string> categories;  // OoTR filter tags — first non-meta one is the area
    std::string area;              // primary display group, derived from categories
};

// Load OoTR locations from a JSON file produced by tools/extract_oot_locations.py.
// Returns true on success. On failure, writes a diagnostic to `errlog` and
// returns false with `out` cleared.
bool loadChecks(const std::filesystem::path& jsonPath,
                std::vector<Check>& out,
                std::ostream& errlog);

// Completion lookup. Returns nullopt for Unsupported types (caller shows
// "[?]" to distinguish "not implemented" from "[ ] pending"). Also
// returns nullopt for Xflag-typed checks when the xflag state hasn't
// been fetched yet (`xst.valid == false`). PlayerData carries non-flag
// save-info fields (e.g. scarecrowSpawnSongSet) consulted by the
// name-based Event dispatch.
std::optional<bool> isCheckComplete(const Check& chk,
                                    const save::PlayerData& pd,
                                    const save::Inventory& inv,
                                    const save::SaveFlags& flags,
                                    const save::XflagState& xst);

}  // namespace tpt::game::oot
