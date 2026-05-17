#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace tpt::memory {

// AOB (array-of-byte) pattern with per-byte wildcards. Format: hex byte
// pairs separated by whitespace; `??` is a single-byte wildcard. Example:
//   "48 8D 05 ?? ?? ?? ?? 48 89 ?? ??"
// matches a LEA + MOV pair where the LEA's disp32 and the MOV's ModR/M +
// disp32 vary.
struct Pattern {
    std::vector<std::uint8_t> bytes;
    std::vector<bool> wildcards;
    std::size_t size() const noexcept { return bytes.size(); }
};

// Throws std::invalid_argument on malformed input.
Pattern parsePattern(std::string_view spec);

// Find the first match of `p` in `buf`. Returns offset into `buf`, or
// nullopt if no match.
std::optional<std::size_t> findPattern(std::span<const std::uint8_t> buf,
                                       const Pattern& p);

// Find every match. Useful when the pattern is ambiguous and we want to
// pick the "right" one by additional criteria (e.g. the target address
// landing in .data).
std::vector<std::size_t> findAllPatterns(std::span<const std::uint8_t> buf,
                                         const Pattern& p);

// Decode an x86-64 RIP-relative displacement at `instrAddr + dispOffset`
// (4 bytes, signed). Returns the absolute address the instruction targets:
//   target = instrAddr + instrLength + sign_extend(disp32)
// `disp` must point to the 4 disp bytes themselves. Used after a pattern
// match to recover what address a LEA/MOV [rip+...] is loading.
std::uintptr_t decodeRipRel32(std::uintptr_t instrAddr,
                              std::size_t instrLength,
                              const std::uint8_t* disp);

}  // namespace tpt::memory
