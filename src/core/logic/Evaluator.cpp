#include "core/logic/Evaluator.h"

#include "core/logic/Parser.h"

namespace tpt::core::logic {

bool evaluate(const Node& node, const Context& ctx) {
    using K = Node::Kind;
    switch (node.kind) {
        case K::True:  return true;
        case K::False: return false;
        case K::And:
            return evaluate(*node.left, ctx) && evaluate(*node.right, ctx);
        case K::Or:
            return evaluate(*node.left, ctx) || evaluate(*node.right, ctx);
        case K::Count: {
            const auto it = ctx.items.find(node.name);
            const int have = (it == ctx.items.end()) ? 0 : it->second;
            return have >= node.intValue;
        }
        case K::Setting: {
            const auto it = ctx.settings.find(node.name);
            if (it == ctx.settings.end()) return ctx.permissiveSettings;
            const bool isEqual = (it->second == node.value);
            return node.sense ? isEqual : !isEqual;
        }
        case K::Room:
            return ctx.reachedRooms.find(node.name) != ctx.reachedRooms.end();
        case K::Ident: {
            const auto pIt = ctx.predicates.find(node.name);
            if (pIt != ctx.predicates.end()) {
                try { return pIt->second(ctx); }
                catch (...) { return false; }
            }
            // Not a predicate -> treat as item-has check.
            const auto it = ctx.items.find(node.name);
            return it != ctx.items.end() && it->second >= 1;
        }
    }
    return false;
}

bool evalExpr(std::string_view expr, const Context& ctx) {
    return evaluate(*parse(expr), ctx);
}

}  // namespace tpt::core::logic
