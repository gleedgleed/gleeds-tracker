#include "memory/PatternScan.h"

#include <cctype>
#include <cstring>
#include <stdexcept>

namespace tpt::memory {

namespace {
int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
}  // namespace

Pattern parsePattern(std::string_view s) {
    Pattern p;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        if (i >= s.size()) break;
        if (i + 1 >= s.size()) {
            throw std::invalid_argument("pattern: trailing half-byte");
        }
        if (s[i] == '?' && s[i + 1] == '?') {
            p.bytes.push_back(0);
            p.wildcards.push_back(true);
            i += 2;
            continue;
        }
        const int hi = hexval(s[i]);
        const int lo = hexval(s[i + 1]);
        if (hi < 0 || lo < 0) {
            throw std::invalid_argument("pattern: bad hex byte");
        }
        p.bytes.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
        p.wildcards.push_back(false);
        i += 2;
    }
    if (p.bytes.empty()) {
        throw std::invalid_argument("pattern: empty");
    }
    return p;
}

std::optional<std::size_t> findPattern(std::span<const std::uint8_t> buf,
                                       const Pattern& p) {
    if (p.size() == 0 || buf.size() < p.size()) return std::nullopt;
    const std::size_t last = buf.size() - p.size();
    for (std::size_t i = 0; i <= last; ++i) {
        bool match = true;
        for (std::size_t k = 0; k < p.size(); ++k) {
            if (p.wildcards[k]) continue;
            if (buf[i + k] != p.bytes[k]) { match = false; break; }
        }
        if (match) return i;
    }
    return std::nullopt;
}

std::vector<std::size_t> findAllPatterns(std::span<const std::uint8_t> buf,
                                         const Pattern& p) {
    std::vector<std::size_t> out;
    if (p.size() == 0 || buf.size() < p.size()) return out;
    const std::size_t last = buf.size() - p.size();
    for (std::size_t i = 0; i <= last; ++i) {
        bool match = true;
        for (std::size_t k = 0; k < p.size(); ++k) {
            if (p.wildcards[k]) continue;
            if (buf[i + k] != p.bytes[k]) { match = false; break; }
        }
        if (match) out.push_back(i);
    }
    return out;
}

std::uintptr_t decodeRipRel32(std::uintptr_t instrAddr,
                              std::size_t instrLength,
                              const std::uint8_t* disp) {
    std::int32_t d = 0;
    std::memcpy(&d, disp, 4);
    return static_cast<std::uintptr_t>(
        static_cast<std::int64_t>(instrAddr + instrLength) +
        static_cast<std::int64_t>(d));
}

}  // namespace tpt::memory
