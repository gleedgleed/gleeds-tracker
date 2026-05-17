#pragma once

#include "memory/MemorySource.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tpt::dusk {

// One match of a LEA-RIP32 instruction whose disp32 resolves to a target
// address (typically gameInfoAddr in the DuskProbeInfo this belongs to).
// `bytes` is up to 32 bytes starting at the LEA instruction, for picking
// a stable AOB pattern.
struct LeaRefMatch {
    std::uintptr_t addr = 0;
    std::array<std::uint8_t, 32> bytes{};
    std::size_t bytesLen = 0;
};

// Snapshot of what we know about a running dusklight process. Used by the
// --dusk-probe CLI to surface candidate scan patterns. Empty fields mean
// "not located yet".
struct DuskProbeInfo {
    unsigned long pid = 0;
    void*         process = nullptr;     // HANDLE, caller closes
    std::uintptr_t moduleBase = 0;
    std::size_t    moduleSize = 0;
    std::uintptr_t textBase   = 0;
    std::size_t    textSize   = 0;
    // Resolved via DbgHelp + the locally-built Dusk's PDB. Zero if no
    // symbols available (production end-users won't have these).
    std::uintptr_t gameInfoAddr  = 0;
    std::uintptr_t saveBlockAddr = 0;
    // First N RIP-relative LEA instructions in .text whose disp32 targets
    // gameInfoAddr. Populated only when gameInfoAddr is known.
    std::vector<LeaRefMatch> leaMatches;
    // 512 bytes starting at gameInfoAddr — for inspecting layout to derive
    // the offset to dSv_save_c. Empty if gameInfoAddr is zero.
    std::vector<std::uint8_t> gameInfoDump;
    // What the production AOB scanner found (independent of PDB). When
    // both this and gameInfoAddr are populated they should agree — that's
    // the self-test that confirms the pattern still works.
    std::uintptr_t aobGameInfoAddr = 0;
    // What the production content-based scanner picked as the live save
    // (after temporal-change disambiguation among multiple validator-
    // passing candidates).
    std::uintptr_t contentSaveBlockAddr = 0;
    // 512 bytes at contentSaveBlockAddr (parallel to gameInfoDump,
    // which is at gameInfoAddr/aobGameInfoAddr). Use to compare the
    // two candidates and determine which one is the *live* save —
    // gameplay-mutated fields (rupees, health, position) will diverge
    // between the live copy and any stale mirror.
    std::vector<std::uint8_t> contentDump;
    // All addresses in the target process whose bytes passed
    // looksLikeSaveBlock(). Dusk keeps several save-shaped buffers in
    // memory (save slots, write-out staging, snapshot mirrors); only one
    // is being mutated by gameplay. contentLocateSaveBlock() picks the
    // mutating one; this list exposes the others to the probe so the
    // user can see the multiplicity.
    std::vector<std::uintptr_t> contentCandidates;
    // First 16 bytes from each candidate (parallel to contentCandidates).
    // Covers maxHealth/curHealth/rupees/lanternOil — enough to spot
    // which candidate is the live save and which are mirrors / staging
    // buffers. Useful for understanding when "save to file" flushes a
    // staging buffer (its rupees etc. catch up to the live one).
    std::vector<std::vector<std::uint8_t>> contentCandidateHeads;
};

// Open + probe a running dusklight.exe. Releases nothing on success — the
// returned HANDLE is the caller's to close. Returns nullopt if the process
// isn't running or OpenProcess fails. Symbol resolution is attempted but
// not required; missing symbols leave gameInfoAddr/saveBlockAddr zero.
std::optional<DuskProbeInfo> probeDusk();

// Reads memory from a running Dusklight process (TwilitRealm's PC port of
// Twilight Princess). Dusk is a source-level recompilation of TP: the
// in-memory layout of dSv_save_c, the event flag blocks, etc. is the same
// as on GameCube. Only the *base* address differs — in Dusk these objects
// are normal C++ globals subject to ASLR, not at fixed GC virtual addresses.
//
// Translation strategy ("approach A" from the design discussion):
//   connect() signature-scans the Dusk process for the save block,
//   captures its actual address, and on every subsequent readBytes(addr,
//   ...) translates a GC virtual address (e.g. 0x804061C0 — the US save
//   block) to (saveBlockAddr_ + (addr - gcSaveBlockVA_)). All existing
//   core/ decoders keep working unchanged: they speak GC virtual addresses
//   regardless of source.
//
// Addresses outside the save block aren't currently translatable (we don't
// know where Dusk's globals other than dSv_save_c live). Reads outside
// [gcSaveBlockVA_, gcSaveBlockVA_ + kSaveBlockSize) return false. The
// notable caveat: the TPR seed-header scanner sweeps a large GC RAM range
// for "TPR" magic — that's meaningless on Dusk (no rando today) so it
// will simply find nothing.
//
// Build target: Windows. The cpp body is guarded with #ifdef _WIN32 so the
// file still compiles on other platforms (all methods return false /
// not-connected on non-Windows builds).
class DuskSource : public tpt::memory::MemorySource {
public:
    DuskSource();
    ~DuskSource() override;
    DuskSource(const DuskSource&) = delete;
    DuskSource& operator=(const DuskSource&) = delete;

    // MemorySource interface
    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;
    std::string sourceName() const override { return "Dusk"; }
    std::string gameId() const override;
    bool readBytes(std::uint32_t addr, void* out, std::size_t size) const override;
    bool writeBytes(std::uint32_t addr, const void* data, std::size_t size) const override;
    void tick() override;

    // Quick "is there a dusklight.exe running right now?" check — used by
    // the source factory's auto-detect path without paying the cost of
    // OpenProcess + signature scan.
    static bool isAvailable();

private:
    // Windows process HANDLE. Held as void* to keep <windows.h> out of the
    // header. Null when not connected.
    void* process_ = nullptr;

    // Currently-selected process-local address of the dSv_save_c instance.
    // Set by connect() (initial best guess: highest-addressed candidate)
    // and re-evaluated by tick() based on which candidate last changed —
    // the live save is the one the game is writing to, which manifests
    // as the most-recently-changed bytes among all candidates.
    std::uintptr_t saveBlockAddr_ = 0;

    // Pool of all save-block-shaped buffers in Dusk's memory. Populated
    // once during connect() via content scan + comprehensive validator.
    // Dusk keeps multiple real save buffers (live save + save-event
    // snapshots + other save slots) — they're indistinguishable at
    // startup, so we keep them all and let tick() pick the live one
    // based on observed writes.
    std::vector<std::uintptr_t> candidates_;

    // Last-observed contents of each candidate (parallel to candidates_).
    // Compared against fresh reads in tick() to detect writes.
    std::vector<std::vector<std::uint8_t>> lastSnapshots_;

    // tickCounter_ value at the last time each candidate's bytes changed.
    // The candidate with the largest value is currently the most-recently-
    // updated one (= live save). Ties broken by highest address.
    std::vector<std::uint64_t> lastChangedTick_;
    std::uint64_t tickCounter_ = 0;
    std::chrono::steady_clock::time_point lastTickWall_{};

    // The GC virtual address callers will pass to readBytes() that should
    // map to saveBlockAddr_. Currently fixed to the US TP save address
    // (0x804061C0) — we'd extend to handle EU/JP once we can detect which
    // dump Dusk has loaded.
    std::uint32_t gcSaveBlockVA_ = 0;

    // Synthesized 6-char game ID, matched to whichever region we detected
    // (e.g. "GZ2E01" for US). Empty when not connected.
    std::string gameId_;

    // Translate a GC virtual address into the process-local equivalent.
    // Returns nullopt for addresses that don't fall within a region we
    // know how to map.
    std::optional<std::uintptr_t> translate(std::uint32_t gcAddr,
                                            std::size_t size) const;
};

}  // namespace tpt::dusk
