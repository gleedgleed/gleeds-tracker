#pragma once

#include <filesystem>
#include <ostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/logic/Ast.h"

namespace tpt::game::oot::logic {

// A LogicHelpers macro. `params` is empty for plain aliases like
// `"Hookshot": "Progressive_Hookshot"`. For parameterized macros like
// `"can_play(song)": "..."`, params holds the formal arg names
// (`["song"]`) in declaration order. `body` is the parsed AST of the
// macro's right-hand side.
//
// At evaluation time, plain aliases work like Ident substitution: the
// caller's `Ident("Hookshot")` becomes the parsed body. Parameterized
// aliases work like function-style substitution: the caller's
// `Call("can_play", [Prelude_of_Light])` evaluates the body with the
// formal arg bound to the caller's actual argument.
struct Alias {
    std::vector<std::string>   params;
    tpt::core::logic::NodePtr  body;
};

using AliasTable = std::unordered_map<std::string, Alias>;

class AliasLoadError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Load and parse OoTR's LogicHelpers.json. Each entry's key may be a
// bare identifier ("Bombs") or a parameterized form ("can_play(song)").
// Each value is parsed by the OoT rule parser; parse failures abort
// loading with diagnostics. Returns the table on success.
AliasTable loadAliases(const std::filesystem::path& helpersJson,
                       std::ostream& errlog);

}  // namespace tpt::game::oot::logic
