#include "core/QuestState.h"

#include <array>
#include <cstring>

#include "core/EventFlags.h"
#include "core/SaveOffsets.h"

namespace tpt::core {

namespace {

// All offsets relative to SAVE_FILE_ADDR.
constexpr std::uint32_t kOffMaxHealth        = 0x000;  // u16 BE
constexpr std::uint32_t kOffCurHealth        = 0x002;  // u16 BE
constexpr std::uint32_t kOffRupees           = 0x004;  // u16 BE
constexpr std::uint32_t kOffMaxLanternOil    = 0x006;  // u16 BE
constexpr std::uint32_t kOffCurLanternOil    = 0x008;  // u16 BE
constexpr std::uint32_t kOffWalletTier       = 0x019;
constexpr std::uint32_t kOffMagicMax         = 0x01A;
constexpr std::uint32_t kOffMagicCur         = 0x01B;
constexpr std::uint32_t kOffCurrentForm      = 0x01E;

constexpr std::uint32_t kOffTransformLevel   = 0x030;
constexpr std::uint32_t kOffDarkClearLevel   = 0x031;

constexpr std::uint32_t kOffCurrentStage     = 0x058;  // char[8]
constexpr std::uint32_t kOffSpawnPoint       = 0x060;
constexpr std::uint32_t kOffRoomId           = 0x061;

constexpr std::uint32_t kOffPoeSouls         = 0x10C;

constexpr std::uint32_t kOffTearsFaron       = 0x114;
constexpr std::uint32_t kOffTearsEldin       = 0x115;
constexpr std::uint32_t kOffTearsLanayru    = 0x116;

constexpr std::uint32_t kOffTotalTime        = 0x1A8;  // u64 BE
constexpr std::uint32_t kOffDeathCount       = 0x1B2;  // u16 BE
constexpr std::uint32_t kOffPlayerName       = 0x1B4;  // 16 bytes ASCII

inline std::uint8_t  rd8 (std::span<const std::uint8_t> b, std::uint32_t o) { return b[o]; }
inline std::uint16_t rd16(std::span<const std::uint8_t> b, std::uint32_t o) {
    return static_cast<std::uint16_t>((b[o] << 8) | b[o + 1]);
}
inline std::uint64_t rd64(std::span<const std::uint8_t> b, std::uint32_t o) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | b[o + i];
    return v;
}
inline std::string rdstr(std::span<const std::uint8_t> b, std::uint32_t o, std::size_t n) {
    std::size_t len = 0;
    while (len < n && b[o + len] != 0) ++len;
    return std::string(reinterpret_cast<const char*>(&b[o]), len);
}

inline std::uint8_t readRegionByte(std::span<const std::uint8_t> b,
                                   std::uint8_t regionId,
                                   std::uint8_t currentNode,
                                   std::uint16_t byteOff) {
    const std::uint32_t base = (regionId == currentNode)
        ? kOffsetActiveNode
        : kOffsetNodesStart + static_cast<std::uint32_t>(regionId) * 32;
    return b[base + byteOff];
}

struct SwitchKeyEntry {
    std::string_view name;   // DSL item name
    std::uint8_t     regionId;
    std::uint16_t    rawFlag;
};

constexpr std::array<SwitchKeyEntry, 2> kSwitchKeyTable{{
    {"North_Faron_Woods_Gate_Key", 0x02, 0x14},
    {"Faron_Woods_Coro_Key",       0x02, 0x0C},
}};

}  // namespace

QuestState readQuestState(std::span<const std::uint8_t> b, std::uint8_t currentNode) {
    QuestState qs;
    qs.curHealth      = rd16(b, kOffCurHealth);
    qs.maxHealth      = rd16(b, kOffMaxHealth);
    qs.rupees         = rd16(b, kOffRupees);
    qs.curLanternOil  = rd16(b, kOffCurLanternOil);
    qs.maxLanternOil  = rd16(b, kOffMaxLanternOil);
    qs.walletTier     = rd8 (b, kOffWalletTier);
    qs.magicCur       = rd8 (b, kOffMagicCur);
    qs.magicMax       = rd8 (b, kOffMagicMax);
    qs.currentForm    = rd8 (b, kOffCurrentForm);
    qs.transformLevel = rd8 (b, kOffTransformLevel);
    qs.darkClearLevel = rd8 (b, kOffDarkClearLevel);
    qs.faronTears     = rd8 (b, kOffTearsFaron);
    qs.eldinTears     = rd8 (b, kOffTearsEldin);
    qs.lanayruTears   = rd8 (b, kOffTearsLanayru);
    qs.poeSouls       = rd8 (b, kOffPoeSouls);
    qs.spawnPoint     = rd8 (b, kOffSpawnPoint);
    qs.roomId         = rd8 (b, kOffRoomId);
    qs.deaths         = rd16(b, kOffDeathCount);
    qs.totalTimeFrames= rd64(b, kOffTotalTime);
    qs.playerName     = rdstr(b, kOffPlayerName,   16);
    qs.currentStage   = rdstr(b, kOffCurrentStage,  8);

    qs.portals.reserve(kPortalTable.size());
    for (const auto& p : kPortalTable) {
        qs.portals.push_back({p.name, readGetItemFlag(b, p.itemId)});
    }
    qs.switchKeys.reserve(kSwitchKeyTable.size());
    for (const auto& s : kSwitchKeyTable) {
        const auto [byteOff, mask] = decodeSwitchFlag(s.rawFlag);
        const std::uint8_t byte = readRegionByte(b, s.regionId, currentNode, byteOff);
        qs.switchKeys.push_back({s.name, (byte & mask) != 0});
    }
    return qs;
}

}  // namespace tpt::core
