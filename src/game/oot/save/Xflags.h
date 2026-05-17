#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace tpt::game::oot::save {

// "Xflag" — OoTR's extended flag identifier for collectibles that live
// outside vanilla SaveContext (Pots, Crates, Wonderitems, Silver Rupees,
// Freestanding items, Rupee Towers, Beehives, Flying Pots, Small Crates,
// Drops). Bitfield mirrors the layout in
// OoT-Randomizer-Dev/ASM/c/get_items.h `xflag_t`.
//
// Bytes 0:    [set:1][scene:7]
// Bytes 1-4:  union of the regular and grotto encodings.
//
// Two encodings:
//   - Regular (scene != 0x3E): pad(8), setup(2)+room(6), flag(8), subflag(8)
//   - Grotto  (scene == 0x3E): pad(8), grotto_id(5)+room(4)+flag(7)+subflag(8)
//
// The lookup function below understands both.
struct Xflag {
    bool          set      = false;
    std::uint8_t  scene    = 0;
    std::uint8_t  setup    = 0;  // 0..3
    std::uint8_t  room     = 0;
    std::uint8_t  flag     = 0;
    std::uint8_t  subflag  = 0;
    // Grotto-specific encoding (only used when scene == 0x3E).
    std::uint8_t  grottoId = 0;
};

// Runtime-resolved N64 addresses for OoTR-added symbols. The OoTR
// `RANDO_CONTEXT` lives at a stable address (0x80400000) across all OoTR
// builds — it's a 4-entry pointer table: { COOP_CONTEXT, COSMETIC_CONTEXT,
// extern_ctxt, AUTO_TRACKER_CONTEXT } (see OoT-Randomizer-Dev/ASM/src/
// build.asm). The targets of those pointers move per build as code/data
// shifts around, but the *relative offsets* between OoTR's data symbols
// stay stable within a payload version, so once we read extern_ctxt we
// can compute extended_savectx, collectible_override_flags, and the
// xflag tables via fixed offsets that we baked from our checked-in
// asm_symbols.txt.
//
// All fields zero/false when not yet resolved.
struct OotrAddrs {
    bool          valid                 = false;
    std::uint32_t externCtxt            = 0;  // RANDO_CONTEXT[2] value
    std::uint32_t collectibleFlagsPtr   = 0;  // u32* in N64 RAM
    std::uint32_t numOverrideFlags      = 0;  // u16
    std::uint32_t xflagSceneTable       = 0;
    std::uint32_t xflagRoomTable        = 0;
    std::uint32_t xflagRoomBlob         = 0;
    std::uint32_t extendedSavectx       = 0;
};

// Snapshot of all xflag-system RAM state, captured once per poll. The
// table addresses themselves are fixed per OoTR build (resolved via
// OotrAddrs); the contents change per seed and over time.
struct XflagState {
    // Runtime-resolved OoTR symbol addresses. Populated on first
    // successful resolveOotrAddrs(); reused for all subsequent fetches.
    OotrAddrs ootrAddrs{};
    bool valid = false;  // true when all reads succeeded

    // Source-of-truth pointer + size, both stored as fixed N64 globals
    // (dynamically heap-allocated at game-start; the *pointer's address*
    // is fixed, the value varies per session).
    std::uint32_t  collectibleFlagsPtr = 0;
    std::uint16_t  numOverrideFlags    = 0;

    // The byte array we look up bits in. Indexed bit-wise — see
    // isXflagSet(). Length == numOverrideFlags.
    std::vector<std::uint8_t> collectibleFlags;

    // xflag table contents, ROM-baked per seed.
    // xflag_scene_table[scene] -> 16-bit offset into xflag_room_table,
    //   or 0xFFFF if the scene has no xflags.
    std::vector<std::uint16_t> sceneTable;
    // xflag_room_table is bytes; layout is per-scene variable. We store
    // it raw and walk it in the lookup function.
    std::vector<std::uint8_t>  roomTable;
    // xflag_room_blob: holds the per-room/setup bit-offset header
    // (uint16 base offset + RLC byte stream).
    std::vector<std::uint8_t>  roomBlob;

    // OoTR's extended_savecontext_static_t.collected_dungeon_rewards.
    // One byte per boss reward; non-zero == collected. Indexed by
    // `boss_idx = OoTR_location_default - 5` for default ∈ [5,12]
    // (Gohma..Bongo Bongo per blue_warp.c).
    bool                                 extendedSavectxValid = false;
    std::array<std::uint8_t, 8>          collectedDungeonRewards{};
};

// Stable OoTR anchor: RANDO_CONTEXT lives at this fixed N64 address
// in every OoTR build (per OoT-Randomizer-Dev/ASM/src/build.asm). It's
// a 4-pointer table; the third pointer is extern_ctxt — our anchor for
// resolving all other OoTR symbol addresses.
inline constexpr std::uint32_t kAddrRandoContext         = 0x80400000;
inline constexpr std::uint32_t kRandoContextExternCtxtOff = 0x08;

// Per-build OoTR address tables live in Xflags.cpp — symbols shift
// non-uniformly between OoTR commits, so we identify the build via
// RANDO_CONTEXT[2] (extern_ctxt's runtime value) and look up that
// build's absolute symbol addresses. See kKnownBuilds in Xflags.cpp
// and the discovery procedure in its comment.

inline constexpr std::size_t   kXflagRoomBlobBytes       = 0xBB8;  // 2996
inline constexpr std::size_t   kXflagRoomTableBytes      = 0x2BC;  // 700
inline constexpr std::size_t   kXflagSceneTableBytes     = 0xCA;   // 101 × u16
inline constexpr std::size_t   kXflagSceneTableEntries   = kXflagSceneTableBytes / 2;

// extended_savecontext_static_t layout (OoT-Randomizer-Dev/ASM/c/save.h):
//   uint8_t silver_rupee_counts[0x16];   // +0x00 (22 bytes)
//   bool    collected_dungeon_rewards[8];// +0x16 (8 bytes — what we read)
//   override_t incoming_queue[3];        // +0x1E
//   uint8_t password[6];                 // +0x32
inline constexpr std::uint32_t kCollectedDungeonRewardsOffset = 0x16;
inline constexpr std::size_t   kCollectedDungeonRewardsBytes  = 8;

// Compute the bit offset of a given xflag into the collectibleFlags
// byte array. Returns 0xFFFF when the flag has no corresponding bit
// (unmapped actor or unknown scene). Direct port of OoTR's
// get_xflag_bit_offset() in get_items.c.
//
// Stateful caching is omitted — OoTR caches per-room to skip re-walking
// the table; we do the walk each call. ~50 ns per call, called once per
// xflag-bearing check at poll time (~1400 entries × ~50ns = ~70 µs).
std::uint16_t getXflagBitOffset(const Xflag& flag, const XflagState& st);

// Convenience: is this xflag set? Returns false when the flag doesn't
// resolve to any byte in the collectible_override_flags table (which is
// fine — that just means the seed isn't tracking this collectible).
bool isXflagSet(const Xflag& flag, const XflagState& st);

}  // namespace tpt::game::oot::save

namespace tpt::memory { class MemorySource; }

namespace tpt::game::oot::save {

// Resolve OoTR symbol addresses by reading RANDO_CONTEXT[2] (extern_ctxt)
// and adding our fixed offsets. Call once at session start; the result
// is stable until the ROM is reloaded. Returns false if RANDO_CONTEXT
// reads invalid (vanilla OoT ROM, or a non-OoTR ROM loaded) — callers
// should leave xflag/extended_savectx state unfetched in that case.
bool resolveOotrAddrs(tpt::memory::MemorySource& mem, OotrAddrs& out);

// Fetch the xflag table contents (one-shot, stable per session).
// Reads ~3.9 KB from N64 RAM. Call once after connect — the tables are
// fixed for the loaded ROM. Returns false if any of the three table
// reads fails (typically: not connected, or addrs not yet resolved).
bool fetchXflagTables(tpt::memory::MemorySource& mem, XflagState& out);

// Fetch the current collectible_override_flags pointer and bytes.
// Call every poll. Returns false on read failure; the existing
// `collectibleFlags` is cleared so callers see no completion until
// the next successful read.
bool fetchCollectibleFlags(tpt::memory::MemorySource& mem, XflagState& out);

// Fetch the OoTR collected_dungeon_rewards byte array (8 bytes from
// extended_savectx + 0x16). Call every poll. On failure, the array is
// zeroed and `extendedSavectxValid` stays false so completion lookups
// return nullopt ([?]) rather than a false [ ].
bool fetchExtendedSavectx(tpt::memory::MemorySource& mem, XflagState& out);

}  // namespace tpt::game::oot::save

