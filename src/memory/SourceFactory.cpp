#include "memory/SourceFactory.h"

#include "dolphin/DolphinClient.h"
#include "dusk/DuskSource.h"

namespace tpt::memory {

std::unique_ptr<MemorySource> makeMemorySource(SourceKind kind) {
    switch (kind) {
        case SourceKind::Dolphin:
            return std::make_unique<tpt::dolphin::Client>();
        case SourceKind::Dusk:
            return std::make_unique<tpt::dusk::DuskSource>();
        case SourceKind::Auto:
        default:
            if (tpt::dusk::DuskSource::isAvailable()) {
                return std::make_unique<tpt::dusk::DuskSource>();
            }
            return std::make_unique<tpt::dolphin::Client>();
    }
}

}  // namespace tpt::memory
