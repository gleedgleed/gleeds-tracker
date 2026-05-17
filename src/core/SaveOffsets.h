#pragma once

#include <cstddef>
#include <cstdint>

namespace tpt::core {

// The full save block lives at SAVE_FILE_ADDR (region-dependent). All file
// offsets in items.py / quest_state.py / event_flags.py are bytes relative
// to that base. Reading this much in one Dolphin call is much cheaper than
// per-field reads.
inline constexpr std::size_t   kSaveBlockSize     = 0x1800;

// Per-dungeon item blocks: 32 bytes each, indexed by node ID.
inline constexpr std::uint32_t kOffsetNodesStart  = 0x1F0;
// "Currently-loaded dungeon" buffer — the active node's items live here
// instead of in its parked slot.
inline constexpr std::uint32_t kOffsetActiveNode  = 0x958;
// Single byte: ID of the node currently loaded (0xFF = overworld).
inline constexpr std::uint32_t kOffsetCurrentNode = 0x978;

}  // namespace tpt::core
