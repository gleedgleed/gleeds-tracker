#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace tpt::game::oot::save {

// Top-level vitals decoded from SavePlayerData. Health/magic units match
// the in-game representation (1 heart = 16 units, 1 magic meter = 48 units);
// callers divide for display.
struct PlayerData {
    // True iff the SaveContext we read contains a loaded save (newf
    // matches "ZELDAZ"). When false, the rest of the struct is its
    // zero-initialized default and should not be trusted.
    bool saveLoaded = false;

    std::string  playerName;
    std::uint16_t deaths = 0;

    std::int16_t healthCapacity = 0;  // 16 units = 1 heart container
    std::int16_t health         = 0;  // 16 units = 1 heart, 4 units = 1/4 heart
    std::int8_t  magicLevel     = 0;  // 0/1/2 = none/single/double meter
    std::int8_t  magic          = 0;  // current magic in units
    std::int16_t rupees         = 0;

    bool isMagicAcquired         = false;  // first magic upgrade picked up
    bool isDoubleMagicAcquired   = false;  // double-meter upgrade picked up
    bool isDoubleDefenseAcquired = false;  // double-defense hearts (red ring)
    bool hasBiggoronSword        = false;  // bgsFlag — Biggoron sword received

    // Set after the player records the 8-note scarecrow song to Pierre
    // (z_message.c:3748). The signal for OoTR's "Pierre" location.
    bool scarecrowSpawnSongSet   = false;

    // HighScores[HS_FISHING] u32 — bitfield with per-prize bits set by
    // ovl_Fishing when a fishing prize is awarded. We carry the raw u32
    // so callers can test specific bits (0x400 child, 0x800 adult,
    // 0x8000 OoTR loach).
    std::uint32_t hsFishing      = 0;

    // Current form (Save.linkAge == 0). OoT logic predicates branch on this.
    bool isAdult = true;
};

// Decode PlayerData from a SaveContext slice. `saveContext` must contain
// at least kHeaderReadSize bytes (enough for PlayerData + Inventory). If
// the newf magic doesn't match, the returned struct has saveLoaded=false
// and all other fields zeroed.
PlayerData readPlayerData(std::span<const std::uint8_t> saveContext);

}  // namespace tpt::game::oot::save
