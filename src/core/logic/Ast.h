#pragma once

#include <memory>
#include <string>
#include <vector>

namespace tpt::core::logic {

// AST node shared between the TP and OoT logic parsers. Each game has
// its own parser frontend that emits these nodes; the evaluator
// dispatches on `kind` so adding game-specific kinds is contained to
// adding new switch cases.
//
// Memory layout note: the `children` field is only populated for Call
// (variadic args). Every other Kind uses `left`/`right`. Adding the
// vector costs ~24 bytes per node; trivial against parser-cache size.
struct Node {
    enum class Kind {
        // Shared across games.
        True,
        False,
        And,         // left, right
        Or,          // left, right
        Not,         // left = operand
        Ident,       // name = identifier (item or predicate)
        Count,       // name = item, intValue = required count
        StringLit,   // value = literal string (e.g. 'open' / 'Defeat Queen Gohma')
        Compare,     // left, right; sense (true = '==', false = '!=')

        // TP-only: parser/evaluator handle these; OoT doesn't emit them.
        Setting,     // name = setting_name, value = expected, sense (true=equals)
        Room,        // name = room name (with underscores already turned into spaces)

        // OoT-only: function call. name = callee, children = args.
        // Includes special metarules `at(region, rule)` and `here(rule)`
        // which evaluator dispatches on the callee name.
        Call,
    };

    Kind kind;
    std::string name;
    std::string value;
    int  intValue = 0;
    bool sense = true;
    std::shared_ptr<const Node> left;
    std::shared_ptr<const Node> right;
    // Used only by Kind::Call. Empty for every other kind.
    std::vector<std::shared_ptr<const Node>> children;
};

using NodePtr = std::shared_ptr<const Node>;

inline NodePtr makeTrue()  { return std::make_shared<Node>(Node{Node::Kind::True,  {}, {}, 0, true,  nullptr, nullptr, {}}); }
inline NodePtr makeFalse() { return std::make_shared<Node>(Node{Node::Kind::False, {}, {}, 0, true,  nullptr, nullptr, {}}); }

}  // namespace tpt::core::logic
