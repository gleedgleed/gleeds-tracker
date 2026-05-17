#pragma once

#include <string>
#include <string_view>

namespace tpt::core::logic {

// Strip // line comments, /* block */ comments, and trailing commas from
// JSONC. String literals are skipped, so /* inside "..." is preserved.
// The output is valid JSON consumable by nlohmann::json.
std::string stripJsonc(std::string_view src);

}  // namespace tpt::core::logic
