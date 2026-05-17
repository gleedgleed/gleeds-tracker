#include "core/SeedHeader.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <utility>

#include "memory/MemorySource.h"

namespace tpt::core {

namespace {

constexpr std::array<std::pair<int, std::string_view>, 7> kCastleReqMap{{
    {0, "Open"}, {1, "Fused_Shadows"}, {2, "Mirror_Shards"}, {3, "Dungeons"},
    {4, "Vanilla"}, {5, "Poe_Souls"}, {6, "Hearts"},
}};
constexpr std::array<std::pair<int, std::string_view>, 6> kCastleBkReqMap{{
    {0, "None"}, {1, "Fused_Shadows"}, {2, "Mirror_Shards"}, {3, "Dungeons"},
    {4, "Poe_Souls"}, {5, "Hearts"},
}};
constexpr std::array<std::pair<int, std::string_view>, 4> kPalaceReqMap{{
    {0, "Open"}, {1, "Fused_Shadows"}, {2, "Mirror_Shards"}, {3, "Vanilla"},
}};
constexpr std::array<std::pair<int, std::string_view>, 4> kWalletSizeMap{{
    {0, "Reduced"}, {1, "Vanilla"}, {2, "HD"}, {3, "Large"},
}};
constexpr std::array<std::pair<int, std::string_view>, 6> kDamageMagMap{{
    {1, "Vanilla"}, {2, "Double"}, {3, "Triple"}, {4, "Quadruple"},
    {5, "OHKO"}, {80, "OHKO_internal"},
}};
constexpr std::array<std::pair<int, std::string_view>, 3> kMirrorChamberMap{{
    {0, "Open"}, {1, "Barrier"}, {2, "Closed"},
}};

struct MapClearEntry { std::string_view name; std::uint8_t mask; };
constexpr std::array<MapClearEntry, 6> kMapClearTable{{
    {"skipSnowpeakEntrance",   0x40},
    {"openMap",                0x20},
    {"lanayruTwilightCleared", 0x10},
    {"eldinTwilightCleared",   0x08},
    {"faronTwilightCleared",   0x04},
    {"skipPrologue",           0x02},
}};

constexpr std::array<std::string_view, 10> kSeedEnabledFlagNames{
    "TRANSFORM_ANYWHERE", "QUICK_TRANSFORM", "INCREASE_SPINNER_SPEED",
    "BONKS_DO_DAMAGE", "AUTOFILL_WALLETS", "MODIFY_SHOP_MODELS",
    "RAINBOW_LANTERN", "RAINBOW_MIDNA", "RAINBOW_LIGHT_SWORD",
    "LIGHT_SWORD_ALWAYS_ON",
};

constexpr std::array<std::string_view, 6> kVolatilePatchNames{
    "faronTwilightCleared", "eldinTwilightCleared", "lanayruTwilightCleared",
    "skipMinorCutscenes", "skipMdh", "openMap",
};

template <std::size_t N>
std::string mapEnum(const std::array<std::pair<int, std::string_view>, N>& table, int v) {
    for (const auto& [k, name] : table) if (k == v) return std::string(name);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "?(%d)", v);
    return std::string(buf);
}

inline std::uint8_t  rd8 (std::span<const std::uint8_t> b, std::uint32_t o) { return b[o]; }
inline std::uint16_t rd16(std::span<const std::uint8_t> b, std::uint32_t o) {
    return static_cast<std::uint16_t>((b[o] << 8) | b[o + 1]);
}
inline std::uint32_t rd32(std::span<const std::uint8_t> b, std::uint32_t o) {
    return (std::uint32_t(b[o]) << 24) | (std::uint32_t(b[o + 1]) << 16) |
           (std::uint32_t(b[o + 2]) << 8) | std::uint32_t(b[o + 3]);
}
inline std::string rdAscii(std::span<const std::uint8_t> b, std::uint32_t o, std::size_t n) {
    std::size_t len = 0;
    while (len < n && b[o + len] != 0) ++len;
    return std::string(reinterpret_cast<const char*>(&b[o]), len);
}

std::uint8_t totSwordIdToTier(std::uint8_t id) {
    switch (id) {
        case 0xFF: return 0;  // Gives_Vanilla
        case 0x3F: return 1;  // Wooden_Sword
        case 0x28: return 2;  // Ordon_Sword
        case 0x29: return 3;  // Master_Sword
        case 0x49: return 4;  // Master_Sword_Light
        default:   return 0;
    }
}

bool isPlausibleHeader(std::span<const std::uint8_t> buf, std::size_t off) {
    if (off + kSeedHeaderSize > buf.size()) return false;
    const std::uint16_t verMajor = rd16(buf, static_cast<std::uint32_t>(off + 0x24));
    const std::uint16_t headerSize = rd16(buf, static_cast<std::uint32_t>(off + 0x28));
    if (verMajor == 0 || verMajor > 100) return false;
    if (headerSize < kSeedHeaderSize || headerSize > 0x200) return false;
    // seedName at +0x03 should be printable ASCII or 0 padding.
    for (std::size_t i = 0; i < 33; ++i) {
        const std::uint8_t c = buf[off + 0x03 + i];
        if (c == 0) break;
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

std::optional<std::size_t> findInBuffer(std::span<const std::uint8_t> buf) {
    static constexpr std::uint8_t magic[3] = {'T', 'P', 'R'};
    if (buf.size() < 3) return std::nullopt;
    for (std::size_t i = 0; i + 3 <= buf.size(); ++i) {
        if (buf[i] == magic[0] && buf[i + 1] == magic[1] && buf[i + 2] == magic[2]) {
            if (isPlausibleHeader(buf, i)) return i;
        }
    }
    return std::nullopt;
}

std::vector<SeedFlag> readBitfield(const tpt::memory::MemorySource& mem,
                                   std::uint32_t seedDataStart,
                                   std::uint16_t dataOffset,
                                   std::uint16_t numEntries,
                                   std::span<const std::string_view> names) {
    std::vector<SeedFlag> out;
    if (numEntries == 0) return out;
    std::uint8_t bits[16] = {};
    if (!mem.readBytes(seedDataStart + dataOffset, bits, sizeof(bits))) return out;

    const std::size_t cap = std::min<std::size_t>(numEntries, names.size());
    out.reserve(cap);
    for (std::size_t i = 0; i < cap; ++i) {
        const std::size_t wordIdx = i / 32;
        const std::size_t bitInWord = i % 32;
        const std::size_t logicalByte = bitInWord / 8;
        const std::size_t physicalByte = wordIdx * 4 + (3 - logicalByte);  // ReverseBytes(4)
        if (physicalByte >= sizeof(bits)) break;
        const std::uint8_t mask = static_cast<std::uint8_t>(0x80u >> (bitInWord % 8));
        out.push_back({std::string(names[i]), (bits[physicalByte] & mask) != 0});
    }
    return out;
}

}  // namespace

SeedSettings decodeSeedHeader(std::span<const std::uint8_t> b, std::uint32_t foundAt) {
    SeedSettings s;
    s.seedName              = rdAscii(b, 0x03, 33);
    s.versionMajor          = rd16(b, 0x24);
    s.versionMinor          = rd16(b, 0x26);
    s.headerSize            = rd16(b, 0x28);
    s.dataSize              = rd16(b, 0x2A);
    s.totalSize             = rd32(b, 0x2C);
    s.maloShopDonation      = rd16(b, 0x8E);
    s.castleRequirementsRaw    = rd8(b, 0x90);
    s.palaceRequirementsRaw    = rd8(b, 0x91);
    s.mapClearBits             = rd8(b, 0x92);
    s.damageMagnificationRaw   = rd8(b, 0x93);
    s.totSwordRequirementRaw   = rd8(b, 0x94);
    s.mirrorChamberEntranceRaw = rd8(b, 0x95);
    s.castleRequirementCount   = rd8(b, 0x96);
    s.castleBkRequirementsRaw  = rd8(b, 0x97);
    s.castleBkRequirementCount = rd8(b, 0x98);
    s.walletSizeRaw            = rd8(b, 0x99);
    s.foundAt = foundAt;

    s.castleRequirements     = mapEnum(kCastleReqMap,     s.castleRequirementsRaw);
    s.palaceRequirements     = mapEnum(kPalaceReqMap,     s.palaceRequirementsRaw);
    s.damageMagnification    = mapEnum(kDamageMagMap,     s.damageMagnificationRaw);
    s.mirrorChamberEntrance  = mapEnum(kMirrorChamberMap, s.mirrorChamberEntranceRaw);
    s.castleBkRequirements   = mapEnum(kCastleBkReqMap,   s.castleBkRequirementsRaw);
    s.walletSize             = mapEnum(kWalletSizeMap,    s.walletSizeRaw);
    s.totEntranceTier        = totSwordIdToTier(s.totSwordRequirementRaw);

    s.mapClearFlags.reserve(kMapClearTable.size());
    for (const auto& e : kMapClearTable) {
        s.mapClearFlags.push_back({std::string(e.name), (s.mapClearBits & e.mask) != 0});
    }
    return s;
}

std::optional<std::uint32_t> scanForSeedHeader(const tpt::memory::MemorySource& mem) {
    if (!mem.isConnected()) return std::nullopt;

    std::vector<std::uint8_t> chunk(kScanChunk);
    std::uint32_t addr = kMemBase;
    while (addr < kMemEnd) {
        const std::uint32_t n = std::min(kScanChunk, kMemEnd - addr);
        if (!mem.readBytes(addr, chunk.data(), n)) {
            addr += n;
            continue;
        }
        const auto idx = findInBuffer({chunk.data(), n});
        if (idx) return addr + static_cast<std::uint32_t>(*idx);
        // Step forward but keep a small overlap so a magic split across the
        // chunk boundary still gets caught next iteration.
        addr += n - static_cast<std::uint32_t>(kSeedHeaderSize);
    }
    return std::nullopt;
}

std::optional<SeedSettings> readSeedSettings(const tpt::memory::MemorySource& mem) {
    const auto found = scanForSeedHeader(mem);
    if (!found) return std::nullopt;

    std::vector<std::uint8_t> headerBytes(kSeedHeaderSize);
    if (!mem.readBytes(*found, headerBytes.data(), headerBytes.size())) return std::nullopt;

    SeedSettings s = decodeSeedHeader(headerBytes, *found);
    const std::uint32_t dataStart = *found +
        (s.headerSize ? s.headerSize : static_cast<std::uint16_t>(kSeedHeaderSizeWebgen));

    // flagBitfieldInfo at header offsets 0x38 (numEntries) / 0x3A (dataOffset).
    s.seedFlags = readBitfield(mem, dataStart,
        rd16(headerBytes, 0x3A), rd16(headerBytes, 0x38),
        kSeedEnabledFlagNames);
    // volatilePatchInfo at 0x30 / 0x32.
    s.volatilePatches = readBitfield(mem, dataStart,
        rd16(headerBytes, 0x32), rd16(headerBytes, 0x30),
        kVolatilePatchNames);

    return s;
}

}  // namespace tpt::core
