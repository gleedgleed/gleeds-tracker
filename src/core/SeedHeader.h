#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace tpt::memory { class MemorySource; }

namespace tpt::core {

// GC main RAM bounds.
inline constexpr std::uint32_t kMemBase   = 0x80000000u;
inline constexpr std::uint32_t kMemEnd    = 0x81800000u;  // 24 MB
inline constexpr std::uint32_t kScanChunk = 0x10000u;     // 64 KB per IPC

inline constexpr std::size_t   kSeedHeaderSize        = 0x9A;
inline constexpr std::size_t   kSeedHeaderSizeWebgen  = 0x160;

struct SeedFlag {
    std::string name;
    bool value = false;
};

struct SeedSettings {
    // Identity.
    std::string seedName;
    std::uint16_t versionMajor = 0;
    std::uint16_t versionMinor = 0;
    std::uint16_t headerSize   = 0;
    std::uint16_t dataSize     = 0;
    std::uint32_t totalSize    = 0;

    // Settings (raw byte + named string).
    std::uint8_t castleRequirementsRaw    = 0;
    std::string  castleRequirements;
    std::uint8_t palaceRequirementsRaw    = 0;
    std::string  palaceRequirements;
    std::uint8_t mapClearBits             = 0;
    std::vector<SeedFlag> mapClearFlags;     // insertion order
    std::uint8_t damageMagnificationRaw   = 0;
    std::string  damageMagnification;
    std::uint8_t totSwordRequirementRaw   = 0;
    std::uint8_t totEntranceTier          = 0;   // 0..4
    std::uint8_t mirrorChamberEntranceRaw = 0;
    std::string  mirrorChamberEntrance;
    std::uint8_t castleRequirementCount    = 0;
    std::uint8_t castleBkRequirementsRaw   = 0;
    std::string  castleBkRequirements;
    std::uint8_t castleBkRequirementCount  = 0;
    std::uint8_t walletSizeRaw             = 0;
    std::string  walletSize;
    std::uint16_t maloShopDonation         = 0;

    // Bitfield sections (insertion-ordered for stable display).
    std::vector<SeedFlag> seedFlags;          // SeedEnabledFlag bits
    std::vector<SeedFlag> volatilePatches;    // volatilePatchInfo bits

    std::uint32_t foundAt = 0;                // address where TPR magic was found
};

// Decode a buffer that already contains a TPR seed header (>= kSeedHeaderSize bytes).
SeedSettings decodeSeedHeader(std::span<const std::uint8_t> headerBytes,
                              std::uint32_t foundAt = 0);

// Scan main RAM for the "TPR" magic and validate. Returns the absolute address.
std::optional<std::uint32_t> scanForSeedHeader(const tpt::memory::MemorySource& mem);

// Top-level: scan, decode header, also fill seedFlags + volatilePatches.
// Returns nullopt if no header was found.
std::optional<SeedSettings> readSeedSettings(const tpt::memory::MemorySource& mem);

}  // namespace tpt::core
