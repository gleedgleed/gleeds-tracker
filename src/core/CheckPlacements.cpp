#include "core/CheckPlacements.h"

#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/Items.h"
#include "core/SeedHeader.h"
#include "memory/MemorySource.h"

namespace tpt::core {

namespace {

inline std::uint16_t rd16BE(std::span<const std::uint8_t> b, std::uint32_t o) {
    return static_cast<std::uint16_t>((b[o] << 8) | b[o + 1]);
}
inline std::uint32_t rd32BE(std::span<const std::uint8_t> b, std::uint32_t o) {
    return (std::uint32_t(b[o]) << 24) | (std::uint32_t(b[o + 1]) << 16) |
           (std::uint32_t(b[o + 2]) << 8) | std::uint32_t(b[o + 3]);
}

// Seed-header offsets (per Randomizer-Web-Generator-main/.../SeedData.cs:3915-3969).
// All UInt16 BE.
constexpr std::uint32_t kHdrOff_ArcCheckInfoNumEntries  = 0x50;
constexpr std::uint32_t kHdrOff_ArcCheckInfoDataOffset  = 0x52;

constexpr std::size_t  kArcEntrySize = 12;

// ARC entries with replacementType == 3 (Instruction) carry a raw instruction
// word in replacementValue, not an item ID — skip those.
constexpr std::uint8_t kReplacementType_Instruction = 3;

}  // namespace

CheckPlacementsIndex CheckPlacementsIndex::load(const std::filesystem::path& jsonPath) {
    CheckPlacementsIndex out;

    std::ifstream in(jsonPath, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "warning: check_placements.json not found at %s\n",
                     jsonPath.string().c_str());
        return out;
    }
    std::ostringstream ss;
    ss << in.rdbuf();

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(ss.str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "warning: check_placements.json parse error: %s\n", e.what());
        return out;
    }
    if (!j.is_object()) return out;

    for (auto it = j.begin(); it != j.end(); ++it) {
        const auto& v = it.value();
        if (!v.is_object()) continue;
        auto arcIt = v.find("arc");
        if (arcIt == v.end() || !arcIt->is_array()) continue;

        for (const auto& slot : *arcIt) {
            if (!slot.is_object()) continue;
            ArcFingerprint fp{};
            fp.offset = slot.value("offset", 0u);
            fp.stage  = static_cast<std::uint8_t>(slot.value("stage", 0));
            fp.room   = static_cast<std::uint8_t>(slot.value("room",  0));
            fp.dir    = static_cast<std::uint8_t>(slot.value("dir",   0));
            fp.type   = static_cast<std::uint8_t>(slot.value("type",  0));
            out.map_.emplace(fp, it.key());
        }
    }
    return out;
}

std::string_view CheckPlacementsIndex::lookup(const ArcFingerprint& fp) const {
    const auto it = map_.find(fp);
    if (it == map_.end()) return {};
    return it->second;
}

namespace {

// Parse a 12-byte ARC entry at offset `base` within buf. Returns nullopt on
// out-of-range or instruction-type entries.
struct ArcEntry {
    ArcFingerprint fp;
    std::uint8_t   itemId;
};
std::optional<ArcEntry> parseArcEntry(std::span<const std::uint8_t> buf,
                                      std::size_t base) {
    if (base + kArcEntrySize > buf.size()) return std::nullopt;
    const std::uint32_t offset           = rd32BE(buf, static_cast<std::uint32_t>(base + 0));
    const std::uint32_t replacementValue = rd32BE(buf, static_cast<std::uint32_t>(base + 4));
    const std::uint8_t  dir   = buf[base + 8];
    const std::uint8_t  type  = buf[base + 9];
    const std::uint8_t  stage = buf[base + 10];
    const std::uint8_t  room  = buf[base + 11];
    if (type == kReplacementType_Instruction) return std::nullopt;

    ArcEntry e;
    e.fp = ArcFingerprint{offset, stage, room, dir, type};
    e.itemId = static_cast<std::uint8_t>(replacementValue & 0xFFu);
    return e;
}

// Header layout drifts between rando versions, so we can't always trust
// `dataStart + arcCheckInfoDataOffset` to land on the array start. Instead we
// scan a generous window of main RAM for the first 12-byte chunk that matches
// a known fingerprint, then require ≥3 of the next 12-byte-strided chunks to
// also match — that confirms we hit the real array and not a coincidental
// match in static-replacement data.
std::optional<std::uint32_t> findArcArrayStart(
    std::span<const std::uint8_t> buf,
    std::uint32_t bufBase,
    const CheckPlacementsIndex& index)
{
    if (buf.size() < kArcEntrySize) return std::nullopt;

    constexpr std::size_t kConfirmCount = 3;     // entries forward to confirm
    constexpr std::size_t kConfirmStride = kArcEntrySize;

    for (std::size_t off = 0; off + kArcEntrySize <= buf.size(); ++off) {
        const auto e = parseArcEntry(buf, off);
        if (!e) continue;
        if (index.lookup(e->fp).empty()) continue;

        // Confirm: at least kConfirmCount of the next entries also resolve.
        std::size_t hits = 0;
        for (std::size_t k = 1; k <= kConfirmCount * 4 && hits < kConfirmCount; ++k) {
            const auto e2 = parseArcEntry(buf, off + k * kConfirmStride);
            if (!e2) continue;
            if (!index.lookup(e2->fp).empty()) ++hits;
        }
        if (hits >= kConfirmCount) return bufBase + static_cast<std::uint32_t>(off);
    }
    return std::nullopt;
}

}  // namespace

SeedPlacements readSeedPlacements(const tpt::memory::MemorySource& mem,
                                  const SeedSettings& seed,
                                  const CheckPlacementsIndex& index) {
    SeedPlacements out;
    if (index.empty() || seed.foundAt == 0) return out;

    std::vector<std::uint8_t> headerBytes(kSeedHeaderSize);
    if (!mem.readBytes(seed.foundAt, headerBytes.data(), headerBytes.size())) return out;
    std::span<const std::uint8_t> hb(headerBytes);

    const std::uint16_t numEntries = rd16BE(hb, kHdrOff_ArcCheckInfoNumEntries);
    if (numEntries == 0) return out;

    const std::uint16_t headerSize = rd16BE(hb, 0x28);
    const std::uint16_t dataOffset = rd16BE(hb, kHdrOff_ArcCheckInfoDataOffset);
    const std::uint32_t headerEnd  = seed.foundAt +
        (headerSize ? headerSize : kSeedHeaderSizeWebgen);

    // Scan a generous window around the header's expected location. Section
    // sizes drift between rando versions (e.g. 1.3 → 1.4 reorganised the
    // earlier sections), but the array itself always sits somewhere within a
    // few KB of `headerEnd + dataOffset`. 32 KB on either side is plenty.
    constexpr std::uint32_t kScanSlop = 0x8000;
    const std::uint32_t expected      = headerEnd + dataOffset;
    const std::uint32_t scanStart     = (expected > kScanSlop) ? (expected - kScanSlop) : 0;
    const std::uint32_t scanLen       = kScanSlop * 2;

    std::vector<std::uint8_t> scanBuf(scanLen);
    if (!mem.readBytes(scanStart, scanBuf.data(), scanLen)) return out;

    const auto arrayStart = findArcArrayStart(
        std::span<const std::uint8_t>(scanBuf), scanStart, index);
    if (!arrayStart) return out;

    // Now we know where the array starts. Read numEntries * 12 bytes from there.
    const std::size_t totalBytes = static_cast<std::size_t>(numEntries) * kArcEntrySize;
    std::vector<std::uint8_t> buf(totalBytes);
    if (!mem.readBytes(*arrayStart, buf.data(), totalBytes)) return out;

    std::span<const std::uint8_t> b(buf);
    for (std::size_t i = 0; i < numEntries; ++i) {
        const auto e = parseArcEntry(b, i * kArcEntrySize);
        if (!e) continue;
        const auto name = index.lookup(e->fp);
        if (name.empty()) continue;
        out.emplace(std::string(name), e->itemId);
    }
    return out;
}

bool isCheckProgressionInSeed(std::string_view checkName,
                              const SeedPlacements& placements) {
    const auto it = placements.find(std::string(checkName));
    if (it == placements.end()) return false;
    return isProgressionItemId(it->second);
}

}  // namespace tpt::core
