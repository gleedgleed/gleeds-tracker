#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tpt::core {

struct PortalState {
    std::string_view name;
    bool unlocked = false;
};

// Warp-portal item table: portal name -> GC custom item ID (customItems.h,
// "<region>_Portal"). The item's "first-bit" in player_get_item (SAVE+0x0CC)
// is set whenever the player owns that warp — by activating the portal
// in-world OR by the seed pre-giving it as a starting item ("Unlock Map
// Regions" on a pre-cleared province). It's the same signal the Midna warp
// menu reads. The per-region switch flags set by _02_*PortalItemFunc are
// only a side effect of receiving the item and are NOT written for pre-gives,
// so they're unreliable for completion. The matching randomizer check is
// named "<name> Portal" (except "Ordon Spring", the start portal, which has
// no check).
struct PortalEntry {
    std::string_view name;
    std::uint8_t     itemId;
};
inline constexpr std::array<PortalEntry, 15> kPortalTable{{
    {"Ordon Spring",      0x14},
    {"South Faron",       0x15},
    {"North Faron",       0x3C},
    {"Kakariko Gorge",    0x4D},
    {"Kakariko Village",  0x4E},
    {"Death Mountain",    0x52},
    {"Castle Town",       0x3A},
    {"Zoras Domain",      0x57},
    {"Lake Hylia",        0x8F},
    {"Gerudo Desert",     0x3B},
    {"Mirror Chamber",    0xAE},
    {"Snowpeak",          0xAF},
    {"Sacred Grove",      0xBF},
    {"Bridge of Eldin",   0xE8},
    {"Upper Zoras River", 0x39},
}};

struct SwitchKeyState {
    std::string_view name;   // DSL item name (e.g. "North_Faron_Woods_Gate_Key")
    bool open = false;
};

struct QuestState {
    // Vitals.
    //
    // TP encodes health asymmetrically:
    //   curHealth: quarter-heart units (each Octorok hit = -1, full heart = 4)
    //   maxHealth: fifth-heart units (each heart-piece picked up = +1, full container = 5)
    // Fresh save at full HP: curHealth=12, maxHealth=15 (both = "3 hearts").
    std::uint16_t curHealth      = 0;
    std::uint16_t maxHealth      = 0;
    std::uint16_t rupees         = 0;
    std::uint16_t curLanternOil  = 0;
    std::uint16_t maxLanternOil  = 0;
    std::uint8_t  walletTier     = 0;  // 0=child, 1=big, 2=giant
    std::uint8_t  magicCur       = 0;
    std::uint8_t  magicMax       = 0;
    std::uint8_t  currentForm    = 0;  // 0=human, non-zero=wolf

    // Twilight progression. Both are BITMASKS, not ordinal levels:
    //   0x1 Faron, 0x2 Eldin, 0x4 Lanayru cleared; 0x8 MDH (Midna's Desperate
    //   Hour) completed. See Randomizer-master user_patch/05_newFileFunctions
    //   and main.cpp. transformLevel tracks the "last transformed" twilight and
    //   is only written during the MDH sequence (so it stays 0 until MDH).
    std::uint8_t  transformLevel  = 0;
    std::uint8_t  darkClearLevel  = 0;

    // Light drops.
    std::uint8_t  faronTears   = 0;
    std::uint8_t  eldinTears   = 0;
    std::uint8_t  lanayruTears = 0;

    // Collect.
    std::uint8_t  poeSouls     = 0;

    // Identity / meta.
    std::string playerName;
    std::string currentStage;
    std::uint8_t spawnPoint = 0;
    std::uint8_t roomId     = 0;
    std::uint16_t deaths    = 0;
    std::uint64_t totalTimeFrames = 0;

    // Insertion-ordered (matches Python tables).
    std::vector<PortalState>     portals;
    std::vector<SwitchKeyState>  switchKeys;
};

// Decode raw `dComIfGs_onStageSwitch` flag into byte_offset + bit_mask.
constexpr std::pair<std::uint16_t, std::uint8_t> decodeSwitchFlag(std::uint16_t flag) {
    return {static_cast<std::uint16_t>(flag >> 3),
            static_cast<std::uint8_t>(1u << (flag & 0x7))};
}

QuestState readQuestState(std::span<const std::uint8_t> block,
                          std::uint8_t currentNode);

}  // namespace tpt::core
