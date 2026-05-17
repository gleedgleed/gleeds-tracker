#pragma once

#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "game/TrackerSnapshot.h"

namespace tpt::memory { class MemorySource; }
namespace tpt::ui     { struct FilterSpec; }

namespace tpt::game {

// Abstraction over a tracked game (Twilight Princess, Ocarina of Time, …).
// The UI shell owns one instance of a concrete GameModule, polls it at
// the GUI cadence, and delegates game-specific rendering (right pane,
// options row, settings pane) back to the module.
//
// Anything game-typed — inventory shape, save layout, predicates, rando
// settings format — lives behind this interface. The shell stays
// game-agnostic and consumes only the snapshot() + the module's
// rendering callbacks.
class GameModule {
public:
    virtual ~GameModule() = default;

    // Identity — short id is the persistence key; displayName is the
    // human-readable label for the status bar / window title.
    virtual std::string id()          const = 0;
    virtual std::string displayName() const = 0;

    // ---- Lifecycle --------------------------------------------------------

    // Load world data (room/check JSONCs, save-bit bindings, etc.).
    // Returns false on irrecoverable failure; details written to errlog.
    virtual bool loadWorldData(std::ostream& errlog) = 0;

    // Re-poll the source and rebuild the snapshot. Called at the GUI's
    // poll cadence (~500ms). Cheap enough to call every tick.
    virtual void poll(tpt::memory::MemorySource& mem) = 0;

    // ---- Data exposure for the UI shell -----------------------------------

    virtual const TrackerSnapshot& snapshot() const = 0;

    // Tab definitions for the filter strip. Returned by const reference;
    // the module owns the storage.
    virtual const std::vector<tpt::ui::FilterSpec>& filterSpecs() const = 0;

    // ---- Module-owned ImGui panes -----------------------------------------
    //
    // Each of these is called from the shell with the active ImGui context
    // implicit. The module is free to use any widgets it wants; the shell
    // just supplies the slot.

    // The options row (immediately under the status bar): settings string
    // input, game-specific toggles like "progression rupees only".
    virtual void renderOptionsRow() = 0;

    // The right-most column: inventory + quest-state summary. Game-typed,
    // so the module renders it directly.
    virtual void renderRightPane() = 0;

    // Optional detailed rando-settings pane. No-op for games without
    // settings strings, or until the rando ships.
    virtual void renderSettingsPane() = 0;

    // ---- Persisted user preferences ---------------------------------------
    //
    // Each game module owns its own slice of preferences.json. The shell
    // hands the game its sub-object on startup and persists whatever the
    // game returns on save. Inactive games' blobs are preserved untouched.

    virtual void           loadPrefs(const nlohmann::json& sub) = 0;
    virtual nlohmann::json savePrefs() const                    = 0;

    // ---- Source selection -------------------------------------------------

    // Build the memory source this game prefers (auto-detect among the
    // emulators / PC ports the game's playable on).
    virtual std::unique_ptr<tpt::memory::MemorySource> defaultSource() const = 0;
};

}  // namespace tpt::game
