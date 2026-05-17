#pragma once

#include <string>
#include <string_view>

namespace tpt::game::oot::logic {

// Convert OoTR's JSON-with-Python-comments-and-multiline-strings into
// strict JSON consumable by nlohmann::json.
//
// What's accepted on input:
//   - Python-style hash comments: `# anything to end of line`
//   - C-style line + block comments: `// ...` and `/* ... */`
//   - Trailing commas before } or ]
//   - Literal newlines and surrounding indentation inside `"..."` string
//     literals — OoTR uses these to wrap long rule expressions readably.
//     We replace the newline-plus-whitespace run with a single space, so
//     the rule string the C++ side sees is one long line.
//
// Reuses the regular JSONC stripper (src/core/logic/Jsonc.h) for the
// pieces that overlap (trailing commas). The hash and multi-line bits
// live here because they're OoTR-specific and shouldn't pollute the
// shared Jsonc helper used by TP.
std::string prepOotJson(std::string_view src);

}  // namespace tpt::game::oot::logic
