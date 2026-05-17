#pragma once

#include "game/oot/logic/Context.h"
#include "game/oot/save/Inventory.h"
#include "game/oot/save/PlayerData.h"
#include "game/oot/save/SaveFlags.h"

namespace tpt::game::oot::logic {

// Translate the decoded save state into the evaluator's view. Fills
// `items` (name → count), `settings` (string defaults until OoTR
// settings-string parsing lands), and `isAdult`. Does NOT touch
// `events` or `reachedRegions` — those are populated by the reach BFS.
Context buildContext(const save::PlayerData& pd,
                     const save::Inventory&  inv,
                     const save::SaveFlags&  flags);

}  // namespace tpt::game::oot::logic
