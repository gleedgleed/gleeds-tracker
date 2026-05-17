#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>

namespace tpt::memory { class MemorySource; }

namespace tpt::core {

struct SeedSettings;

// Identifies a check's slot in the rando's arcCheckInfo array. Matches the
// (offset, stage, room, dir, type) fields the seed writes for each ARC
// replacement; see rando::ARCReplacement in
// Randomizer-master/GameCube/include/rando/data.h.
struct ArcFingerprint {
    std::uint32_t offset = 0;
    std::uint8_t  stage  = 0;
    std::uint8_t  room   = 0;
    std::uint8_t  dir    = 0;
    std::uint8_t  type   = 0;

    bool operator==(const ArcFingerprint& o) const noexcept {
        return offset == o.offset && stage == o.stage && room == o.room
            && dir == o.dir && type == o.type;
    }
};

struct ArcFingerprintHash {
    std::size_t operator()(const ArcFingerprint& fp) const noexcept {
        // Pack into a single u64 — all five fields fit easily.
        std::uint64_t k = std::uint64_t(fp.offset) << 32;
        k |= std::uint64_t(fp.stage) << 24;
        k |= std::uint64_t(fp.room)  << 16;
        k |= std::uint64_t(fp.dir)   <<  8;
        k |= std::uint64_t(fp.type);
        return std::hash<std::uint64_t>{}(k);
    }
};

// Static index loaded once at startup from data/check_placements.json.
class CheckPlacementsIndex {
public:
    static CheckPlacementsIndex load(const std::filesystem::path& jsonPath);

    // Returns the check name for an ARC fingerprint, or empty string if unknown.
    std::string_view lookup(const ArcFingerprint& fp) const;

    bool   empty() const noexcept { return map_.empty(); }
    std::size_t size()  const noexcept { return map_.size();  }

private:
    std::unordered_map<ArcFingerprint, std::string, ArcFingerprintHash> map_;
};

// Per-seed result: check name -> GC item ID that the current seed places there.
using SeedPlacements = std::unordered_map<std::string, std::uint8_t>;

// Read the seed's arcCheckInfo array from main RAM and resolve each entry to
// its check name via `index`. Returns an empty map if the seed magic isn't
// available, the header is malformed, or the index is empty. The seed's
// `foundAt` field locates the header; we re-read its bytes to access fields
// the existing SeedSettings doesn't already expose.
SeedPlacements readSeedPlacements(const tpt::memory::MemorySource& mem,
                                  const SeedSettings& seed,
                                  const CheckPlacementsIndex& index);

// Returns true iff the seed places a progression item at the named check.
// Pure data lookup against the placement map + item-id curated list.
bool isCheckProgressionInSeed(std::string_view checkName,
                              const SeedPlacements& placements);

}  // namespace tpt::core
