#include "game/oot/logic/JsonPrep.h"

#include <cctype>

#include "core/logic/Jsonc.h"

namespace tpt::game::oot::logic {

namespace {

bool isSpace(char c) { return std::isspace(static_cast<unsigned char>(c)); }

// Strip comments (//, /* */, #) and flatten multi-line strings into
// single-line JSON-legal strings. One pass — string literals are
// detected so that comment delimiters inside them don't get eaten and
// internal newlines do get replaced with a single space.
std::string passOne(std::string_view src) {
    std::string out;
    out.reserve(src.size());
    for (std::size_t i = 0; i < src.size();) {
        const char c = src[i];

        if (c == '"') {
            // Walk the string, copying bytes. Replace any run of
            // `\n` + whitespace with one space so the resulting JSON
            // string is on one line and rule expressions stay readable.
            out.push_back(c);
            ++i;
            while (i < src.size()) {
                const char s = src[i];
                if (s == '\\' && i + 1 < src.size()) {
                    out.push_back(s);
                    out.push_back(src[i + 1]);
                    i += 2;
                    continue;
                }
                if (s == '\n' || s == '\r') {
                    // Collapse newline + leading whitespace of the
                    // next line into a single space. Preserves token
                    // boundaries for the rule parser.
                    while (i < src.size() && isSpace(src[i])) ++i;
                    if (i < src.size() && src[i] != '"') out.push_back(' ');
                    continue;
                }
                out.push_back(s);
                ++i;
                if (s == '"') break;
            }
            continue;
        }

        if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
            i += 2;
            while (i < src.size() && src[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
            i += 2;
            while (i + 1 < src.size() && !(src[i] == '*' && src[i + 1] == '/')) ++i;
            if (i + 1 < src.size()) i += 2;
            continue;
        }
        if (c == '#') {
            // Python-style line comment — OoTR uses these throughout.
            // Treat identically to `//`.
            ++i;
            while (i < src.size() && src[i] != '\n') ++i;
            continue;
        }

        out.push_back(c);
        ++i;
    }
    return out;
}

// Trailing-comma elision. Same as core/logic/Jsonc.cpp but local so we
// can keep the OoT prep entirely in one translation unit.
std::string passTwo(std::string_view src) {
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

std::string prepOotJson(std::string_view src) {
    return passTwo(passOne(src));
}

}  // namespace tpt::game::oot::logic
