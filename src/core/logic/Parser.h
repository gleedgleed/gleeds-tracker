#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

#include "core/logic/Ast.h"

namespace tpt::core::logic {

class ParseError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// Parse a TPR DSL expression. Cached by expression string — repeated calls
// with the same expression return the same shared AST. Throws ParseError on
// malformed input.
NodePtr parse(std::string_view expr);

// Reset the parse cache (used by tests).
void clearParseCache();

// Number of unique expressions currently cached.
std::size_t parseCacheSize();

}  // namespace tpt::core::logic
