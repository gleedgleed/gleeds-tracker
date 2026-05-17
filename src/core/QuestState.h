#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tpt::core {

// Twilight progression labels (transform_level, dark_clear_level).
inline constexpr std::array<std::string_view, 5> kTwilightLevels{
    "Sewers", "Faron", "Eldin", "Lanayru", "MDH/Full"};

struct PortalState {
    std::string_view name;
    bool unlocked = false;
};

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

    // Twilight progression.
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
