#pragma once

#include "core/logic/Context.h"

namespace tpt::core::logic {

// Populate ctx.predicates with the ~80 predicate functions ported from
// LogicFunctions.cs. Predicates that call other predicates do so via
// ctx.predicates at evaluation time, so registration order doesn't matter.
void registerPredicates(Context& ctx);

}  // namespace tpt::core::logic
