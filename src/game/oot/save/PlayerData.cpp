#include "game/oot/save/PlayerData.h"

#include "game/oot/save/SaveOffsets.h"

namespace tpt::game::oot::save {

namespace {

// "ZELDAZ" — the SavePlayerData.newf magic that's set when a save file
// is loaded. Compared byte-for-byte (no encoding subtleties — these are
// plain ASCII letters).
constexpr std::uint8_t kNewfMagic[6] = {'Z','E','L','D','A','Z'};

// All reads are from the N64-native big-endian byte stream produced by
// Project64Source::readBytes(). MIPS layout: high byte at lowest offset.
inline std::uint16_t rdU16(std::span<const std::uint8_t> b, std::uint32_t o) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(b[o]) << 8) | b[o + 1]);
}
inline std::int16_t rdS16(std::span<const std::uint8_t> b, std::uint32_t o) {
    return static_cast<std::int16_t>(rdU16(b, o));
}
inline std::int32_t rdS32(std::span<const std::uint8_t> b, std::uint32_t o) {
    return static_cast<std::int32_t>(
        (static_cast<std::uint32_t>(b[o])     << 24) |
        (static_cast<std::uint32_t>(b[o + 1]) << 16) |
        (static_cast<std::uint32_t>(b[o + 2]) << 8)  |
         static_cast<std::uint32_t>(b[o + 3]));
}
// Map a single OoT NTSC filename byte to ASCII. Encoding documented in
// oot-main/include/message.h (FILENAME_* macros):
//   0x00-0x09 = digits '0'-'9'
//   0x0A-0xAA = hiragana / katakana (no ASCII equivalent — render as '?')
//   0xAB-0xC4 = uppercase A-Z
//   0xC5-0xDE = lowercase a-z
//   0xDF      = space / padding
//   0xE1-0xEB = punctuation
// PAL uses a smaller table (uppercase at 0x0A); add a second branch when
// the first PAL user reports.
char decodeNtscFilenameByte(std::uint8_t c) {
    if (c <= 0x09)                  return static_cast<char>('0' + c);
    if (c >= 0xAB && c <= 0xC4)     return static_cast<char>('A' + (c - 0xAB));
    if (c >= 0xC5 && c <= 0xDE)     return static_cast<char>('a' + (c - 0xC5));
    if (c == 0xDF)                  return ' ';
    switch (c) {
        case 0xE1: return '?';
        case 0xE2: return '!';
        case 0xE3: return ':';
        case 0xE4: return '-';
        case 0xE5: return '(';
        case 0xE6: return ')';
        case 0xE9: return ',';
        case 0xEA: return '.';
        case 0xEB: return '/';
        default:   return '?';
    }
}

inline std::string rdName(std::span<const std::uint8_t> b,
                          std::uint32_t o, std::size_t cap) {
    std::string out;
    out.reserve(cap);
    for (std::size_t i = 0; i < cap; ++i) {
        const auto c = b[o + i];
        if (c == 0x00 || c == 0xDF) break;  // null or padding-space ends the name
        out.push_back(decodeNtscFilenameByte(c));
    }
    return out;
}

}  // namespace

PlayerData readPlayerData(std::span<const std::uint8_t> sc) {
    PlayerData pd;
    if (sc.size() < kOffInventoryGsTokens + 2) return pd;

    for (std::size_t i = 0; i < sizeof(kNewfMagic); ++i) {
        if (sc[kOffNewf + i] != kNewfMagic[i]) return pd;
    }
    pd.saveLoaded = true;

    pd.deaths                    = rdU16(sc, kOffDeaths);
    pd.playerName                = rdName(sc, kOffPlayerName, 8);
    pd.healthCapacity            = rdS16(sc, kOffHealthCapacity);
    pd.health                    = rdS16(sc, kOffHealth);
    pd.magicLevel                = static_cast<std::int8_t>(sc[kOffMagicLevel]);
    pd.magic                     = static_cast<std::int8_t>(sc[kOffMagic]);
    pd.rupees                    = rdS16(sc, kOffRupees);
    pd.isMagicAcquired           = sc[kOffIsMagicAcquired] != 0;
    pd.isDoubleMagicAcquired     = sc[kOffIsDoubleMagicAcquired] != 0;
    pd.isDoubleDefenseAcquired   = sc[kOffIsDoubleDefenseAcquired] != 0;
    pd.hasBiggoronSword          = sc[kOffBgsFlag] != 0;
    pd.isAdult                   = rdS32(sc, kOffLinkAge) == 0;
    if (sc.size() >= kOffHsFishing + 4) {
        pd.hsFishing = static_cast<std::uint32_t>(rdS32(sc, kOffHsFishing));
    }
    if (sc.size() > kOffScarecrowSpawnSongSet) {
        pd.scarecrowSpawnSongSet = sc[kOffScarecrowSpawnSongSet] != 0;
    }
    return pd;
}

}  // namespace tpt::game::oot::save
