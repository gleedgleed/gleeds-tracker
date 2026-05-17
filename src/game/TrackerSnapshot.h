#pragma once

#include <map>
#include <string>
#include <vector>

#include "ui/UIState.h"  // CheckEntry, CheckStatus, FilterSpec

namespace tpt::game {

// Game-agnostic data shape consumed by the UI shell. Each GameModule
// populates one of these in poll(); the UI shell renders it without
// knowing the underlying game.
//
// Anything game-typed (Inventory, QuestState, SeedSettings, …) stays
// inside the GameModule implementation. The snapshot exposes only the
// already-aggregated check views + counters + small status strings the
// shell needs to draw its three columns and status bar.
struct TrackerSnapshot {
    bool worldLoaded = false;
    bool saveLoaded  = false;

    // Game-supplied right-side text for the status bar (region, seed
    // name, etc.). Module-formatted; shell prints it verbatim.
    std::string statusText;
    std::string errorText;

    // Check views — same shapes the existing UI already consumes.
    std::map<std::string, std::vector<tpt::ui::CheckEntry>> allByStage;
    std::map<std::string, std::vector<tpt::ui::CheckEntry>> reachableByStage;
    std::map<std::string,
             std::map<std::string, std::vector<tpt::ui::CheckEntry>>> flagAllByStage;
    std::map<std::string,
             std::map<std::string, std::vector<tpt::ui::CheckEntry>>> flagReachableByStage;

    int totalCompleted        = 0;
    int totalResolvable       = 0;
    int totalReachablePending = 0;
};

}  // namespace tpt::game
