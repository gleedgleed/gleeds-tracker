#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "core/logic/Context.h"
#include "core/logic/WorldData.h"

namespace tpt::core::logic {

// Default starting room when no warps are unlocked. From here the entire
// Ordon Province + Faron Woods graph is naturally reachable.
inline constexpr const char* kDefaultStartRoom = "Outside Links House";

// BFS room reachability to fixpoint. Mutates ctx.reachedRooms in-place so
// `Room.X` references inside requirement expressions resolve correctly.
// Returns the same set for caller convenience.
const std::unordered_set<std::string>&
reach(const RoomMap& rooms, Context& ctx,
      const std::vector<std::string>& startRooms,
      const std::vector<std::string>& warpRooms);

// Names of reachable, in-logic, not-yet-completed checks. Order is stable
// (insertion order over the BFS traversal).
std::vector<std::string>
pendingInReach(const RoomMap& rooms, const CheckMap& checks,
               const std::unordered_set<std::string>& reached,
               const std::unordered_set<std::string>& completed,
               const Context& ctx);

}  // namespace tpt::core::logic
