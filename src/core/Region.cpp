#include "core/Region.h"

#include <cstring>

#include "memory/MemorySource.h"

namespace tpt::core {

std::optional<Region> detectRegion(const tpt::memory::MemorySource& mem) {
    // Ask the source for its game ID instead of reading the GC-virtual
    // address 0x80000000 directly. Dolphin's implementation still reads
    // that address internally; Dusk synthesizes the ID from its known
    // build target. This keeps detectRegion source-agnostic — it never
    // pokes an address only Dolphin can serve.
    const std::string id = mem.gameId();
    if (id.size() < 4) return std::nullopt;
    for (const auto& r : kRegions) {
        if (std::memcmp(id.data(), r.id4.data(), 4) == 0) return r;
    }
    return std::nullopt;
}

}  // namespace tpt::core
