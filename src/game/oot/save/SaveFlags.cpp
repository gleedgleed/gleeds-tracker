#include "game/oot/save/SaveFlags.h"

#include "game/oot/save/SaveOffsets.h"

namespace tpt::game::oot::save {

namespace {

// Big-endian readers — readBytes() already un-byteswapped the P64 RDRAM
// to N64 native, so multi-byte values are stored high-byte-first here.
inline std::uint16_t rdU16(std::span<const std::uint8_t> b, std::uint32_t o) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(b[o]) << 8) | b[o + 1]);
}
inline std::uint32_t rdU32(std::span<const std::uint8_t> b, std::uint32_t o) {
    return (static_cast<std::uint32_t>(b[o])     << 24) |
           (static_cast<std::uint32_t>(b[o + 1]) << 16) |
           (static_cast<std::uint32_t>(b[o + 2]) << 8)  |
            static_cast<std::uint32_t>(b[o + 3]);
}

}  // namespace

SaveFlags readSaveFlags(std::span<const std::uint8_t> sc) {
    SaveFlags out;
    if (sc.size() < kOffInfTable + kInfTableWords * 2) return out;

    for (std::uint32_t i = 0; i < kSceneFlagsCount; ++i) {
        const std::uint32_t base = kOffSceneFlagsBase +
            i * static_cast<std::uint32_t>(kSceneFlagsStride);
        auto& s = out.scenes[i];
        s.chest   = rdU32(sc, base + kSceneFlagOffChest);
        s.swch    = rdU32(sc, base + kSceneFlagOffSwch);
        s.clear   = rdU32(sc, base + kSceneFlagOffClear);
        s.collect = rdU32(sc, base + kSceneFlagOffCollect);
        s.unk     = rdU32(sc, base + kSceneFlagOffUnk);
        s.rooms   = rdU32(sc, base + kSceneFlagOffRooms);
        s.floors  = rdU32(sc, base + kSceneFlagOffFloors);
    }

    // gsFlags packs 4 scenes per u32 (one byte each). Unpack into the
    // 24-entry array so callers index by gs-scene directly.
    for (std::uint32_t w = 0; w < kGsFlagsCount; ++w) {
        const std::uint32_t word = rdU32(sc, kOffGsFlags + w * 4);
        for (std::uint32_t b = 0; b < kGsScenesPerWord; ++b) {
            // gGsFlagsShifts in z_inventory.c: 0, 8, 16, 24 — byte 0 is
            // the lowest 8 bits of the word.
            out.gsFlags[w * kGsScenesPerWord + b] =
                static_cast<std::uint8_t>((word >> (b * 8)) & 0xFF);
        }
    }

    for (std::uint32_t i = 0; i < kEventChkInfWords; ++i) {
        out.eventChkInf[i] = rdU16(sc, kOffEventChkInf + i * 2);
    }
    for (std::uint32_t i = 0; i < kItemGetInfWords; ++i) {
        out.itemGetInf[i] = rdU16(sc, kOffItemGetInf + i * 2);
    }
    for (std::uint32_t i = 0; i < kInfTableWords; ++i) {
        out.infTable[i] = rdU16(sc, kOffInfTable + i * 2);
    }
    return out;
}

bool chestBit(const SaveFlags& f, std::uint8_t scene, std::uint8_t bit) {
    if (scene >= f.scenes.size() || bit >= 32) return false;
    return (f.scenes[scene].chest & (std::uint32_t{1} << bit)) != 0;
}

bool collectBit(const SaveFlags& f, std::uint8_t scene, std::uint8_t bit) {
    if (scene >= f.scenes.size() || bit >= 32) return false;
    return (f.scenes[scene].collect & (std::uint32_t{1} << bit)) != 0;
}

bool clearBit(const SaveFlags& f, std::uint8_t scene, std::uint8_t bit) {
    if (scene >= f.scenes.size() || bit >= 32) return false;
    return (f.scenes[scene].clear & (std::uint32_t{1} << bit)) != 0;
}

bool gsTokenBit(const SaveFlags& f, std::uint8_t gsScene, std::uint8_t mask) {
    if (gsScene >= f.gsFlags.size()) return false;
    return (f.gsFlags[gsScene] & mask) != 0;
}

bool eventBit(const SaveFlags& f, std::uint16_t bit) {
    const std::size_t word = bit / 16;
    const std::size_t pos  = bit % 16;
    if (word >= f.eventChkInf.size()) return false;
    return (f.eventChkInf[word] & (std::uint16_t{1} << pos)) != 0;
}

bool itemGetBit(const SaveFlags& f, std::uint16_t bit) {
    const std::size_t word = bit / 16;
    const std::size_t pos  = bit % 16;
    if (word >= f.itemGetInf.size()) return false;
    return (f.itemGetInf[word] & (std::uint16_t{1} << pos)) != 0;
}

bool infTableBit(const SaveFlags& f, std::uint16_t bit) {
    const std::size_t word = bit / 16;
    const std::size_t pos  = bit % 16;
    if (word >= f.infTable.size()) return false;
    return (f.infTable[word] & (std::uint16_t{1} << pos)) != 0;
}

}  // namespace tpt::game::oot::save
