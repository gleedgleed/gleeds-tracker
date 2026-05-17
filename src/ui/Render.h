#pragma once

#include "ui/Style.h"
#include "ui/UIState.h"

namespace tpt::game { class GameModule; }

namespace tpt::ui::render {

// Top bar with hook/region/seed/counts/mode/error info. Single row.
void statusBar(const State& s);

// Full-viewport 3-column layout. The GameModule supplies the game-specific
// options row + right-pane content; everything else is game-agnostic.
void mainLayout(State& s, tpt::game::GameModule& game,
                style::Settings& styleSettings, bool& fontReloadNeeded);

// Individual panes (called by mainLayout). Exposed so they can be docked
// independently if we ever switch to docking. The columns that render
// game-defined filter tabs take the GameModule for filterSpecs().
void allChecksColumn(State& s, tpt::game::GameModule& game);
void filtersColumn(State& s, tpt::game::GameModule& game);
void rightColumn(const State& s, tpt::game::GameModule& game);

}  // namespace tpt::ui::render
