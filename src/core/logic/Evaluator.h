#pragma once

#include <string_view>

#include "core/logic/Ast.h"
#include "core/logic/Context.h"

namespace tpt::core::logic {

// Evaluate a parsed AST against a context.
bool evaluate(const Node& node, const Context& ctx);

// Convenience: parse + evaluate. Caches the parse internally.
bool evalExpr(std::string_view expr, const Context& ctx);

}  // namespace tpt::core::logic
