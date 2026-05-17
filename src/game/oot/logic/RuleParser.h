#pragma once

#include <stdexcept>
#include <string_view>

#include "core/logic/Ast.h"

namespace tpt::game::oot::logic {

class RuleParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Parse one OoTR rule expression into the shared AST defined in
// src/core/logic/Ast.h. Accepts the Python-flavored subset OoTR uses:
//
//   expr        := or
//   or          := and ('or' and)*
//   and         := not_expr ('and' not_expr)*
//   not_expr    := 'not' not_expr | cmp
//   cmp         := atom (('==' | '!=') atom)?
//   atom        := True | False | None
//                | Number
//                | StringLit             ('...' or "...")
//                | Ident
//                | Ident '(' args ')'    function call (incl. at/here)
//                | '(' expr ')'          parenthesized
//                | '(' Ident ',' Number ')'  counted-item shorthand
//   args        := empty | expr (',' expr)*
//
// Throws RuleParseError with the offset of the offending byte and a
// short message on malformed input. Otherwise returns a shared_ptr to
// the constructed AST.
tpt::core::logic::NodePtr parseRule(std::string_view src);

}  // namespace tpt::game::oot::logic
