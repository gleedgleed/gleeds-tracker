#include "core/logic/Jsonc.h"

#include <cctype>

namespace tpt::core::logic {

namespace {

bool isSpace(char c) { return std::isspace(static_cast<unsigned char>(c)); }

// First pass: strip // and /* */ comments while preserving string literals.
std::string stripComments(std::string_view src) {
    std::string out;
    out.reserve(src.size());
    for (std::size_t i = 0; i < src.size();) {
        const char c = src[i];
        if (c == '"') {
            out.push_back(c);
            ++i;
            while (i < src.size()) {
                const char s = src[i];
                out.push_back(s);
                ++i;
                if (s == '\\' && i < src.size()) {
                    out.push_back(src[i]);
                    ++i;
                } else if (s == '"') {
                    break;
                }
            }
        } else if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
            i += 2;
            while (i < src.size() && src[i] != '\n') ++i;
        } else if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
            i += 2;
            while (i + 1 < src.size() && !(src[i] == '*' && src[i + 1] == '/')) ++i;
            if (i + 1 < src.size()) i += 2;
        } else {
            out.push_back(c);
            ++i;
        }
    }
    return out;
}

// Second pass: drop trailing commas before } or ].
std::string stripTrailingCommas(std::string_view src) {
    std::string out;
    out.reserve(src.size());
    for (std::size_t i = 0; i < src.size(); ++i) {
        const char c = src[i];
        if (c == ',') {
            std::size_t j = i + 1;
            while (j < src.size() && isSpace(src[j])) ++j;
            if (j < src.size() && (src[j] == '}' || src[j] == ']')) continue;
        }
        out.push_back(c);
    }
    return out;
}

}  // namespace

std::string stripJsonc(std::string_view src) {
    return stripTrailingCommas(stripComments(src));
}

}  // namespace tpt::core::logic
