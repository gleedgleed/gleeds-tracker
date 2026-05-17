#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace tpt::memory {

// Source-agnostic interface for reading (and optionally writing) emulator /
// PC-port memory. Concrete implementations:
//   - tpt::dolphin::Client  — attaches to a running Dolphin process.
//
// Future implementations could include a Dusk PC-port reader, a savefile
// reader, or a mock for testing. Code in core/ that decodes save state
// takes a `MemorySource&` so it doesn't depend on the concrete source.
//
// Addresses passed to read/write are console virtual addresses (e.g.
// 0x804061C0 for the US TP save block). The implementation translates to
// whatever its native addressing requires. No byte-swapping is done at this
// layer — multi-byte reads return raw bytes; callers swap.
class MemorySource {
public:
    virtual ~MemorySource() = default;

    // Attempt to attach to the source. Returns true if the source is
    // connected and ready to serve reads after the call.
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    // Short human-readable name of the backing source (e.g. "Dolphin",
    // "Dusk"). Used in the GUI status bar so the user can see which
    // emulator/port the tracker is attached to. Always non-empty.
    virtual std::string sourceName() const = 0;

    // Source identifier for the running game. For Dolphin this is the
    // 6-char GameCube/Wii game ID (e.g. "GZ2E01"). Empty string when no
    // identifier is available.
    virtual std::string gameId() const = 0;

    // Read `size` bytes starting at console address `addr` into `out`.
    // Returns false on failure (not connected, address out of range, ...).
    virtual bool readBytes(std::uint32_t addr, void* out, std::size_t size) const = 0;

    // Write `size` bytes from `data` to console address `addr`. Returns
    // false on failure. Optional capability — sources may always return
    // false if writing isn't supported.
    virtual bool writeBytes(std::uint32_t addr, const void* data, std::size_t size) const = 0;

    // Optional periodic refresh. The poll loop calls this at ~poll
    // cadence. Sources can override for ongoing source-side housekeeping
    // — e.g., DuskSource uses it to re-evaluate which save buffer is the
    // live one based on observed writes. Default is a no-op.
    virtual void tick() {}
};

}  // namespace tpt::memory
