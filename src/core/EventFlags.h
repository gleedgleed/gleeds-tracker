#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

namespace tpt::core {

// dSv_event_c at SAVE+0x7F0: 256 bytes / 2048 single-bit event flags.
// libtp encoding of an EventFlags constant: (byte_offset << 8) | bit_mask.
inline constexpr std::uint32_t kOffsetEventBlock   = 0x7F0;

// dSv_player_get_item_c.mItemsFlags[8] at SAVE+0x0CC: 32 bytes / 256 bits,
// one per GC item ID. Each bit = "received this item ID at least once."
inline constexpr std::uint32_t kOffsetGetItemFlags = 0x0CC;

struct EventFlagEntry {
    std::string_view name;
    std::uint16_t    raw;     // libtp encoding
};

struct GetItemFlagEntry {
    std::string_view name;
    std::uint8_t     itemId;  // GC item ID (0x00..0xFF)
};

// Curated subset of libtp::data::flags::EventFlags. Iteration order is stable.
// Returned as spans so callers don't depend on exact entry counts.
std::span<const EventFlagEntry>   eventFlagTable();
std::span<const GetItemFlagEntry> getItemFlagTable();

constexpr std::pair<std::uint16_t, std::uint8_t> decodeEventFlag(std::uint16_t raw) {
    return {static_cast<std::uint16_t>(raw >> 8),
            static_cast<std::uint8_t>(raw & 0xFF)};
}

bool readEventFlag(std::span<const std::uint8_t> block, std::uint16_t raw);
bool readGetItemFlag(std::span<const std::uint8_t> block, std::uint8_t itemId);

}  // namespace tpt::core
