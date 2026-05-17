#pragma once

#include <string>

#include "core/logic/Ast.h"
#include "game/oot/logic/AliasTable.h"
#include "game/oot/logic/Context.h"

namespace tpt::game::oot::logic {

// Evaluate an OoT rule AST against the given Context + AliasTable.
//
// Resolution order for an Ident `X`:
//   1. Current parameterized-macro scope (formal arg bound to actual arg AST)
//   2. AliasTable (`Hookshot` → parsed body of `Progressive_Hookshot`)
//   3. Context.items   (`Bombs` → 1 if owned)
//   4. Context.settings (`open_kakariko` truthiness)
//   5. Context.events  (event string match)
//   6. Falls through to false — unknown identifier means "not satisfied".
//
// For Call `f(args)`:
//   1. Special metarules: at(region, rule), here(rule), can_use(item),
//      has_*(...) (medallions/stones/hearts), __in__, __index__
//   2. AliasTable with matching parameter count → bind args, eval body
//   3. Otherwise false.
//
// Re-entrant. Each call constructs a small scope stack on the stack;
// no mutable state on the evaluator itself.
class Evaluator {
public:
    Evaluator(const Context& ctx, const AliasTable& aliases);

    bool evaluate(const tpt::core::logic::Node& node) const;

private:
    const Context&    ctx_;
    const AliasTable& aliases_;
};

}  // namespace tpt::game::oot::logic
