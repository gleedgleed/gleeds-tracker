#include "game/oot/save/Xflags.h"

#include <cstring>

#include "memory/MemorySource.h"

namespace tpt::game::oot::save {

namespace {

// Big-endian readers — same convention as the rest of the OoT save
// pipeline. The xflag tables are stored in MIPS native (BE) order.
inline std::uint16_t rdU16(std::span<const std::uint8_t> b, std::size_t o) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(b[o]) << 8) | b[o + 1]);
}

}  // namespace

std::uint16_t getXflagBitOffset(const Xflag& flag, const XflagState& st) {
    if (!st.valid) return 0xFFFF;
    if (flag.scene >= kXflagSceneTableEntries) return 0xFFFF;

    // Step 1: scene_table[scene] gives a byte offset into room_table.
    // 0xFFFF means "scene has no xflag entries."
    if (flag.scene * 2u + 1u >= st.sceneTable.size() * 2u) return 0xFFFF;
    const std::uint16_t roomTableIndex = st.sceneTable[flag.scene];
    if (roomTableIndex == 0xFFFF) return 0xFFFF;
    if (roomTableIndex >= st.roomTable.size()) return 0xFFFF;

    // Step 2: walk the per-scene room_table entries.
    //   regular scenes:
    //     1 byte: number of (room,setup) entries
    //     for each: 1 byte (setup<<6 | room) + 2 bytes (roomBlob offset)
    //   grottos (scene 0x3E):
    //     1 byte: count
    //     for each: 1 byte setup_id (grotto_id), 1 byte room_id, 2 bytes offset
    const bool isGrotto = flag.scene == 0x3E;
    std::size_t i = roomTableIndex;
    if (i >= st.roomTable.size()) return 0xFFFF;
    const std::uint8_t entryCount = st.roomTable[i++];

    std::uint16_t roomByteOffset = 0xFFFF;
    for (std::uint8_t e = 0; e < entryCount; ++e) {
        if (i + 3 >= st.roomTable.size()) return 0xFFFF;
        std::uint8_t setupId;
        std::uint8_t roomId;
        if (isGrotto) {
            setupId = st.roomTable[i++];
            roomId  = st.roomTable[i++];
        } else {
            const std::uint8_t packed = st.roomTable[i++];
            setupId = (packed & 0xC0) >> 6;
            roomId  = packed & 0x3F;
        }

        const std::uint16_t blobOff =
            static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(st.roomTable[i]) << 8) |
                 static_cast<std::uint16_t>(st.roomTable[i + 1]));
        i += 2;

        const std::uint8_t flagRoom  = isGrotto ? flag.room    : flag.room;
        const std::uint8_t flagSetup = isGrotto ? flag.grottoId : flag.setup;
        if (roomId == flagRoom && setupId == flagSetup) {
            roomByteOffset = blobOff;
            break;
        }
    }
    if (roomByteOffset == 0xFFFF) return 0xFFFF;
    if (roomByteOffset + 3 >= st.roomBlob.size()) return 0xFFFF;

    // Step 3: read the per-room header from roomBlob.
    //   uint16 base bit offset
    //   uint8  rlc_size (byte length of the run-length data, halved)
    //   ... RLC tokens (token byte, length byte) ...
    // The RLC expands to a table indexed by actor index; each entry is
    // the bit offset within this room for that actor's flag.
    const std::uint16_t baseBitOffset =
        static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(st.roomBlob[roomByteOffset]) << 8) |
             static_cast<std::uint16_t>(st.roomBlob[roomByteOffset + 1]));
    std::size_t p = roomByteOffset + 2;
    const std::uint8_t rlcSizeHalved = st.roomBlob[p++] / 2;

    // Decode the RLC, accumulating per-actor bit offsets. We only need
    // the entry at `flag.flag` (the actor index OoTR encoded into the
    // xflag's `flag` field); short-circuit when we've reached it.
    std::uint8_t sum = 0;
    std::size_t actorIdx = 0;
    const std::size_t target = flag.flag;
    std::uint8_t actorBitOffset = 0;  // 0 means "no flag for this actor"
    bool found = false;

    for (std::uint8_t t = 0; t < rlcSizeHalved; ++t) {
        if (p + 1 >= st.roomBlob.size()) return 0xFFFF;
        const std::uint8_t token  = st.roomBlob[p++];
        const std::uint8_t length = st.roomBlob[p++];
        for (std::uint8_t k = 0; k < length; ++k) {
            sum += token;
            if (actorIdx == target) {
                if (token != 0) actorBitOffset = sum;
                found = true;
                break;
            }
            ++actorIdx;
        }
        if (found) break;
    }
    if (!found || actorBitOffset == 0) return 0xFFFF;

    // Step 4: combine base + per-actor + subflag.
    return baseBitOffset + actorBitOffset - 1u + flag.subflag;
}

bool isXflagSet(const Xflag& flag, const XflagState& st) {
    const std::uint16_t off = getXflagBitOffset(flag, st);
    if (off == 0xFFFF) return false;
    const std::size_t byteIdx = off / 8;
    if (byteIdx >= st.collectibleFlags.size()) return false;
    // OoTR stores bits MSB-first within each byte:
    // collectible_override_flags[off/8] & (0x80 >> (off%8))
    const std::uint8_t mask = static_cast<std::uint8_t>(0x80u >> (off % 8u));
    return (st.collectibleFlags[byteIdx] & mask) != 0;
}

namespace {

// Per-build OoTR address table. Each row maps a unique build
// identifier (the value at RANDO_CONTEXT[2], i.e. the extern_ctxt
// pointer for that build) to the absolute N64 addresses of the OoTR
// symbols we read. Different OoTR commits ship the same symbols at
// different addresses because the linker output depends on what code
// is in front of them — but within a single build the addresses are
// fixed. Adding support for a new build is a single new entry below.
//
// Discovery procedure for a new build: run `tptracker.exe --oot-extras`
// against the running ROM, note the `slot[2]` value, defeat any boss
// once, re-run --oot-extras, scroll to the "Scan for extended_savectx
// candidates" section and grab the address where bools[8] reads
// `01 00 00 00 00 00 00 00`.
struct OotrBuild {
    std::uint32_t externCtxtValue;     // RANDO_CONTEXT[2] — build ID
    std::uint32_t extendedSavectx;     // absolute N64 address
    std::uint32_t collectibleFlagsPtr; // absolute N64 address (u32* + u16 count at +4)
    std::uint32_t xflagSceneTable;
    std::uint32_t xflagRoomTable;
    std::uint32_t xflagRoomBlob;
};

constexpr OotrBuild kKnownBuilds[] = {
    // Build matching OoT-Randomizer-Dev/ASM/build/asm_symbols.txt
    // (checked-in reference snapshot).
    {
        /*externCtxtValue=*/    0x8042B56C,
        /*extendedSavectx=*/    0x8044A120,
        /*collectibleFlagsPtr=*/0x8042FA80,
        /*xflagSceneTable=*/    0x8043157C,
        /*xflagRoomTable=*/     0x804312C0,
        /*xflagRoomBlob=*/      0x80430708,
    },
    // Previously had a second entry for externCtxt=0x8042AFC8 with an
    // extendedSavectx address located by content-scan. That address
    // (0x80425E85) was misaligned and a likely false-positive from the
    // scan — produced "Queen Gohma defeated on fresh save" because the
    // sanity check (silver_rupee_counts[i] <= 30) is easily satisfied
    // by random RAM. Removed 2026-05-17. Boss completion now reads
    // vanilla sceneFlags[bossRoom].collect bit 31, so this build no
    // longer needs an extended_savectx entry just to track bosses.
    //
    // Adding a new build entry: discover externCtxt via --oot-extras,
    // then resolve each symbol with an independent method (not just a
    // content-scan around expected values).
};

const OotrBuild* findBuild(std::uint32_t externCtxt) {
    for (const auto& b : kKnownBuilds) {
        if (b.externCtxtValue == externCtxt) return &b;
    }
    return nullptr;
}

}  // namespace

bool resolveOotrAddrs(tpt::memory::MemorySource& mem, OotrAddrs& out) {
    out = {};
    std::uint8_t externPtrBytes[4]{};
    if (!mem.readBytes(kAddrRandoContext + kRandoContextExternCtxtOff,
                       externPtrBytes, sizeof(externPtrBytes))) {
        return false;
    }
    const std::uint32_t externCtxt =
        (static_cast<std::uint32_t>(externPtrBytes[0]) << 24) |
        (static_cast<std::uint32_t>(externPtrBytes[1]) << 16) |
        (static_cast<std::uint32_t>(externPtrBytes[2]) << 8)  |
         static_cast<std::uint32_t>(externPtrBytes[3]);
    if ((externCtxt & 0xFF000000u) != 0x80000000u ||
        externCtxt >= 0x80800000u) {
        return false;
    }
    out.externCtxt = externCtxt;

    // Look up known build. Unknown builds get all-zero symbol
    // addresses and the fetch functions' sanity checks then keep all
    // OoTR-specific check types as [?]. Adding a new build is a
    // one-line entry in kKnownBuilds above.
    if (const OotrBuild* b = findBuild(externCtxt)) {
        out.extendedSavectx     = b->extendedSavectx;
        out.collectibleFlagsPtr = b->collectibleFlagsPtr;
        out.numOverrideFlags    = b->collectibleFlagsPtr
                                ? b->collectibleFlagsPtr + 4 : 0;
        out.xflagSceneTable     = b->xflagSceneTable;
        out.xflagRoomTable      = b->xflagRoomTable;
        out.xflagRoomBlob       = b->xflagRoomBlob;
    }
    // Mark valid as long as extern_ctxt resolved — fetch functions
    // already gate per-symbol on (out.x != 0) before reading.
    out.valid = true;
    return true;
}

bool fetchXflagTables(tpt::memory::MemorySource& mem, XflagState& out) {
    out.valid = false;
    out.sceneTable.assign(kXflagSceneTableEntries, 0);
    out.roomTable.assign(kXflagRoomTableBytes, 0);
    out.roomBlob.assign(kXflagRoomBlobBytes, 0);
    if (!out.ootrAddrs.valid) return false;
    if (out.ootrAddrs.xflagSceneTable == 0 ||
        out.ootrAddrs.xflagRoomTable  == 0 ||
        out.ootrAddrs.xflagRoomBlob   == 0) {
        return false;  // unknown OoTR build — leave xflag types as [?]
    }

    // Read raw scene-table bytes then byte-swap into uint16s. We can't
    // reinterpret_cast directly because of alignment and host byte
    // order — and our MemorySource already byte-swap-corrects per word,
    // so the bytes we receive are in N64-native MIPS BE order. The
    // sceneTable entries are u16s, so 2 bytes each big-endian.
    std::vector<std::uint8_t> sceneRaw(kXflagSceneTableBytes, 0);
    if (!mem.readBytes(out.ootrAddrs.xflagSceneTable,
                       sceneRaw.data(), sceneRaw.size())) {
        return false;
    }
    for (std::size_t i = 0; i < kXflagSceneTableEntries; ++i) {
        out.sceneTable[i] = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(sceneRaw[i * 2]) << 8) |
             static_cast<std::uint16_t>(sceneRaw[i * 2 + 1]));
    }

    if (!mem.readBytes(out.ootrAddrs.xflagRoomTable,
                       out.roomTable.data(), out.roomTable.size())) {
        return false;
    }
    if (!mem.readBytes(out.ootrAddrs.xflagRoomBlob,
                       out.roomBlob.data(), out.roomBlob.size())) {
        return false;
    }
    out.valid = true;
    return true;
}

bool fetchExtendedSavectx(tpt::memory::MemorySource& mem, XflagState& out) {
    out.extendedSavectxValid = false;
    out.collectedDungeonRewards.fill(0);
    if (!out.ootrAddrs.valid) return false;
    if (out.ootrAddrs.extendedSavectx == 0) return false;

    // Read the whole 30-byte prefix (silver_rupee_counts[22] + the 8 boss
    // bytes that follow) so we can sanity-check what we got. Address comes
    // from out.ootrAddrs.extendedSavectx, resolved via extern_ctxt + a
    // fixed offset (see resolveOotrAddrs / Xflags.h). If our offset
    // hypothesis is wrong for the loaded OoTR build, the sanity checks
    // below fail closed and bosses stay [?] rather than going false [x].
    std::array<std::uint8_t,
               kCollectedDungeonRewardsOffset + kCollectedDungeonRewardsBytes> chunk{};
    if (!mem.readBytes(out.ootrAddrs.extendedSavectx,
                       chunk.data(), chunk.size())) {
        return false;
    }

    // Sanity 1: silver_rupee_counts[i] is bounded by the silver-rupee
    // count per puzzle. Max in OoTR is well below 30. Anything beyond
    // that here is non-rupee data — wrong address.
    for (std::size_t i = 0; i < kCollectedDungeonRewardsOffset; ++i) {
        if (chunk[i] > 30) return false;
    }
    // Sanity 2: collected_dungeon_rewards[i] is a C bool — 0 or 1.
    // Anything else is junk.
    for (std::size_t i = 0; i < kCollectedDungeonRewardsBytes; ++i) {
        if (chunk[kCollectedDungeonRewardsOffset + i] > 1) return false;
    }

    std::copy(chunk.begin() + kCollectedDungeonRewardsOffset,
              chunk.end(),
              out.collectedDungeonRewards.begin());
    out.extendedSavectxValid = true;
    return true;
}

bool fetchCollectibleFlags(tpt::memory::MemorySource& mem, XflagState& out) {
    out.collectibleFlags.clear();
    out.collectibleFlagsPtr = 0;
    out.numOverrideFlags    = 0;
    if (!out.ootrAddrs.valid) return false;
    if (out.ootrAddrs.collectibleFlagsPtr == 0 ||
        out.ootrAddrs.numOverrideFlags    == 0) {
        return false;  // unknown build, address unmapped
    }

    // Read the pointer (N64 stores it BE-packed; our readBytes returns
    // MIPS-native bytes already in BE order, so manual assemble).
    std::uint8_t ptrBytes[4]{};
    std::uint8_t countBytes[2]{};
    if (!mem.readBytes(out.ootrAddrs.collectibleFlagsPtr,
                       ptrBytes, sizeof(ptrBytes)) ||
        !mem.readBytes(out.ootrAddrs.numOverrideFlags,
                       countBytes, sizeof(countBytes))) {
        return false;
    }
    const std::uint32_t ptr =
        (static_cast<std::uint32_t>(ptrBytes[0]) << 24) |
        (static_cast<std::uint32_t>(ptrBytes[1]) << 16) |
        (static_cast<std::uint32_t>(ptrBytes[2]) << 8)  |
         static_cast<std::uint32_t>(ptrBytes[3]);
    const std::uint16_t count =
        static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(countBytes[0]) << 8) |
             static_cast<std::uint16_t>(countBytes[1]));
    out.collectibleFlagsPtr = ptr;
    out.numOverrideFlags    = count;

    if (ptr == 0 || count == 0 || count > 0x4000) {
        // Sanity bounds — `num_override_flags` is small in practice
        // (low hundreds). Anything larger means we mis-read or the
        // pointer is stale.
        return false;
    }
    // Sanity: N64 RDRAM lives at 0x80000000..0x807FFFFF (KSEG0/KSEG1
    // mapped). A pointer outside that range can't be a valid heap
    // allocation — typically means our baked kAddrCollectibleFlagsPtr
    // points at a different region in this OoTR build (e.g. webgen
    // commits where the symbol moved). Reject so xflag types stay [?]
    // rather than hand garbage to the bit-walk.
    if ((ptr & 0xFF000000u) != 0x80000000u || ptr >= 0x80800000u) {
        return false;
    }
    out.collectibleFlags.assign(count, 0);
    if (!mem.readBytes(ptr, out.collectibleFlags.data(), count)) {
        out.collectibleFlags.clear();
        return false;
    }
    return true;
}

}  // namespace tpt::game::oot::save
