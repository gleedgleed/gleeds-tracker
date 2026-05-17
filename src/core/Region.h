#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace tpt::memory { class MemorySource; }

namespace tpt::core {

struct Region {
    std::string_view id4;     // 4-byte game-ID prefix at 0x80000000 (e.g. "GZ2E")
    std::string_view name;    // "US" / "EU" / "JP"
    std::uint32_t    saveAddr;
};

inline constexpr std::array<Region, 3> kRegions{{
    {"GZ2E", "US", 0x804061C0u},
    {"GZ2P", "EU", 0x80408160u},
    {"GZ2J", "JP", 0x80400300u},
}};

// Reads 4 bytes at 0x80000000 and matches against kRegions. Returns nullopt
// if not connected or if the prefix matches no known TP build.
std::optional<Region> detectRegion(const tpt::memory::MemorySource& mem);

}  // namespace tpt::core
