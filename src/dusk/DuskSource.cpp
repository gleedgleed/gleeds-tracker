#include "dusk/DuskSource.h"

#include "core/Region.h"
#include "core/SaveOffsets.h"
#include "memory/PatternScan.h"

#include <array>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <winver.h>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "version.lib")
#endif

namespace tpt::dusk {

namespace {

constexpr const char* kProcessName = "dusklight.exe";
constexpr const char* kSymbolName  = "g_dComIfG_gameInfo";

// === Pattern for locating g_dComIfG_gameInfo in production builds ============
//
// Derived from a RelWithDebInfo Dusk build (May 2026) via --dusk-probe.
// The pattern brackets a tail-call wrapper function that loads
// g_dComIfG_gameInfo into rcx, jumps to some other function, and then has
// 15 bytes of int3 padding before a tiny accessor (movzx eax,
// byte [rcx+0x1B]; ret). The padding + specific accessor make this 32-byte
// signature highly selective.
//
// Wildcards:
//   bytes [3..6]   — disp32 of the LEA (ASLR-relative; this is what we
//                    extract to recover g_dComIfG_gameInfo's address)
//   bytes [8..11]  — disp32 of the JMP (varies per build)
//
constexpr const char* kGameInfoLEAPattern =
    "48 8D 0D ?? ?? ?? ?? E9 ?? ?? ?? ?? "
    "CC CC CC CC CC CC CC CC CC CC CC CC CC CC CC "
    "0F B6 41 1B C3";
constexpr std::size_t kLEAInstructionLen = 7;  // REX.W + 8D + ModR/M + disp32
constexpr std::size_t kLEADispOffset     = 3;  // 48 8D 0D | XX XX XX XX

// Static offset from g_dComIfG_gameInfo to its dSv_save_c instance.
// Empirically confirmed via probe hex dump: player name "Link" appears at
// g_dComIfG_gameInfo+0x1B4, which matches the canonical SAVE+0x1B4
// SLOT_NAME offset. So mSave[0] sits at offset 0 of dComIfG_inf_c — the
// nested layout `info.mSavedata.mSave[0]` accumulates to zero because each
// nesting starts at offset 0 of its parent. Reverify if upstream Dusk
// reorders fields in dComIfG_inf_c.
constexpr std::ptrdiff_t kSaveBlockOffsetFromGameInfo = 0;

#ifdef _WIN32

// Walk the process list looking for `name`. Returns 0 if none found.
DWORD findPidByName(const char* name) {
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32 pe{};
    pe.dwSize = sizeof(pe);
    DWORD found = 0;
    if (::Process32First(snap, &pe)) {
        do {
            if (::_stricmp(pe.szExeFile, name) == 0) {
                found = pe.th32ProcessID;
                break;
            }
        } while (::Process32Next(snap, &pe));
    }
    ::CloseHandle(snap);
    return found;
}

// Find module `name` loaded in process `pid`. Module enumeration needs to
// happen against the target PID, not via a process handle. Returns
// {baseAddr, size} or nullopt.
struct ModuleInfo { std::uintptr_t base; std::size_t size; };
std::optional<ModuleInfo> findModule(DWORD pid, const char* name) {
    HANDLE snap = ::CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return std::nullopt;
    MODULEENTRY32 me{};
    me.dwSize = sizeof(me);
    std::optional<ModuleInfo> result;
    if (::Module32First(snap, &me)) {
        do {
            if (::_stricmp(me.szModule, name) == 0) {
                result = ModuleInfo{
                    reinterpret_cast<std::uintptr_t>(me.modBaseAddr),
                    static_cast<std::size_t>(me.modBaseSize),
                };
                break;
            }
        } while (::Module32Next(snap, &me));
    }
    ::CloseHandle(snap);
    return result;
}

// Read PE headers from the target process, find the .text section's
// {address, size} in target-process address space. Returns nullopt on any
// parse failure.
std::optional<std::pair<std::uintptr_t, std::size_t>>
findTextSection(HANDLE process, ModuleInfo mod) {
    // DOS header.
    IMAGE_DOS_HEADER dos{};
    SIZE_T got = 0;
    if (!::ReadProcessMemory(process, reinterpret_cast<LPCVOID>(mod.base),
                             &dos, sizeof(dos), &got) || got != sizeof(dos)
        || dos.e_magic != IMAGE_DOS_SIGNATURE) {
        return std::nullopt;
    }
    // NT headers.
    IMAGE_NT_HEADERS nt{};
    if (!::ReadProcessMemory(process,
            reinterpret_cast<LPCVOID>(mod.base + dos.e_lfanew),
            &nt, sizeof(nt), &got) || got != sizeof(nt)
        || nt.Signature != IMAGE_NT_SIGNATURE) {
        return std::nullopt;
    }
    // Section table follows the optional header.
    const std::uintptr_t sectTableAddr = mod.base + dos.e_lfanew
        + offsetof(IMAGE_NT_HEADERS, OptionalHeader)
        + nt.FileHeader.SizeOfOptionalHeader;
    std::vector<IMAGE_SECTION_HEADER> sects(nt.FileHeader.NumberOfSections);
    if (!::ReadProcessMemory(process, reinterpret_cast<LPCVOID>(sectTableAddr),
            sects.data(),
            sects.size() * sizeof(IMAGE_SECTION_HEADER),
            &got)
        || got != sects.size() * sizeof(IMAGE_SECTION_HEADER)) {
        return std::nullopt;
    }
    for (const auto& s : sects) {
        if (std::strncmp(reinterpret_cast<const char*>(s.Name), ".text", 5) == 0) {
            return std::make_pair(mod.base + s.VirtualAddress,
                                  static_cast<std::size_t>(s.Misc.VirtualSize));
        }
    }
    return std::nullopt;
}

// Resolve a symbol via DbgHelp. Returns 0 on miss (symbols not present,
// name not found, etc.). Caller is responsible for ::SymCleanup if needed.
std::uintptr_t resolveSymbol(HANDLE process, ModuleInfo mod, const char* name) {
    if (!::SymInitialize(process, NULL, FALSE)) return 0;
    // Tell DbgHelp where the module is loaded so it can find the PDB.
    if (!::SymLoadModuleEx(process, NULL, kProcessName, NULL,
                           mod.base, static_cast<DWORD>(mod.size),
                           NULL, 0)) {
        ::SymCleanup(process);
        return 0;
    }
    char buf[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
    auto* si = reinterpret_cast<SYMBOL_INFO*>(buf);
    si->SizeOfStruct = sizeof(SYMBOL_INFO);
    si->MaxNameLen   = MAX_SYM_NAME;
    std::uintptr_t out = 0;
    if (::SymFromName(process, name, si)) {
        out = static_cast<std::uintptr_t>(si->Address);
    }
    ::SymCleanup(process);
    return out;
}

// ============================================================================
// Known-build offset table (Path 1: fast attach)
// ============================================================================
// Maps a dusklight.exe build to the RVA of g_dComIfG_gameInfo within its
// module. With a known entry attach is one ReadProcessMemory + validator
// pass — no whole-process scanning, no multi-candidate disambiguation, no
// tick() bookkeeping. The buffer at moduleBase+offset *is* the live save
// by construction (validated empirically per build via --dusk-probe).
//
// Populate a new entry:
//   1. Run `tptracker.exe --dusk-probe` against the target dusklight.exe.
//   2. From the output, take (gameInfoAddr - moduleBase) — that's the RVA.
//      Both PDB and AOB columns must agree on gameInfoAddr; if they don't,
//      one of them is wrong and the entry isn't safe to commit.
//   3. Add a row here. The fast path validates with looksLikeSaveBlock()
//      before committing, so a stale offset that survives a code reshuffle
//      will fall through to the content scan rather than mis-attach.

struct BuildVersion {
    std::uint16_t major;
    std::uint16_t minor;
    std::uint16_t patch;
    std::uint16_t build;
    constexpr bool operator==(const BuildVersion&) const = default;
};

struct KnownBuild {
    BuildVersion   version;
    std::uintptr_t gameInfoOffset;  // RVA from dusklight.exe module base
};

// Empty table is the correct starting state — every connect() falls
// through to Path 2 until offsets are measured. Bump the std::array size
// and append rows as releases stabilize, e.g.:
//
//   constexpr std::array<KnownBuild, 1> kKnownBuilds{{
//       { {1, 1, 1, 0}, 0x???????? },  // Dusk v1.1.1
//   }};
//
// (Plain `KnownBuild kKnownBuilds[] = {}` would be ill-formed in C++
// since zero-size raw arrays aren't allowed; std::array<T, 0> is.)
constexpr std::array<KnownBuild, 0> kKnownBuilds{};

// Walk modules in `pid`, return szExePath for the named module (full
// on-disk image path, needed by GetFileVersionInfoA). Empty on miss.
// Parallels findModule() but returns the path string rather than the
// mapped base+size — kept separate so the version read is cheap when the
// PID lookup is the only thing wanted.
std::string findModulePath(DWORD pid, const char* name) {
    HANDLE snap = ::CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return {};
    MODULEENTRY32 me{};
    me.dwSize = sizeof(me);
    std::string out;
    if (::Module32First(snap, &me)) {
        do {
            if (::_stricmp(me.szModule, name) == 0) {
                out = me.szExePath;
                break;
            }
        } while (::Module32Next(snap, &me));
    }
    ::CloseHandle(snap);
    return out;
}

// Read VS_FIXEDFILEINFO from `path` and unpack to four 16-bit fields.
// Returns nullopt when the binary lacks a version resource (some custom
// builds strip them). Dusk v1.1.1's official x86_64 build ships one
// (verified: 1.1.1.0), so the official release cadence is well-served.
std::optional<BuildVersion> readFileVersion(const char* path) {
    DWORD handle = 0;
    const DWORD size = ::GetFileVersionInfoSizeA(path, &handle);
    if (size == 0) return std::nullopt;
    std::vector<std::uint8_t> buf(size);
    if (!::GetFileVersionInfoA(path, handle, size, buf.data())) {
        return std::nullopt;
    }
    VS_FIXEDFILEINFO* ffi = nullptr;
    UINT ffiLen = 0;
    if (!::VerQueryValueA(buf.data(), "\\",
                          reinterpret_cast<LPVOID*>(&ffi), &ffiLen)
        || !ffi || ffiLen < sizeof(VS_FIXEDFILEINFO)) {
        return std::nullopt;
    }
    BuildVersion v;
    v.major = static_cast<std::uint16_t>(HIWORD(ffi->dwFileVersionMS));
    v.minor = static_cast<std::uint16_t>(LOWORD(ffi->dwFileVersionMS));
    v.patch = static_cast<std::uint16_t>(HIWORD(ffi->dwFileVersionLS));
    v.build = static_cast<std::uint16_t>(LOWORD(ffi->dwFileVersionLS));
    return v;
}

const KnownBuild* lookupKnownBuild(const BuildVersion& v) {
    for (const auto& kb : kKnownBuilds) {
        if (kb.version == v) return &kb;
    }
    return nullptr;
}

#endif  // _WIN32

}  // namespace

#ifdef _WIN32

// ============================================================================
// Content-based save-block locator (primary path)
// ============================================================================
//
// The save-block layout is a TP-game invariant: any correct Dusk binary
// (or future re-implementation) MUST place dSv_save_c fields at the offsets
// the original game's save-file format demands, because the on-disk save
// data has to round-trip through this in-memory layout. We exploit two
// such invariants — the player slot name and the horse name — to recognize
// dSv_save_c instances in arbitrary writable memory. This survives compiler
// version changes, LTO, PGO, function-reorder, and Dusk version updates as
// long as the save format itself doesn't break.

// True if `field` looks like a TP stage code: "X_YYY...\0" where X is
// an uppercase letter, "_" is literal, and YYY... is at least 2 chars of
// uppercase letters / digits, then a null terminator. Examples that
// pass: "F_SP108", "R_SP01", "D_MN05A". Strong invariant — Dusk's stale
// name-prefixed buffers don't have this exact shape at +0x4E.
bool isPlausibleStageCode(const std::uint8_t* field, std::size_t len) {
    if (len < 5) return false;
    const std::uint8_t c0 = field[0];
    if (c0 < 'A' || c0 > 'Z') return false;
    if (field[1] != '_') return false;
    for (std::size_t i = 2; i < len; ++i) {
        const std::uint8_t c = field[i];
        if (c == 0) return i >= 4;  // at least 2 chars between '_' and null
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!ok) return false;
    }
    return false;  // no null terminator within `len`
}

// True if `field` looks like a TP slot-name field: 7-char (+ null
// terminator) ASCII name, first char is a letter or space, no
// non-printable bytes before the terminator. Empty fields (first byte
// null) fail — those mean "no save loaded in this slot".
bool isPlausibleAsciiName(const std::uint8_t* field, std::size_t len) {
    if (len < 2) return false;
    const std::uint8_t first = field[0];
    const bool firstOk = (first >= 'A' && first <= 'Z')
                      || (first >= 'a' && first <= 'z')
                      || first == ' ';
    if (!firstOk) return false;
    bool foundNull = false;
    for (std::size_t i = 1; i < len; ++i) {
        const std::uint8_t c = field[i];
        if (c == 0) { foundNull = true; break; }
        if (c < 0x20 || c >= 0x7F) return false;  // non-printable
    }
    return foundNull;
}

// True if `buf` (length `bufLen`, must be >= kSaveBlockSize) looks like a
// *loaded* dSv_save_c instance. Combines two kinds of checks:
//
//   1) Two cross-validated string fields make false positives vanishingly
//      unlikely:
//        +0x1B4: player slot name (8-byte field — 7 chars + null)
//        +0x1C5: horse name (8-byte field — same shape)
//      Both are user-editable, so we don't pin values, only structure.
//
//   2) Sanity checks on game-state fields that distinguish a *live* save
//      from an uninitialized/empty save-slot buffer. Dusk allocates buffers
//      for all 3 save slots and copies the default player+horse names
//      into each — but the slots that aren't loaded leave game-state
//      fields at 0xFFFF (uninitialized) or 0x0000. A live save has
//      plausible health values:
//        +0x000 maxHealth (u16 BE): 1..160 quarter-hearts (40 = max)
//        +0x002 curHealth (u16 BE): same range, <= maxHealth
//      0xFFFF is never a valid TP health value, so it's a clean reject.
// dSv_save_c offsets (from src/core/QuestState.cpp). Mirrored here so we
// can validate without pulling in the full decoder — the validator runs
// inside an inner scan loop and needs to be cheap-and-strict.
constexpr std::size_t kPlayerNameOffset    = 0x1B4;
constexpr std::size_t kPlayerNameLen       = 8;
constexpr std::size_t kHorseNameOffset     = 0x1C5;
constexpr std::size_t kHorseNameLen        = 8;
constexpr std::size_t kMaxHealthOffset     = 0x000;
constexpr std::size_t kCurHealthOffset     = 0x002;
constexpr std::size_t kRupeesOffset        = 0x004;
constexpr std::size_t kMaxLanternOilOffset = 0x006;
constexpr std::size_t kCurLanternOilOffset = 0x008;
constexpr std::size_t kWalletTierOffset    = 0x019;
constexpr std::size_t kCurrentFormOffset   = 0x01E;
constexpr std::size_t kTransformLevelOffset= 0x030;
constexpr std::size_t kDarkClearLevelOffset= 0x031;
constexpr std::size_t kStageCodeOffset1    = 0x4E;  // "previous" stage code
constexpr std::size_t kStageCodeOffset2    = 0x58;  // current stage code (per QuestState)
constexpr std::size_t kStageCodeLen        = 8;
constexpr std::size_t kPoeSoulsOffset      = 0x10C;
constexpr std::size_t kTearsFaronOffset    = 0x114;
constexpr std::size_t kTearsEldinOffset    = 0x115;
constexpr std::size_t kTearsLanayruOffset  = 0x116;

// Comprehensive structural+semantic plausibility check on a buffer that
// could be a dSv_save_c. Walks every easily-bounded field we know about
// and rejects on any out-of-range value. We're lenient about
// player-modified saves (cheating) but strict about anything that's
// fundamentally impossible — wallet tier 7, transform level 200, etc.
// Together these checks make false-positive matches astronomically
// unlikely; once it passes, the candidate is structurally a real TP save.
bool looksLikeSaveBlock(const std::uint8_t* buf, std::size_t bufLen) {
    if (bufLen < tpt::core::kSaveBlockSize) return false;

    // String-shape checks (cheapest, most selective: a passing buffer must
    // have valid ASCII names + valid TP stage-code patterns at 4 fixed
    // offsets).
    if (!isPlausibleAsciiName(buf + kPlayerNameOffset, kPlayerNameLen)) return false;
    if (!isPlausibleAsciiName(buf + kHorseNameOffset,  kHorseNameLen))  return false;
    if (!isPlausibleStageCode(buf + kStageCodeOffset1, kStageCodeLen))  return false;
    if (!isPlausibleStageCode(buf + kStageCodeOffset2, kStageCodeLen))  return false;

    auto rd16 = [](const std::uint8_t* p) -> std::uint16_t {
        return (std::uint16_t(p[0]) << 8) | std::uint16_t(p[1]);
    };

    // Health (u16 BE, fifth-heart units). Reject the 0xFFFF "uninitialized"
    // marker and clearly-impossible ranges. Vanilla cap is 0x64 (20 hearts);
    // we allow up to 0x200 for health mods.
    const std::uint16_t maxHp = rd16(buf + kMaxHealthOffset);
    if (maxHp < 5 || maxHp > 0x200) return false;
    const std::uint16_t curHp = rd16(buf + kCurHealthOffset);
    if (curHp > maxHp) return false;

    // Rupees: giant-wallet cap = 1000; allow 2000 for safety.
    if (rd16(buf + kRupeesOffset) > 2000) return false;

    // Lantern oil (u16 BE). Vanilla cap ~100; allow 1000.
    const std::uint16_t maxOil = rd16(buf + kMaxLanternOilOffset);
    const std::uint16_t curOil = rd16(buf + kCurLanternOilOffset);
    if (maxOil > 1000) return false;
    if (curOil > maxOil + 100) return false;  // small slack for off-by-one

    // Single-byte enums with hard upper bounds.
    if (buf[kWalletTierOffset]     > 2)  return false;  // 0/1/2 = child/big/giant
    if (buf[kCurrentFormOffset]    > 1)  return false;  // 0=human, 1=wolf
    if (buf[kTransformLevelOffset] > 4)  return false;  // 5 twilight stages
    if (buf[kDarkClearLevelOffset] > 4)  return false;

    // Tear counters: 16 per region.
    if (buf[kTearsFaronOffset]   > 16) return false;
    if (buf[kTearsEldinOffset]   > 16) return false;
    if (buf[kTearsLanayruOffset] > 16) return false;

    // Poe souls: 60 collectible total.
    if (buf[kPoeSoulsOffset] > 60) return false;

    return true;
}

// One contiguous writable region of the target process. Returned by
// enumerateWritableRegions().
struct MemoryRegion { std::uintptr_t base; std::size_t size; };

// Cap per-region scan size. The .data sections of major modules are
// usually a few MB; heap regions can be much larger but the save block
// won't live there. 256 MB is a generous safety bound.
constexpr std::size_t kMaxRegionScan = 256 * 1024 * 1024;

// Walk the target process's virtual address space via VirtualQueryEx,
// collecting committed read-write regions large enough to hold a save
// block. Guards skip non-writable / guard / inaccessible regions.
std::vector<MemoryRegion> enumerateWritableRegions(HANDLE process) {
    std::vector<MemoryRegion> out;
    MEMORY_BASIC_INFORMATION mbi{};
    std::uintptr_t addr = 0;
    // User-mode upper bound on x64 Windows (0x7FFF'FFFF'FFFF user limit).
    constexpr std::uintptr_t kAddrCeil = 0x00007FFFFFFFFFFFULL;
    while (addr < kAddrCeil) {
        if (!::VirtualQueryEx(process, reinterpret_cast<LPCVOID>(addr),
                              &mbi, sizeof(mbi))) break;
        const DWORD writeMask = PAGE_READWRITE | PAGE_WRITECOPY
                              | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if (mbi.State == MEM_COMMIT
            && (mbi.Protect & writeMask)
            && !(mbi.Protect & PAGE_GUARD)
            && mbi.RegionSize >= tpt::core::kSaveBlockSize) {
            out.push_back({reinterpret_cast<std::uintptr_t>(mbi.BaseAddress),
                           static_cast<std::size_t>(mbi.RegionSize)});
        }
        const auto next = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress)
                        + mbi.RegionSize;
        if (next <= addr) break;  // safety against pathological mbi
        addr = next;
    }
    return out;
}

// Collect *every* save-block-shaped buffer in the target's writable
// memory. Multiple matches are expected — Dusk keeps several mirrors
// (save slots 0-2, write-out staging, etc.). The validator only proves
// "this could be a TP save", not "this is the live one"; the caller
// disambiguates.
//
// Scan stride is 16 bytes: top-level globals on x64 MSVC are aligned to
// at least 16 (alignof(max_align_t)). Drop to 8 if a future Dusk build
// aligns dSv_save_c less strictly.
std::vector<std::uintptr_t> findAllSaveBlockCandidates(HANDLE process) {
    std::vector<std::uintptr_t> out;
    const auto regions = enumerateWritableRegions(process);
    std::vector<std::uint8_t> buf;
    for (const auto& r : regions) {
        const std::size_t sz = std::min(r.size, kMaxRegionScan);
        buf.resize(sz);
        SIZE_T got = 0;
        if (!::ReadProcessMemory(process, reinterpret_cast<LPCVOID>(r.base),
                                 buf.data(), buf.size(), &got)
            || got < tpt::core::kSaveBlockSize) {
            continue;
        }
        const std::size_t lastStart = got - tpt::core::kSaveBlockSize;
        for (std::size_t off = 0; off <= lastStart; off += 16) {
            if (looksLikeSaveBlock(buf.data() + off, got - off)) {
                out.push_back(r.base + off);
            }
        }
    }
    return out;
}

// Among multiple save-block-shaped candidates, pick the one being
// actively mutated by gameplay. The live save updates many times per
// second (frame counter, in-game time, position, animation state);
// stale mirrors don't. Sample the first 128 bytes of each candidate,
// sleep briefly, re-sample, and return whichever candidate's bytes
// changed.
//
// 128 bytes covers maxHealth/curHealth/rupees/lanternOil/spawn point
// and several other game-state fields — enough to detect any active
// gameplay even without movement. The sleep needs to span at least one
// game frame; ~120ms is conservative (>= 1 frame at 9 fps).
//
// Fallback when no candidate changes (game paused, in-menu, etc.):
// prefer the highest-address candidate. In practice the live save sits
// higher in the .data section than its snapshot mirrors, since live
// state structs tend to be declared after auxiliary buffers.
std::uintptr_t pickLiveCandidate(HANDLE process,
                                 const std::vector<std::uintptr_t>& candidates) {
    if (candidates.empty()) return 0;
    if (candidates.size() == 1) return candidates[0];

    constexpr std::size_t kSampleSize = 128;
    std::vector<std::vector<std::uint8_t>> snap1(candidates.size(),
        std::vector<std::uint8_t>(kSampleSize));
    std::vector<std::vector<std::uint8_t>> snap2(candidates.size(),
        std::vector<std::uint8_t>(kSampleSize));
    auto sample = [&](std::vector<std::vector<std::uint8_t>>& dst) {
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            SIZE_T got = 0;
            ::ReadProcessMemory(process,
                reinterpret_cast<LPCVOID>(candidates[i]),
                dst[i].data(), dst[i].size(), &got);
        }
    };
    sample(snap1);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    sample(snap2);

    // Pick the candidate whose sampled bytes changed the most. "Most"
    // breaks ties when multiple buffers update simultaneously (e.g.,
    // live + a tightly-mirrored snapshot).
    std::size_t bestIdx = 0;
    std::size_t bestDiff = 0;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        std::size_t diff = 0;
        for (std::size_t k = 0; k < kSampleSize; ++k) {
            if (snap1[i][k] != snap2[i][k]) ++diff;
        }
        if (diff > bestDiff) { bestDiff = diff; bestIdx = i; }
    }
    if (bestDiff > 0) return candidates[bestIdx];

    // No candidate changed — game is paused / on title / between
    // frames. Fall back to highest-address heuristic.
    return *std::max_element(candidates.begin(), candidates.end());
}

// Primary content-scan entrypoint. Returns 0 if nothing recognizable was
// found (most commonly because no save is loaded yet).
std::uintptr_t contentLocateSaveBlock(HANDLE process) {
    const auto candidates = findAllSaveBlockCandidates(process);
    return pickLiveCandidate(process, candidates);
}

// Provisional pick at connect()-time, before tick() has run. Ranked by:
//
//   0. **Timeline filter** (pre-filter). `totalTimeFrames` at +0x1A8 is
//      stamped at each save event, identical across all save-shaped
//      buffers belonging to the same save event, and different across
//      slots / older sessions. We keep only candidates with the maximum
//      observed value — i.e., the most-recently-active timeline — and
//      drop everything else as stale slot-state. This deterministically
//      eliminates the "other save slot has more progress than the live
//      save" failure mode.
//
//   1. Semantic score = popcount of set bits across the event-flag block
//      (+0x7F0, 256 bytes) plus the get-item-flag block (+0x0CC, 32 bytes).
//      Among same-timeline candidates this is usually equal (live and its
//      mirror reflect the same progress), but it's a useful first
//      separator if Dusk ever holds two timelines at the same save-time.
//
//   2. Transient tiebreaker = rupees + curHealth + curLanternOil. Lets us
//      pick the right one early-game when the semantic scores are still
//      tied: rupees (etc.) diverge between live (most recently written)
//      and staging (frozen at save event) within a single timeline.
//
//   3. Highest address (last resort, when even transient is tied — e.g.
//      immediately post-save when live and staging are byte-identical).
//
// This is *only* the initial guess — tick() refines as soon as gameplay
// actually writes to one of the candidates.
std::uintptr_t pickInitialCandidate(HANDLE process,
                                    const std::vector<std::uintptr_t>& cands) {
    if (cands.empty()) return 0;
    if (cands.size() == 1) return cands[0];

    constexpr std::size_t kTotalTimeFramesOffset = 0x1A8;
    struct Scored {
        std::uintptr_t addr;
        int semantic;
        int transient;
        std::uint64_t totalTimeFrames;
    };
    std::vector<Scored> rows;
    rows.reserve(cands.size());
    std::vector<std::uint8_t> save(tpt::core::kSaveBlockSize);

    auto rd64BE = [](const std::uint8_t* p) -> std::uint64_t {
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
        return v;
    };

    for (auto addr : cands) {
        SIZE_T got = 0;
        if (!::ReadProcessMemory(process, reinterpret_cast<LPCVOID>(addr),
                                 save.data(), save.size(), &got)
            || got != save.size()) {
            continue;
        }
        Scored s{addr, 0, 0, 0};
        for (std::size_t i = 0x7F0; i < 0x7F0 + 256 && i < save.size(); ++i) {
            s.semantic += std::popcount(save[i]);
        }
        for (std::size_t i = 0x0CC; i < 0x0CC + 32 && i < save.size(); ++i) {
            s.semantic += std::popcount(save[i]);
        }
        auto rd16 = [&](std::size_t off) {
            return (int(save[off]) << 8) | int(save[off + 1]);
        };
        s.transient = rd16(0x004) + rd16(0x002) + rd16(0x008);
        s.totalTimeFrames = rd64BE(save.data() + kTotalTimeFramesOffset);
        rows.push_back(s);
    }
    if (rows.empty()) return 0;

    // Pre-filter: keep only candidates from the most-recently-active
    // timeline. Empty buffers (totalTimeFrames=0) only survive if every
    // candidate is also at 0 (i.e. no save has happened yet) — in that
    // case all candidates pass and the score-based ranking applies.
    std::uint64_t maxTimeline = 0;
    for (const auto& r : rows) {
        if (r.totalTimeFrames > maxTimeline) maxTimeline = r.totalTimeFrames;
    }
    std::vector<Scored> active;
    active.reserve(rows.size());
    for (const auto& r : rows) {
        if (r.totalTimeFrames == maxTimeline) active.push_back(r);
    }
    // Defensive: if filtering somehow left us empty (shouldn't happen),
    // fall back to the full set.
    auto& pool = active.empty() ? rows : active;

    auto best = std::max_element(pool.begin(), pool.end(),
        [](const Scored& a, const Scored& b) {
            if (a.semantic  != b.semantic)  return a.semantic  < b.semantic;
            if (a.transient != b.transient) return a.transient < b.transient;
            return a.addr < b.addr;
        });
    return best->addr;
}

// ============================================================================
// AOB (array-of-byte) scanner — fast fallback / dev diagnostic
// ============================================================================

// Run the production AOB scan against an already-read .text buffer.
// Returns the resolved g_dComIfG_gameInfo address, or 0 on no match.
std::uintptr_t aobLocateGameInfoIn(std::span<const std::uint8_t> text,
                                   std::uintptr_t textBase) {
    if (kGameInfoLEAPattern[0] == '\0') return 0;
    const auto pat = tpt::memory::parsePattern(kGameInfoLEAPattern);
    const auto off = tpt::memory::findPattern(text, pat);
    if (!off) return 0;
    const std::uintptr_t instrAddr = textBase + *off;
    return tpt::memory::decodeRipRel32(
        instrAddr, kLEAInstructionLen,
        text.data() + *off + kLEADispOffset);
}

// Scan a .text buffer for x86-64 "REX.W LEA reg, [rip+disp32]" instructions
// whose disp32 target equals `target`. Up to 8 matches. The REX prefix is
// 0x48 (rax/rcx/rdx/rbx/rsp/rbp/rsi/rdi) or 0x4C (r8..r15). Opcode 0x8D.
// ModR/M byte has mod=00, rm=101 — mask (b & 0xC7) == 0x05.
std::vector<LeaRefMatch> scanLeaReferences(std::span<const std::uint8_t> text,
                                           std::uintptr_t textBase,
                                           std::uintptr_t target,
                                           std::size_t maxMatches = 8) {
    std::vector<LeaRefMatch> out;
    if (text.size() < 7) return out;
    const std::size_t last = text.size() - 7;
    for (std::size_t i = 0; i <= last && out.size() < maxMatches; ++i) {
        const std::uint8_t rex = text[i];
        if (rex != 0x48 && rex != 0x4C) continue;
        if (text[i + 1] != 0x8D) continue;
        if ((text[i + 2] & 0xC7) != 0x05) continue;
        std::int32_t disp = 0;
        std::memcpy(&disp, &text[i + 3], 4);
        const std::uintptr_t instrEnd = textBase + i + 7;
        const std::uintptr_t hit = static_cast<std::uintptr_t>(
            static_cast<std::int64_t>(instrEnd) + disp);
        if (hit != target) continue;
        LeaRefMatch m;
        m.addr = textBase + i;
        m.bytesLen = std::min<std::size_t>(32, text.size() - i);
        std::memcpy(m.bytes.data(), &text[i], m.bytesLen);
        out.push_back(m);
    }
    return out;
}

// ============================================================================
// Path 1 entry: known-build fast attach
// ============================================================================
// Identify the running build via VS_FIXEDFILEINFO, look up the gameInfo
// offset in kKnownBuilds, point at moduleBase+offset, validate against
// looksLikeSaveBlock(). Returns the address of dSv_save_c on success.
//
// Validation uses the same comprehensive check the content scan runs
// (string-shape + numeric-range across ~12 save fields), not a fixed
// magic like "Link" at +0x1B4 — that would fail the moment a user
// renames their save slot, leaving the fast path permanently disabled
// for them. Structural validation survives any user-editable content.
//
// Returns nullopt when: process has no version resource, build isn't in
// the table, save isn't loaded yet (target reads as zeros), or the
// offset has drifted from a stale table entry. Connect() falls through
// to the content scan in any of those cases.
std::optional<std::uintptr_t> tryKnownBuildFastPath(HANDLE h, DWORD pid) {
    const auto mod = findModule(pid, kProcessName);
    if (!mod) return std::nullopt;
    const std::string path = findModulePath(pid, kProcessName);
    if (path.empty()) return std::nullopt;
    const auto ver = readFileVersion(path.c_str());
    if (!ver) return std::nullopt;
    const KnownBuild* kb = lookupKnownBuild(*ver);
    if (!kb) return std::nullopt;

    const std::uintptr_t gameInfoAddr = mod->base + kb->gameInfoOffset;
    const std::uintptr_t saveAddr     = gameInfoAddr + kSaveBlockOffsetFromGameInfo;

    std::vector<std::uint8_t> buf(tpt::core::kSaveBlockSize);
    SIZE_T got = 0;
    if (!::ReadProcessMemory(h, reinterpret_cast<LPCVOID>(saveAddr),
                             buf.data(), buf.size(), &got)
        || got != buf.size()) {
        return std::nullopt;
    }
    if (!looksLikeSaveBlock(buf.data(), buf.size())) return std::nullopt;
    return saveAddr;
}

// ============================================================================
// Path 3 entry: AOB-pattern fallback
// ============================================================================
// Last-resort attach used only when both the build-offset table (Path 1)
// and the writable-memory content scan (Path 2) have come up empty.
// Reuses the same kGameInfoLEAPattern scan that --dusk-probe relies on:
// parse .text, recognize a tail-call wrapper around g_dComIfG_gameInfo,
// recover the LEA's disp32 to get gameInfoAddr.
//
// Sits third because AOB is brittle to compiler/linker changes: any
// re-ordering of .text that shifts the wrapper out of its 32-byte shape
// invalidates the pattern. The content scan is more durable — it
// validates against TP's save *file format*, which is fixed by the
// on-disk format spec rather than by codegen. AOB earns its place only
// because it can succeed where content scan returns nothing (e.g. if
// the validator drifts vs. a future Dusk save layout).
std::optional<std::uintptr_t> tryAobFallback(HANDLE h, DWORD pid) {
    const auto mod = findModule(pid, kProcessName);
    if (!mod) return std::nullopt;
    const auto sect = findTextSection(h, *mod);
    if (!sect) return std::nullopt;
    std::vector<std::uint8_t> textBuf(sect->second);
    SIZE_T got = 0;
    if (!::ReadProcessMemory(h, reinterpret_cast<LPCVOID>(sect->first),
                             textBuf.data(), textBuf.size(), &got)
        || got != textBuf.size()) {
        return std::nullopt;
    }
    const std::uintptr_t gameInfoAddr =
        aobLocateGameInfoIn(textBuf, sect->first);
    if (!gameInfoAddr) return std::nullopt;
    const std::uintptr_t saveAddr = gameInfoAddr + kSaveBlockOffsetFromGameInfo;

    std::vector<std::uint8_t> buf(tpt::core::kSaveBlockSize);
    if (!::ReadProcessMemory(h, reinterpret_cast<LPCVOID>(saveAddr),
                             buf.data(), buf.size(), &got)
        || got != buf.size()) {
        return std::nullopt;
    }
    if (!looksLikeSaveBlock(buf.data(), buf.size())) return std::nullopt;
    return saveAddr;
}

#endif  // _WIN32

std::optional<DuskProbeInfo> probeDusk() {
#ifdef _WIN32
    DuskProbeInfo info{};
    info.pid = findPidByName(kProcessName);
    if (!info.pid) return std::nullopt;

    HANDLE h = ::OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
                             FALSE, info.pid);
    if (!h) return std::nullopt;
    info.process = h;

    const auto mod = findModule(info.pid, kProcessName);
    if (mod) {
        info.moduleBase = mod->base;
        info.moduleSize = mod->size;

        std::vector<std::uint8_t> textBuf;
        if (auto sect = findTextSection(h, *mod)) {
            info.textBase = sect->first;
            info.textSize = sect->second;
            textBuf.resize(info.textSize);
            SIZE_T got = 0;
            if (!::ReadProcessMemory(h,
                    reinterpret_cast<LPCVOID>(info.textBase),
                    textBuf.data(), textBuf.size(), &got)
                || got != textBuf.size()) {
                textBuf.clear();
            }
        }

        info.gameInfoAddr = resolveSymbol(h, *mod, kSymbolName);

        // Production scanners — both run independently of the PDB so the
        // probe doubles as a self-test for whichever path connect() relies
        // on. AOB is the fast happy path; content scan is the resilient
        // fallback. The probe surfaces both plus the full content-candidate
        // list so we can see whether disambiguation is working.
        if (!textBuf.empty()) {
            info.aobGameInfoAddr = aobLocateGameInfoIn(textBuf, info.textBase);
        }
        info.contentCandidates    = findAllSaveBlockCandidates(h);
        info.contentSaveBlockAddr = pickLiveCandidate(h, info.contentCandidates);

        // Sample the first 16 bytes of each candidate so the printer can
        // decode live-state fields per-candidate. Lets us see, e.g.,
        // whether the "save-to-file staging buffer" has caught up to
        // the live save after the user pressed Save.
        info.contentCandidateHeads.resize(info.contentCandidates.size());
        for (std::size_t i = 0; i < info.contentCandidates.size(); ++i) {
            info.contentCandidateHeads[i].resize(16);
            SIZE_T got = 0;
            if (!::ReadProcessMemory(h,
                    reinterpret_cast<LPCVOID>(info.contentCandidates[i]),
                    info.contentCandidateHeads[i].data(),
                    info.contentCandidateHeads[i].size(), &got)
                || got != info.contentCandidateHeads[i].size()) {
                info.contentCandidateHeads[i].clear();
            }
        }

        if (info.gameInfoAddr) {
            info.saveBlockAddr = info.gameInfoAddr + kSaveBlockOffsetFromGameInfo;

            // List of LEAs targeting gameInfoAddr — useful when we need to
            // re-derive the pattern (e.g. for a new Dusk release).
            if (!textBuf.empty()) {
                info.leaMatches = scanLeaReferences(
                    textBuf, info.textBase, info.gameInfoAddr);
            }
        }

        // 512 bytes at the "best" guess for g_dComIfG_gameInfo. PDB wins
        // when available; otherwise AOB. This lets the human eyeball the
        // bytes (struct layout, pointer indirection, etc.) when content
        // scan and AOB disagree.
        const std::uintptr_t dumpAddr = info.gameInfoAddr
            ? info.gameInfoAddr : info.aobGameInfoAddr;
        if (dumpAddr) {
            info.gameInfoDump.resize(512);
            SIZE_T got = 0;
            if (!::ReadProcessMemory(h,
                    reinterpret_cast<LPCVOID>(dumpAddr),
                    info.gameInfoDump.data(), info.gameInfoDump.size(), &got)
                || got != info.gameInfoDump.size()) {
                info.gameInfoDump.clear();
            }
        }

        // Parallel dump from the content-scan candidate. Used to diff
        // against the AOB/PDB dump and determine which one tracks live
        // gameplay state.
        if (info.contentSaveBlockAddr) {
            info.contentDump.resize(512);
            SIZE_T got = 0;
            if (!::ReadProcessMemory(h,
                    reinterpret_cast<LPCVOID>(info.contentSaveBlockAddr),
                    info.contentDump.data(), info.contentDump.size(), &got)
                || got != info.contentDump.size()) {
                info.contentDump.clear();
            }
        }
    }
    return info;
#else
    return std::nullopt;
#endif
}

DuskSource::DuskSource() = default;

DuskSource::~DuskSource() {
    disconnect();
}

bool DuskSource::connect() {
#ifdef _WIN32
    if (process_) return isConnected();

    const DWORD pid = findPidByName(kProcessName);
    if (!pid) return false;

    HANDLE h = ::OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
                             FALSE, pid);
    if (!h) return false;

    // Region-specific settings the translation layer needs. Dusk is
    // region-agnostic at the binary level, but downstream readBytes()
    // still translates GC virtual addresses, so we have to pick one.
    // US only today.
    gcSaveBlockVA_ = tpt::core::kRegions[0].saveAddr;  // US
    gameId_        = "GZ2E01";

    // Attach proceeds in order of decreasing speed / increasing
    // breadth-of-cases-covered:
    //
    //   Path 1 (fast)    — known-build offset table. Single
    //                      ReadProcessMemory + structural validator. No
    //                      scanning, no multi-candidate disambiguation,
    //                      no tick() bookkeeping.
    //   Path 2 (content) — sweep all writable regions for save-shaped
    //                      buffers; pick initial by timeline/score;
    //                      tick() refines on observed writes. Survives
    //                      compiler/linker changes that don't affect
    //                      TP's save *file format*.
    //   Path 3 (AOB)     — recover g_dComIfG_gameInfo via a tail-call
    //                      wrapper pattern in .text. Brittle to .text
    //                      reshuffles, used only when content scan
    //                      returns nothing.
    //
    // Each path validates with looksLikeSaveBlock() against the target
    // address before committing — same structural check Path 2 already
    // uses, so an offset that survives a code reshuffle but lands on
    // junk degrades to "fall through" rather than "mis-attach".
    std::uintptr_t resolved = 0;

    if (const auto fast = tryKnownBuildFastPath(h, pid)) {
        resolved = *fast;
        // candidates_ stays empty → tick() naturally becomes a no-op
        // (see DuskSource::tick(): early return on candidates_.empty()).
    } else {
        candidates_ = findAllSaveBlockCandidates(h);
        if (!candidates_.empty()) {
            // Seed live-update tracking: capture each candidate's bytes
            // so tick() can detect future writes by diff and re-select
            // the live one across save events / slot switches.
            lastSnapshots_.clear();
            lastChangedTick_.clear();
            lastSnapshots_.reserve(candidates_.size());
            lastChangedTick_.reserve(candidates_.size());
            tickCounter_  = 0;
            lastTickWall_ = {};
            for (auto addr : candidates_) {
                std::vector<std::uint8_t> snap(tpt::core::kSaveBlockSize);
                SIZE_T got = 0;
                if (!::ReadProcessMemory(h, reinterpret_cast<LPCVOID>(addr),
                                         snap.data(), snap.size(), &got)
                    || got != snap.size()) {
                    snap.clear();
                }
                lastSnapshots_.push_back(std::move(snap));
                lastChangedTick_.push_back(0);
            }
            resolved = pickInitialCandidate(h, candidates_);
            if (!resolved) {
                // Defensive: every candidate's initial read failed. Take
                // the highest-addressed one — live globals tend to be
                // declared after auxiliary buffers in .data.
                resolved = *std::max_element(candidates_.begin(),
                                             candidates_.end());
            }
        } else if (const auto aob = tryAobFallback(h, pid)) {
            resolved = *aob;
            // candidates_ stays empty → tick() no-op (same as Path 1).
        }
    }

    if (!resolved) {
        // Nothing attached — close the handle and leave the source in
        // its disconnected default state. (Pre-existing behaviour leaked
        // the handle on this path; restructuring to defer process_
        // assignment until success closes the leak.)
        ::CloseHandle(h);
        candidates_.clear();
        lastSnapshots_.clear();
        lastChangedTick_.clear();
        gcSaveBlockVA_ = 0;
        gameId_.clear();
        return false;
    }

    process_       = h;
    saveBlockAddr_ = resolved;
    return true;
#else
    return false;
#endif
}

void DuskSource::disconnect() {
#ifdef _WIN32
    if (process_) {
        ::CloseHandle(static_cast<HANDLE>(process_));
        process_ = nullptr;
    }
#endif
    saveBlockAddr_ = 0;
    gcSaveBlockVA_ = 0;
    gameId_.clear();
    candidates_.clear();
    lastSnapshots_.clear();
    lastChangedTick_.clear();
    tickCounter_  = 0;
    lastTickWall_ = {};
}

bool DuskSource::isConnected() const {
#ifdef _WIN32
    if (!process_ || !saveBlockAddr_) return false;
    DWORD exitCode = 0;
    if (!::GetExitCodeProcess(static_cast<HANDLE>(process_), &exitCode)) return false;
    return exitCode == STILL_ACTIVE;
#else
    return false;
#endif
}

std::string DuskSource::gameId() const {
    return isConnected() ? gameId_ : std::string{};
}

std::optional<std::uintptr_t> DuskSource::translate(std::uint32_t gcAddr,
                                                    std::size_t size) const {
    if (!saveBlockAddr_ || !gcSaveBlockVA_) return std::nullopt;
    const std::uint32_t lo = gcSaveBlockVA_;
    const std::uint32_t hi = gcSaveBlockVA_ + tpt::core::kSaveBlockSize;
    if (gcAddr < lo || gcAddr + size > hi) return std::nullopt;
    return saveBlockAddr_ + (gcAddr - lo);
}

bool DuskSource::readBytes(std::uint32_t addr, void* out, std::size_t size) const {
#ifdef _WIN32
    if (!isConnected()) return false;
    const auto local = translate(addr, size);
    if (!local) return false;
    SIZE_T got = 0;
    const BOOL ok = ::ReadProcessMemory(static_cast<HANDLE>(process_),
        reinterpret_cast<LPCVOID>(*local), out, size, &got);
    return ok && got == size;
#else
    (void)addr; (void)out; (void)size;
    return false;
#endif
}

bool DuskSource::writeBytes(std::uint32_t /*addr*/,
                            const void* /*data*/,
                            std::size_t /*size*/) const {
    return false;
}

bool DuskSource::isAvailable() {
#ifdef _WIN32
    return findPidByName(kProcessName) != 0;
#else
    return false;
#endif
}

void DuskSource::tick() {
#ifdef _WIN32
    if (!process_ || candidates_.empty()) return;

    // Throttle: at most one diff sweep every ~500ms. The poll loop in
    // UIState calls us at that cadence already, but defensively we
    // enforce it here too in case the source is shared with code that
    // ticks more often.
    const auto now = std::chrono::steady_clock::now();
    if (lastTickWall_.time_since_epoch().count() != 0
        && now - lastTickWall_ < std::chrono::milliseconds(450)) {
        return;
    }
    lastTickWall_ = now;

    ++tickCounter_;
    auto* h = static_cast<HANDLE>(process_);

    // Read each candidate; mark "changed at this tick" if the bytes differ
    // from the previously-captured snapshot. Save events temporarily make
    // staging buffers update too, which would spike them to current
    // tickCounter_; that's fine — the live save will update again on the
    // very next gameplay event and reclaim the title. We always pick the
    // most-recently-changed candidate (highest lastChangedTick_), so this
    // is self-healing across save events.
    std::vector<std::uint8_t> buf(tpt::core::kSaveBlockSize);
    for (std::size_t i = 0; i < candidates_.size(); ++i) {
        SIZE_T got = 0;
        if (!::ReadProcessMemory(h, reinterpret_cast<LPCVOID>(candidates_[i]),
                                 buf.data(), buf.size(), &got)
            || got != buf.size()) {
            continue;
        }
        if (lastSnapshots_[i].size() != buf.size()
            || std::memcmp(lastSnapshots_[i].data(), buf.data(), buf.size()) != 0) {
            lastChangedTick_[i] = tickCounter_;
            lastSnapshots_[i] = buf;  // copy, buf is reused next iter
        }
    }

    // Pick candidate with newest change. Tiebreaker: highest address (a
    // mild preference; matters only when no candidate has ever changed
    // since connect()).
    std::size_t bestIdx = 0;
    for (std::size_t i = 1; i < candidates_.size(); ++i) {
        if (lastChangedTick_[i] > lastChangedTick_[bestIdx]
            || (lastChangedTick_[i] == lastChangedTick_[bestIdx]
                && candidates_[i] > candidates_[bestIdx])) {
            bestIdx = i;
        }
    }
    saveBlockAddr_ = candidates_[bestIdx];
#endif
}

}  // namespace tpt::dusk
