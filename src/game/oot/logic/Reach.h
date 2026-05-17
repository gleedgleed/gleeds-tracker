#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "core/logic/Ast.h"
#include "game/oot/logic/AliasTable.h"
#include "game/oot/logic/Context.h"
#include "game/oot/logic/WorldGraph.h"

namespace tpt::game::oot::logic {

// A region with every edge's rule pre-parsed. Built once per world
// load; reused for every poll. Storing parsed ASTs (vs. re-parsing on
// every BFS iteration) keeps reach cheap enough to run at GUI cadence.
struct CompiledEdge {
    std::string                target;
    tpt::core::logic::NodePtr  rule;     // null only if the rule string was empty
};

struct CompiledRegion {
    std::string name;
    std::string dungeon;
    bool        timePasses = false;
    std::vector<CompiledEdge> exits;
    std::vector<CompiledEdge> locations;
    std::vector<CompiledEdge> events;
};

using CompiledRegionMap = std::unordered_map<std::string, CompiledRegion>;

// Pre-parse every rule string in the raw region map. Throws if any
// rule string fails to parse — should be impossible after --oot-parse
// has been used to validate the corpus.
CompiledRegionMap compileWorld(const RegionMap& src);

// BFS over the compiled world, mutating ctx in-place. The algorithm:
//
//   1. Seed reachedRegions with `startRegion` ("Root").
//   2. Iterate: for every reached region, try each exit's rule with the
//      current context; if it passes, mark the target reached. Try each
//      event's rule; if it passes, add the event to ctx.events. Repeat
//      until a full iteration produces no changes.
//   3. Final pass: for every reached region's locations, evaluate the
//      access rule; gather names whose rule passes into the result.
//
// Returns the set of locations the player can currently reach in logic.
// The context's reachedRegions / events are also left populated for
// follow-up queries.
std::unordered_set<std::string>
reach(const CompiledRegionMap& world,
      const AliasTable& aliases,
      Context& ctx,
      const std::string& startRegion = "Root");

}  // namespace tpt::game::oot::logic
