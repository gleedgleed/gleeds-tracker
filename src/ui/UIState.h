#pragma once

#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/CheckPlacements.h"
#include "core/EventFlags.h"
#include "core/Items.h"
#include "core/CheckSaveBindings.h"
#include "core/QuestState.h"
#include "core/Region.h"
#include "core/SeedHeader.h"
#include "core/logic/Context.h"
#include "core/logic/WorldData.h"
#include "memory/MemorySource.h"

namespace tpt::ui {

enum class CheckStatus { Done, Pending, Unknown };

inline const char* statusMarker(CheckStatus s) {
    switch (s) {
        case CheckStatus::Done:    return "[x]";
        case CheckStatus::Pending: return "[ ]";
        case CheckStatus::Unknown: return "[?]";
    }
    return "[?]";
}

struct CheckEntry {
    std::string  name;
    CheckStatus  status = CheckStatus::Unknown;
};

// Live tracker state. The "world" group is loaded once at startup; the
// "live" group is re-derived every poll tick from the save block.
struct State {
    // ---- World data (loaded once) ------------------------------------------
    tpt::core::logic::RoomMap        rooms;
    tpt::core::logic::CheckMap       checks;
    tpt::core::CheckSaveBindings     saveBindings;
    tpt::core::CheckPlacementsIndex  placementsIndex;
    bool worldLoaded = false;

    // Stable display order over the master list, keyed by stage name.
    std::map<std::string, std::vector<std::string>> masterByStage;

    // ---- Connection ---------------------------------------------------------
    bool emulatorHooked = false;
    std::string sourceName;          // "Dolphin" / "Dusk" — set by poll()
    std::optional<tpt::core::Region> region;
    std::string gameId;

    // ---- Snapshot (re-polled) ----------------------------------------------
    bool saveLoaded = false;
    std::uint8_t currentNode = 0xFF;
    std::optional<tpt::core::Inventory>   inv;
    std::optional<tpt::core::QuestState>  qs;
    std::unordered_map<std::string, bool> eventFlags;
    std::unordered_map<std::string, bool> getItemFlags;
    std::optional<tpt::core::SeedSettings> seed;
    // check name -> GC item ID placed in this seed. Re-derived when seed
    // changes; empty if no seed in memory.
    tpt::core::SeedPlacements placements;

    // ---- Derived from snapshot + world -------------------------------------
    std::unordered_set<std::string> completed;       // names from LOCATION_TABLE that are done
    std::unordered_set<std::string> reachedRooms;    // BFS reach output
    std::unordered_set<std::string> pendingSet;      // reachable + in-logic + not done

    // category -> [(name, status)] (insertion-sorted by name)
    std::map<std::string, std::vector<CheckEntry>> allByStage;
    // category -> [(name, Pending)] — reachable + in-logic + not done
    std::map<std::string, std::vector<CheckEntry>> reachableByStage;
    // Per-flag views. Both keyed by filter label (e.g. "Poes", "Bugs").
    //   flagAllByStage     — every check of that flag, with status markers (left col)
    //   flagReachableByStage — only checks of that flag that are also pending (mid col)
    std::map<std::string, std::map<std::string, std::vector<CheckEntry>>> flagAllByStage;
    std::map<std::string, std::map<std::string, std::vector<CheckEntry>>> flagReachableByStage;

    int totalCompleted = 0;
    int totalResolvable = 0;
    int totalReachablePending = 0;

    std::string error;

    // ---- UI selection (preserved across polls) -----------------------------
    std::string selectedCheck;
    bool glitched = false;
    bool autoHook = true;
    std::string settingsString;

    // True when the current seed has at least one rupee slot holding a
    // progression item. Detected once when the seed first loads.
    bool rupeeShuffleActive = false;
    // UI filter toggle: when true, the All-Checks pane hides rupee checks
    // whose placed item isn't progression. Only meaningful when
    // rupeeShuffleActive — the toggle is hidden otherwise.
    bool progressionRupeesOnly = false;
};

// Filter labels → match rules over web-gen Check::categories (and, as a
// fallback, Check::name). A check belongs to the tab if any of its categories
// appears in matchAnyCategory, OR its name is listed in matchAnyName.
// matchAnyName is for tabs the web-gen taxonomy doesn't natively express
// (e.g. mini-bosses — no equivalent webgen tag, so the 7 names are pinned
// directly). Treat this as a UI-vocabulary shim; a richer
// ui-category → webgen-category map can replace it later.
//
// The active filter table is owned by the GameModule and exposed via
// GameModule::filterSpecs(). The UI shell iterates whatever the module
// returns; this struct is the contract.
struct FilterSpec {
    std::string label;
    std::vector<std::string> matchAnyCategory;
    std::vector<std::string> matchAnyName;
    // When true, this tab matches checks whose category is "progression-capable"
    // in the current seed (computed dynamically, ignores matchAny*). See
    // rebuildFlagViews / computeProgressionCats.
    bool progression = false;
    // When true, the tab is only rendered in the middle (Reachable) column;
    // the left (All Checks) column skips it.
    bool reachableOnly = false;
};

}  // namespace tpt::ui
