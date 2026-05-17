#pragma once

#include "memory/MemorySource.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tpt::p64 {

// Snapshot of what we know about a running Project64 process. Surface for
// the --p64-probe dev tool. Empty / zero fields mean "not located yet".
struct ProbeInfo {
    unsigned long pid       = 0;
    void*         process   = nullptr;        // HANDLE, caller closes
    std::uintptr_t moduleBase = 0;
    std::size_t    moduleSize = 0;

    // Fast-path: read+validate at each known RDRAM base address.
    // Parallel arrays. `validatedFastPath` is the first base that
    // successfully matched the validator, or zero if none matched.
    std::vector<std::uintptr_t> fastPathCandidates;
    std::vector<bool>           fastPathValidated;
    std::uintptr_t              validatedFastPath = 0;

    // VirtualQueryEx-driven fallback: every committed region whose
    // +0x11A5EC offset matches the byte-swapped "ZELD" magic.
    std::vector<std::uintptr_t> scannedCandidates;

    // Diagnostic counters from the VirtualQueryEx walk. Useful to confirm
    // the walk reached the right address range when the validator fails
    // everywhere.
    std::size_t    scanRegionsTotal     = 0;  // every iteration of the walk
    std::size_t    scanRegionsCommitted = 0;  // MEM_COMMIT subset
    std::uintptr_t scanHighestAddr      = 0;  // last addr the walk reached
    std::size_t    scanAllocBasesTested = 0;  // unique AllocationBases tried

    // Cartridge ID read from the validated RDRAM base + 0x3B-0x3E. Empty
    // when no validated base was found. Typical OoT NTSC v1.0 = "NZSE".
    std::string cartId;
    // Internal ROM name at +0x20..+0x33 (20 bytes, ASCII, trailing spaces).
    std::string internalRomName;

    // First 64 bytes from the validated RDRAM base. Lets the user
    // eyeball-check the full ROM header. Empty when no base was found.
    std::vector<std::uint8_t> headerDump;
};

// Probe a running project64.exe. Caller owns and closes `process`. Returns
// nullopt if the process isn't running or OpenProcess fails. Independent of
// the live Source — runs even when Source::connect() can't find RDRAM, so
// users can diagnose what's wrong.
std::optional<ProbeInfo> probeProject64();

// Reads memory from a running Project64 process. Project64 emulates an N64,
// which has 4 MB of RDRAM mapped at console virtual addresses 0x80000000
// (KSEG0, cached) and 0xA0000000 (KSEG1, uncached). Both alias to the same
// physical RDRAM. The 8 MB Expansion Pak extends this to 0x807FFFFF /
// 0xA07FFFFF; tracker reads stay below 0x801FFFFF in practice.
//
// Translation strategy: connect() locates Project64's 32 MB RDRAM allocation
// in the emulator's process address space, captures its base, and on every
// readBytes(n64Addr, …) translates `n64Addr` to
// `rdramBase_ + (n64Addr & 0x1FFFFFFF)`. The mask matches what Project64
// itself does internally (see project64-develop/Source/Project64-core/
// N64System/Mips/MemoryVirtualMem.cpp:62).
//
// RDRAM location strategy:
//   1. Try three known historical Project64 RDRAM bases. Validate by
//      reading the z64 endianness magic (0x80 0x37 0x12 0x40) at the
//      base + 0x00 and the "NZ" cartridge prefix at +0x3B..+0x3C.
//   2. If none match, walk VirtualQueryEx committed regions ≥ 32 MB and
//      try the same validator on each.
//   3. Give up. Source remains in disconnected state until the user starts
//      Project64 and loads an OoT-family ROM.
//
// Build target: Windows. The .cpp body is guarded with `#ifdef _WIN32` so
// the file still compiles on other platforms (all methods return
// false / not-connected on non-Windows builds).
class Source : public tpt::memory::MemorySource {
public:
    Source();
    ~Source() override;
    Source(const Source&)            = delete;
    Source& operator=(const Source&) = delete;

    // MemorySource interface
    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;
    std::string sourceName() const override { return "Project64"; }
    std::string gameId()     const override { return gameId_; }
    bool readBytes(std::uint32_t addr, void* out, std::size_t size) const override;
    bool writeBytes(std::uint32_t addr, const void* data, std::size_t size) const override;
    // tick() is intentionally not overridden — Project64's RDRAM is a single
    // contiguous allocation with no "which buffer is live" ambiguity, so
    // periodic resampling isn't needed at this layer.

    // Quick "is there a project64.exe running right now?" check. Used by
    // the factory's auto-detect path without paying the cost of
    // OpenProcess + RDRAM scan.
    static bool isAvailable();

private:
    // Windows process HANDLE. Held as void* to keep <windows.h> out of the
    // header. Null when not connected.
    void* process_ = nullptr;

    // Process-local address of Project64's m_RDRAM[0]. Set by connect()
    // after the fast-path or the VirtualQueryEx fallback validates a
    // base. Zero when not connected.
    std::uintptr_t rdramBase_ = 0;

    // Synthesized 4-char N64 cartridge ID read from the ROM header at
    // RDRAM + 0x3B..+0x3E (e.g. "NZSE" for OoT NTSC v1.0). Empty when not
    // connected.
    std::string gameId_;

    // Translate an N64 virtual address into the process-local equivalent.
    // Accepts KSEG0 (0x80000000+) and KSEG1 (0xA0000000+). Returns nullopt
    // when the address falls outside the 32 MB RDRAM window or the read
    // would cross the end.
    std::optional<std::uintptr_t> translate(std::uint32_t n64Addr,
                                            std::size_t size) const;
};

}  // namespace tpt::p64
