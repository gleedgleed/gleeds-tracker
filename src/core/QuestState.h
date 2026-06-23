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

// Warp-portal table. A portal is usable as a warp when its per-region "stage
// switch" flag is set — this is exactly what the in-game map screen reads to
// decide which warp icons to draw (dMenu_Fmap_c::checkDrawPortalIcon ->
// dComIfGs_isStageSwitch in the TP decomp). Each portal's (node, switchNo) is
// taken from the matching _02_*PortalItemFunc in Randomizer-master
// (02_modifyItemData.cpp), which mirrors the vanilla unlock. The switch flag
// is set both when the portal is opened in-world AND when the rando hands out
// the "<region>_Portal" item.
//
// We additionally OR in the item's get-item "first-bit" (player_get_item,
// SAVE+0x0CC, keyed by the customItems.h item ID) to cover the one case the
// switch flag misses: a seed pre-giving the portal as a starting item
// ("Unlock Map Regions" on a pre-cleared province) sets the get-item bit but
// not the switch. The portal item IDs are otherwise-unused vanilla slots, so
// the get-item bit never collides with a real item.
//
// Ordon Spring is the start portal: it has no _02_ func and no randomizer
// check, so it's tracked by its get-item bit alone (node = kPortalNoNode).
//
// The matching randomizer check is named "<name> Portal" (Ordon Spring has none).
inline constexpr std::uint8_t kPortalNoNode = 0xFF;  // no stage-switch (start portal)
struct PortalEntry {
    std::string_view name;
    std::uint8_t     node;      // AreaNodesID owning the stage-switch flag
    std::uint16_t    switchNo;  // dComIfGs_isStageSwitch flag number
    std::uint8_t     itemId;    // get-item first-bit (pre-give / received signal)
};
inline constexpr std::array<PortalEntry, 15> kPortalTable{{
    {"Ordon Spring",      kPortalNoNode, 0x00, 0x14},
    {"South Faron",       0x02,          0x47, 0x15},
    {"North Faron",       0x02,          0x02, 0x3C},
    {"Kakariko Gorge",    0x06,          0x15, 0x4D},
    {"Kakariko Village",  0x03,          0x1F, 0x4E},
    {"Death Mountain",    0x03,          0x15, 0x52},
    {"Castle Town",       0x06,          0x03, 0x3A},
    {"Zoras Domain",      0x04,          0x02, 0x57},
    {"Lake Hylia",        0x04,          0x0A, 0x8F},
    {"Gerudo Desert",     0x0A,          0x15, 0x3B},
    {"Mirror Chamber",    0x0A,          0x28, 0xAE},
    {"Snowpeak",          0x08,          0x15, 0xAF},
    {"Sacred Grove",      0x07,          0x64, 0xBF},
    {"Bridge of Eldin",   0x06,          0x63, 0xE8},
    {"Upper Zoras River", 0x04,          0x15, 0x39},
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

// Decode a `dComIfGs_*StageSwitch` flag number into (byteOffset, bitMask),
// where byteOffset is relative to the start of a node's 0x20-byte memBit
// block. Switches live in `mSwitch[4]` at block+0x08, stored as big-endian
// uint32 words (dSv_memBit_c::isSwitch reads `mSwitch[no>>5] & (1<<(no&0x1F))`).
// So within each 4-byte word the byte order is reversed vs. a flat array,
// hence the `3 - ...` term. See the TP decomp (d_save.cpp / d_com_inf_game.cpp).
constexpr std::pair<std::uint16_t, std::uint8_t> decodeSwitchFlag(std::uint16_t flag) {
    const std::uint16_t byteOff = static_cast<std::uint16_t>(
        0x08u + 4u * (flag >> 5) + (3u - ((flag & 0x1Fu) >> 3)));
    return {byteOff, static_cast<std::uint8_t>(1u << (flag & 0x7))};
}

QuestState readQuestState(std::span<const std::uint8_t> block,
                          std::uint8_t currentNode);

}  // namespace tpt::core
