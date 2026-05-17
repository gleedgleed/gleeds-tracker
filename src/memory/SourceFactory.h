#pragma once

#include "memory/MemorySource.h"

#include <memory>

namespace tpt::memory {

enum class SourceKind {
    Auto,     // detect: prefer Dusk if dusklight.exe is running, else Dolphin
    Dolphin,  // force a dolphin::Client (default emulator path)
    Dusk,     // force a dusk::DuskSource (PC port)
};

// Build a memory source.
//
// With SourceKind::Auto, the factory checks which emulator/port process is
// running and instantiates the matching source. If multiple are running,
// Dusk wins (it's the more specific signal — most users have only one of
// the two). If neither is running, returns a Dolphin client (the most
// common case) so the GUI can show "waiting for Dolphin..." rather than
// "no source available".
//
// The returned source is **not yet connected** — call connect() on it.
// Re-call makeMemorySource() periodically when isConnected() goes false
// to pick up emulator/port changes (e.g. user closes Dolphin and opens
// Dusk).
std::unique_ptr<MemorySource> makeMemorySource(SourceKind kind = SourceKind::Auto);

}  // namespace tpt::memory
