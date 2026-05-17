#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/CheckPlacements.h"

namespace tpt::core {

// Kind of save-state structure a binding addresses.
//   Region  — per-region 32-byte flag block at NODES_START + region_id*32
//   Flag    — global save offset (e.g. dSv_event_c at SAVE+0x7F0)
//   Event   — virtual / unresolvable via bit; status comes from elsewhere
//             (boss defeats, placement-based completion, etc.)
enum class SaveBindingType {
    Region,
    Flag,
    Event,
    Unknown,
};

// Save-bit binding for a single check: where in the save block its
// completion bit lives. Joined by `name` to the web-gen Check graph at use
// sites — that graph owns categorization and stage assignment, so this
// struct stays narrowly focused on save-state addressing.
struct CheckSaveBinding {
    std::string name;        // canonical check name (matches webgen Check name)
    SaveBindingType type = SaveBindingType::Unknown;
    std::optional<std::uint8_t>  region;   // node ID; only set for Region type
    std::optional<std::uint16_t> offset;   // byte offset; absent => unresolvable
    std::optional<std::uint8_t>  bit;      // single-bit mask; absent => unresolvable
};

using CheckSaveBindings = std::unordered_map<std::string, CheckSaveBinding>;

// Load data/check_save_bindings.json. Returns empty on missing file (warning logged).
CheckSaveBindings loadCheckSaveBindings(const std::filesystem::path& jsonPath);

// Test a single check's completion bit. Returns:
//   true  -> bit is set in the save block
//   false -> bit is not set
//   nullopt -> entry has no resolvable offset/bit (Event-only / unknown layout)
std::optional<bool> isCheckComplete(const CheckSaveBinding& e,
                                    std::span<const std::uint8_t> save,
                                    std::uint8_t currentNode);

// Convenience: build the set of completed check names from the full table.
//
// `placements` (optional) maps check names to GC item IDs as placed by the
// current seed. For checks not resolvable via the binding bit (rupees,
// freestanding pickups, etc.), the function falls back to reading the
// item's global "first-bit" at SAVE+0x0CC — set when the rando hands the
// item out, persistent across save/load, uniquely tied to the item (and
// thus to the check that placed it, for items unique in the seed).
std::unordered_set<std::string> completedCheckSet(
    const CheckSaveBindings& bindings,
    std::span<const std::uint8_t> save,
    std::uint8_t currentNode,
    const SeedPlacements& placements = {});

}  // namespace tpt::core
