#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace tpt::game::oot::save {

// Decoded view of all the bit-flag arrays in SaveContext. Filled by
// readSaveFlags(); queried by the check-completion lookup in Checks.cpp.
//
// All bit reads are done at decode time so the rest of the tracker
// doesn't have to know the on-disk endianness. Each bitmask is held as
// a host-native unsigned int.
//
// Source layout: oot-main/include/save.h:SaveInfo (sceneFlags, gsFlags,
// eventChkInf, itemGetInf, infTable). Bit math for sceneFlags is
// straightforward (one u32 per chest/collect/swch/clear/rooms/floors per
// scene). gsFlags packs 8-bit per-scene fields into 6 u32s — see
// oot-main/src/code/z_inventory.c:gGsFlagsMasks.
struct SceneFlags {
    std::uint32_t chest   = 0;
    std::uint32_t swch    = 0;
    std::uint32_t clear   = 0;
    std::uint32_t collect = 0;
    // The decomp field name is `unk`; per community reverse-engineering
    // this is the scrub-purchase bitmask + other miscellaneous one-time
    // actor flags. Not yet consumed by Checks.cpp's completion logic —
    // reserved here so scrub tracking can be added without bumping the
    // save-flag read pipeline.
    std::uint32_t unk     = 0;
    std::uint32_t rooms   = 0;
    std::uint32_t floors  = 0;
};

struct SaveFlags {
    std::array<SceneFlags, 124>    scenes{};
    // 24 packed 8-bit slots, one per gold-skulltula scene.
    std::array<std::uint8_t, 24>   gsFlags{};
    // 14 × 16 = 224 bits of event flags.
    std::array<std::uint16_t, 14>  eventChkInf{};
    // 4 × 16 = 64 bits of one-time-pickup flags.
    std::array<std::uint16_t, 4>   itemGetInf{};
    // 30 × 16 = 480 bits of NPC-interaction flags.
    std::array<std::uint16_t, 30>  infTable{};
};

// Decode the flag arrays out of a SaveContext slice. `saveContext` must
// contain at least kHeaderReadSize bytes (covers end of infTable at
// 0xF34). Returns a default-constructed result on short input.
SaveFlags readSaveFlags(std::span<const std::uint8_t> saveContext);

// Bit-test helpers. Caller has already decoded SaveFlags; these are
// just typed accessors so the check-completion code reads cleanly.

// Bit `bit` of scenes[scene].chest. `bit` is 0..31. Used for "Chest"
// locations where OoTR's `default` field gives the chest bit index.
bool chestBit(const SaveFlags& f, std::uint8_t scene, std::uint8_t bit);

// Bit `bit` of scenes[scene].collect. Used for "Collectable" /
// "ActorOverride" locations.
bool collectBit(const SaveFlags& f, std::uint8_t scene, std::uint8_t bit);

// Bit `bit` of scenes[scene].clear (room-cleared flags). Used for some
// boss-defeat locations.
bool clearBit(const SaveFlags& f, std::uint8_t scene, std::uint8_t bit);

// A token whose collection bit is in gsFlags[gsScene] masked by `mask`.
// OoTR encodes the mask directly as a `default` of e.g. 0x01, 0x02, 0x04
// — one bit per token per scene, up to 8 tokens per scene.
bool gsTokenBit(const SaveFlags& f, std::uint8_t gsScene, std::uint8_t mask);

// A flat bit index into eventChkInf. `bit` is 0..223. byte = bit/16,
// position = bit%16. Used for "Boss", "Song", "Cutscene", "Event" types.
bool eventBit(const SaveFlags& f, std::uint16_t bit);

// A flat bit index into itemGetInf (0..63). Used for some "NPC" /
// "Scrub" / "BossHeart" types — those where the rando's give-item hook
// stores its completion bit here.
bool itemGetBit(const SaveFlags& f, std::uint16_t bit);

// A flat bit index into infTable (0..479). Used for NPC interaction
// progress where itemGetInf isn't the right home.
bool infTableBit(const SaveFlags& f, std::uint16_t bit);

}  // namespace tpt::game::oot::save
