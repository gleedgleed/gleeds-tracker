#include "p64/Project64Source.h"

#include <algorithm>
#include <array>
#include <cstring>

#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#pragma comment(lib, "psapi.lib")
#endif

namespace tpt::p64 {

namespace {

constexpr const char* kProcessName       = "project64.exe";
constexpr const char* kProcessNameAlt    = "Project64.exe";

// Project64 allocates RDRAM as one 32 MB chunk (0x02000000 bytes — see
// MemoryVirtualMem.cpp:116). Windows VirtualAlloc has historically landed
// it at one of three high-memory addresses across Project64 versions.
constexpr std::uintptr_t kKnownBases[] = {
    0xDFE40000,
    0xDFE70000,
    0xDFFB0000,
};
constexpr std::size_t kRdramSize = 0x02000000;  // 32 MB

// "ZELDAZ" save-loaded magic. Lives in gSaveContext.newf[0..5] at N64
// virtual address gSaveContext+0x1C (OoT NTSC 1.0: 0x8011A5EC, physical
// 0x0011A5EC). Written by the game when a save file is loaded and held
// there throughout play.
//
// Why byte-swapped: Project64 stores N64 RDRAM with the four bytes of
// each 32-bit word in reverse order, so the dynarec'd x86 little-endian
// loads naturally yield MIPS big-endian values. The N64-native ASCII
// "ZELD" (5A 45 4C 44) therefore appears in P64's m_RDRAM as 44 4C 45 5A
// ("DLEZ" as bytes). The validator works in raw P64-memory order — the
// per-word byte-swap is reversed for general reads inside readBytes().
//
// This validator only succeeds when a save is loaded — attach won't
// fire on the title screen. Acceptable: the GUI's retry loop just
// keeps trying until a save loads.
constexpr std::array<std::uint8_t, 4> kSaveLoadedMagicSwapped{
    0x44, 0x4C, 0x45, 0x5A,  // "ZELD" with the per-word byte-swap applied
};
constexpr std::uint32_t kSaveLoadedMagicOffset = 0x11A5EC;
constexpr std::size_t   kHeaderDumpSize        = 0x40;  // bytes shown by --p64-probe

// N64 KSEG0/KSEG1 → physical translation. Both segments alias to the same
// physical RDRAM; the mask strips the segment bits. Matches Project64's
// internal map (MemoryVirtualMem.cpp:62).
constexpr std::uint32_t kN64PhysMask = 0x1FFFFFFF;
// Bounds for a valid RDRAM read: physical address must be < RDRAM size.
// We're not modeling the cartridge bus, RCP registers, or other N64
// hardware — only RDRAM. Reads outside that range return false.
constexpr std::uint32_t kRdramPhysLimit = 0x02000000;  // 32 MB safe upper bound

#ifdef _WIN32

// Walk the process list looking for any of the candidate names. Returns 0
// if none found. Case-insensitive — Windows process filenames in the
// snapshot can be either casing depending on how the user launched.
DWORD findProject64Pid() {
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32 pe{};
    pe.dwSize = sizeof(pe);
    DWORD found = 0;
    if (::Process32First(snap, &pe)) {
        do {
            if (::_stricmp(pe.szExeFile, kProcessName) == 0 ||
                ::_stricmp(pe.szExeFile, kProcessNameAlt) == 0) {
                found = pe.th32ProcessID;
                break;
            }
        } while (::Process32Next(snap, &pe));
    }
    ::CloseHandle(snap);
    return found;
}

// Module enumeration for the main executable. Caller passes the target
// PID; returns (base, size) of project64.exe inside its own process.
// Both fields zero on failure. Used for diagnostic output only — RDRAM
// is allocated via VirtualAlloc and lives outside any module.
bool getProcessModule(DWORD pid, std::uintptr_t& outBase, std::size_t& outSize) {
    outBase = 0;
    outSize = 0;
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32 me{};
    me.dwSize = sizeof(me);
    bool got = false;
    if (::Module32First(snap, &me)) {
        // First entry is the .exe itself.
        outBase = reinterpret_cast<std::uintptr_t>(me.modBaseAddr);
        outSize = me.modBaseSize;
        got = true;
    }
    ::CloseHandle(snap);
    return got;
}

// Read `size` bytes from process `h` at process-local `addr` into `out`.
// Returns true iff the full count was read.
bool readProcess(HANDLE h, std::uintptr_t addr, void* out, std::size_t size) {
    SIZE_T got = 0;
    if (!::ReadProcessMemory(h, reinterpret_cast<LPCVOID>(addr), out, size, &got)) {
        return false;
    }
    return got == size;
}

bool writeProcess(HANDLE h, std::uintptr_t addr, const void* in, std::size_t size) {
    SIZE_T wrote = 0;
    if (!::WriteProcessMemory(h, reinterpret_cast<LPVOID>(addr), in, size, &wrote)) {
        return false;
    }
    return wrote == size;
}

// Apply the validator to a candidate RDRAM base address.
// Reads 4 bytes at base+0x11A5EC and matches against the byte-swapped
// "ZELD" magic. A successful match means an OoT save file is loaded and
// the candidate is a real OoT RDRAM region. Content-only check — no AOB
// pattern matching, no module anchoring, robust to Project64 versions.
bool validateRdramBase(HANDLE h, std::uintptr_t base) {
    std::uint8_t magic[4];
    if (!readProcess(h, base + kSaveLoadedMagicOffset,
                     magic, sizeof(magic))) return false;
    return std::memcmp(magic, kSaveLoadedMagicSwapped.data(),
                       kSaveLoadedMagicSwapped.size()) == 0;
}

// VirtualQueryEx-driven fallback. Walks the target process's address
// space and runs validateRdramBase on each unique *AllocationBase*.
//
// Why AllocationBase, not BaseAddress: VirtualQueryEx splits a single
// reservation into multiple regions whenever the per-page protection
// differs (committed vs reserved, RW vs RWX, etc.). Project64's 32 MB
// RDRAM reservation often appears as dozens of small regions, none of
// which start at the original VirtualAlloc address. AllocationBase is
// preserved across all regions belonging to the same reservation, so
// it's the right anchor for the validator (which expects the magic at
// a fixed offset from the start of P64's m_RDRAM).
//
// Read failures (validator falling on uncommitted pages within the
// reservation) are handled naturally — validateRdramBase returns false
// if ReadProcessMemory can't read the magic offset. False positives are
// blocked by the magic byte sequence being specific enough.
std::uintptr_t scanForRdram(HANDLE h, std::vector<std::uintptr_t>* outAll,
                            ProbeInfo* stats = nullptr) {
    // 32-bit LAA upper bound — project64.exe is 32-bit. For a future
    // 64-bit P64 build, swap to si.lpMaximumApplicationAddress.
    constexpr std::uintptr_t kUpper32LAA = 0x00000000FFFE0000ULL;

    std::unordered_set<std::uintptr_t> tested;
    MEMORY_BASIC_INFORMATION mbi{};
    std::uintptr_t addr = 0;
    std::uintptr_t firstHit = 0;
    while (addr < kUpper32LAA &&
           ::VirtualQueryEx(h, reinterpret_cast<LPCVOID>(addr),
                            &mbi, sizeof(mbi)) == sizeof(mbi)) {
        const auto regionBase = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        const auto allocBase  = reinterpret_cast<std::uintptr_t>(mbi.AllocationBase);
        if (stats) {
            ++stats->scanRegionsTotal;
            if (mbi.State == MEM_COMMIT) ++stats->scanRegionsCommitted;
            stats->scanHighestAddr = regionBase + mbi.RegionSize;
        }

        // Test the reservation root once. Validator fails fast if the
        // magic isn't present or the page isn't readable.
        if (allocBase && tested.insert(allocBase).second) {
            if (stats) ++stats->scanAllocBasesTested;
            if (validateRdramBase(h, allocBase)) {
                if (!firstHit) firstHit = allocBase;
                if (outAll) outAll->push_back(allocBase);
                else        return allocBase;
            }
        }

        const auto next = regionBase + mbi.RegionSize;
        if (next <= addr) break;  // defensive: prevent infinite loop
        addr = next;
    }
    return firstHit;
}

// Read the "ZELDAZ" string at the save-magic offset and return it
// byte-swap-corrected (so it reads "ZELDAZ" instead of "DLEZZA"). 6 bytes
// — gSaveContext.newf is 6 chars. Used by the probe to confirm the
// validator's match is the real save magic and not a coincidence.
std::string readSaveMagicString(HANDLE h, std::uintptr_t base) {
    // 8 bytes covers the magic + 2 trailing bytes; rounded up to word
    // boundary keeps the per-word swap clean.
    std::uint8_t raw[8];
    if (!readProcess(h, base + kSaveLoadedMagicOffset, raw, sizeof(raw))) return {};
    // Reverse bytes within each 32-bit word to recover N64-native order.
    for (std::size_t i = 0; i + 3 < sizeof(raw); i += 4) {
        std::swap(raw[i],     raw[i + 3]);
        std::swap(raw[i + 1], raw[i + 2]);
    }
    std::string out;
    out.reserve(6);
    for (std::size_t i = 0; i < 6; ++i) {
        const auto b = raw[i];
        out.push_back((b >= 0x20 && b < 0x7F) ? static_cast<char>(b) : '?');
    }
    return out;
}

#endif  // _WIN32

}  // namespace

// ---------------------------------------------------------------------------
// Probe entry point — used by --p64-probe to surface what's reachable.
// ---------------------------------------------------------------------------

std::optional<ProbeInfo> probeProject64() {
#ifdef _WIN32
    const DWORD pid = findProject64Pid();
    if (!pid) return std::nullopt;
    HANDLE h = ::OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
                             FALSE, pid);
    if (!h) return std::nullopt;

    ProbeInfo info{};
    info.pid     = pid;
    info.process = h;
    getProcessModule(pid, info.moduleBase, info.moduleSize);

    // Try each known base. Record per-candidate validation result so the
    // probe can show "tried X, Y, Z; W matched". Helpful when adding a
    // new known-base entry for a future Project64 release.
    info.fastPathCandidates.assign(std::begin(kKnownBases), std::end(kKnownBases));
    info.fastPathValidated.reserve(info.fastPathCandidates.size());
    for (auto base : info.fastPathCandidates) {
        const bool ok = validateRdramBase(h, base);
        info.fastPathValidated.push_back(ok);
        if (ok && !info.validatedFastPath) info.validatedFastPath = base;
    }

    // Always run the VirtualQueryEx scan during probe — even if the fast
    // path matched, the scan tells us whether the RDRAM block lives at an
    // address we should add to the known list.
    scanForRdram(h, &info.scannedCandidates, &info);

    const std::uintptr_t winningBase = info.validatedFastPath
        ? info.validatedFastPath
        : (info.scannedCandidates.empty() ? 0 : info.scannedCandidates.front());
    if (winningBase) {
        info.cartId          = readSaveMagicString(h, winningBase);
        // Internal ROM name from the cart header isn't reliably present
        // in RDRAM (the PIF bootloader doesn't DMA the header to a fixed
        // offset). Leave it empty — version detection later will come
        // from save-context fields, not the ROM header.
        info.internalRomName.clear();
        info.headerDump.resize(kHeaderDumpSize);
        if (!readProcess(h, winningBase + kSaveLoadedMagicOffset,
                         info.headerDump.data(),
                         info.headerDump.size())) {
            info.headerDump.clear();
        }
    }
    return info;
#else
    return std::nullopt;
#endif
}

// ---------------------------------------------------------------------------
// Source — live attach used by the GUI and the tracker poll loop.
// ---------------------------------------------------------------------------

Source::Source() = default;

Source::~Source() {
    disconnect();
}

bool Source::isAvailable() {
#ifdef _WIN32
    return findProject64Pid() != 0;
#else
    return false;
#endif
}

bool Source::connect() {
#ifdef _WIN32
    if (process_) return isConnected();

    const DWORD pid = findProject64Pid();
    if (!pid) return false;

    // PROCESS_VM_WRITE + VM_OPERATION are needed for writeBytes()'s
    // WriteProcessMemory path (the --oot-write debug helper); reads still
    // function the same. Falls back to read-only if the elevated open
    // fails (e.g. anti-debug or insufficient privilege).
    HANDLE h = ::OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE |
                             PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION,
                             FALSE, pid);
    if (!h) {
        h = ::OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
                          FALSE, pid);
    }
    if (!h) return false;
    process_ = h;

    // Fast path: walk the known bases. First match wins.
    for (auto base : kKnownBases) {
        if (validateRdramBase(h, base)) {
            rdramBase_ = base;
            break;
        }
    }
    // Fallback: VirtualQueryEx sweep. First hit wins; subsequent matches
    // (if any) are surfaced only by the probe, not the live source.
    if (!rdramBase_) {
        rdramBase_ = scanForRdram(h, nullptr);
    }
    if (!rdramBase_) {
        // Couldn't locate an OoT-family ROM in any candidate region.
        // Process was found and opened, but there's no playable state to
        // read — likely no ROM loaded yet. Stay disconnected so the
        // factory's retry loop tries again.
        ::CloseHandle(h);
        process_ = nullptr;
        return false;
    }

    // Hardcoded for now — the validator already established this is OoT
    // with a loaded save. Version detection (NTSC 1.0 / 1.1 / 1.2 / PAL /
    // MQ / JP) will be added when an OoT decoder lands; until then the
    // tracker only needs to know "it's OoT".
    gameId_ = "OOT";
    return true;
#else
    return false;
#endif
}

void Source::disconnect() {
#ifdef _WIN32
    if (process_) {
        ::CloseHandle(static_cast<HANDLE>(process_));
        process_ = nullptr;
    }
#endif
    rdramBase_ = 0;
    gameId_.clear();
}

bool Source::isConnected() const {
#ifdef _WIN32
    if (!process_ || !rdramBase_) return false;
    DWORD exitCode = 0;
    if (!::GetExitCodeProcess(static_cast<HANDLE>(process_), &exitCode)) return false;
    return exitCode == STILL_ACTIVE;
#else
    return false;
#endif
}

std::optional<std::uintptr_t> Source::translate(std::uint32_t n64Addr,
                                                std::size_t size) const {
    if (!rdramBase_) return std::nullopt;
    const std::uint32_t physAddr = n64Addr & kN64PhysMask;
    // Guard against wrap-around when the requested read straddles the end.
    if (physAddr >= kRdramPhysLimit) return std::nullopt;
    if (size > kRdramPhysLimit || physAddr + size > kRdramPhysLimit) {
        return std::nullopt;
    }
    return rdramBase_ + physAddr;
}

bool Source::readBytes(std::uint32_t addr, void* out, std::size_t size) const {
#ifdef _WIN32
    if (!isConnected()) return false;
    if (size == 0) return true;

    // Per-word byte swap: Project64 stores each N64 32-bit word with its
    // four bytes reversed in m_RDRAM, so host LE loads return MIPS BE
    // values for free. To present N64-native bytes to callers (matching
    // the OoT decomp's z64save.h layout and what every other MemorySource
    // returns), we read an aligned superset, swap each word, and copy the
    // requested sub-range out. Byte at N64 offset N lives at host offset
    // N XOR 3 — this is the per-word reversal expressed pointwise.
    const std::uint32_t firstByte   = addr & kN64PhysMask;
    const std::uint32_t pastLast    = firstByte + static_cast<std::uint32_t>(size);
    if (firstByte >= kRdramPhysLimit || pastLast > kRdramPhysLimit) return false;

    const std::uint32_t alignedStart = firstByte & ~std::uint32_t{3};
    const std::uint32_t alignedEnd   = (pastLast + 3) & ~std::uint32_t{3};
    const std::size_t   alignedSize  = alignedEnd - alignedStart;

    std::vector<std::uint8_t> buf(alignedSize);
    if (!readProcess(static_cast<HANDLE>(process_), rdramBase_ + alignedStart,
                     buf.data(), buf.size())) {
        return false;
    }
    for (std::size_t i = 0; i + 3 < buf.size(); i += 4) {
        std::swap(buf[i],     buf[i + 3]);
        std::swap(buf[i + 1], buf[i + 2]);
    }
    std::memcpy(out, buf.data() + (firstByte - alignedStart), size);
    return true;
#else
    (void)addr; (void)out; (void)size;
    return false;
#endif
}

bool Source::writeBytes(std::uint32_t addr, const void* data, std::size_t size) const {
#ifdef _WIN32
    if (!isConnected()) return false;
    if (size == 0) return true;

    // Project64 stores each N64 32-bit word with its four bytes reversed in
    // host memory (see readBytes for the read-side reasoning). To write a
    // subrange in N64-native order, we read the aligned superset, swap each
    // word to N64-native, splice the caller's bytes in, swap back, and
    // write the aligned region. Read-modify-write: not atomic, but the
    // tracker only uses this for debug helpers where racing the emulator
    // isn't a concern.
    const std::uint32_t firstByte = addr & kN64PhysMask;
    const std::uint32_t pastLast  = firstByte + static_cast<std::uint32_t>(size);
    if (firstByte >= kRdramPhysLimit || pastLast > kRdramPhysLimit) return false;

    const std::uint32_t alignedStart = firstByte & ~std::uint32_t{3};
    const std::uint32_t alignedEnd   = (pastLast + 3) & ~std::uint32_t{3};
    const std::size_t   alignedSize  = alignedEnd - alignedStart;

    std::vector<std::uint8_t> buf(alignedSize);
    if (!readProcess(static_cast<HANDLE>(process_), rdramBase_ + alignedStart,
                     buf.data(), buf.size())) {
        return false;
    }
    for (std::size_t i = 0; i + 3 < buf.size(); i += 4) {
        std::swap(buf[i],     buf[i + 3]);
        std::swap(buf[i + 1], buf[i + 2]);
    }
    std::memcpy(buf.data() + (firstByte - alignedStart), data, size);
    for (std::size_t i = 0; i + 3 < buf.size(); i += 4) {
        std::swap(buf[i],     buf[i + 3]);
        std::swap(buf[i + 1], buf[i + 2]);
    }
    return writeProcess(static_cast<HANDLE>(process_), rdramBase_ + alignedStart,
                        buf.data(), buf.size());
#else
    (void)addr; (void)data; (void)size;
    return false;
#endif
}

}  // namespace tpt::p64
