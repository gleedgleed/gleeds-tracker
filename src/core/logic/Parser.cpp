#include "core/logic/Parser.h"

#include <cctype>
#include <mutex>
#include <unordered_map>

namespace tpt::core::logic {

namespace {

// ----------------------------------------------------------------------------
// Token recognition. The DSL alphabet is small enough that hand-coded matchers
// beat std::regex — and they're easier to read.

bool isIdentStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
bool isIdentCont(char c) {
    return isIdentStart(c) || (c >= '0' && c <= '9');
}

void skipSpaces(std::string_view& s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
}

// If `s` starts with a word `kw` followed by a non-ident char (or end),
// consume it and return true.
bool matchKeyword(std::string_view& s, std::string_view kw) {
    if (s.size() < kw.size()) return false;
    if (s.compare(0, kw.size(), kw) != 0) return false;
    if (s.size() > kw.size() && isIdentCont(s[kw.size()])) return false;
    s.remove_prefix(kw.size());
    return true;
}

// Greedy ident match. Returns the matched range and consumes it from `s`.
std::string_view matchIdent(std::string_view& s) {
    std::size_t i = 0;
    while (i < s.size() && isIdentCont(s[i])) ++i;
    auto out = s.substr(0, i);
    s.remove_prefix(i);
    return out;
}

// Parse an unsigned decimal integer.
bool matchInt(std::string_view& s, int& out) {
    if (s.empty() || s.front() < '0' || s.front() > '9') return false;
    int v = 0;
    std::size_t i = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        v = v * 10 + (s[i] - '0');
        ++i;
    }
    s.remove_prefix(i);
    out = v;
    return true;
}

// ----------------------------------------------------------------------------
// Recursive-descent parser. Mirrors logic.py::_parse_inner exactly:
//   - right-associative (and/or fold the rest of the expression to the right)
//   - inner expression terminates on ')' or end-of-string

NodePtr parseInner(std::string_view& s, int depth);

// Parse a parenthesised form. `s` is positioned just after the '('.
//   ( ident , int )                 -> count
//   ( Setting.name equals val )     -> setting (sense=true)
//   ( Setting.name not_equal val )  -> setting (sense=false)
//   ( Expr )                        -> sub-expr
NodePtr parseParen(std::string_view& s) {
    auto save = s;
    skipSpaces(s);

    // Counted-item: starts with ident then comma then int then ')'.
    if (!s.empty() && isIdentStart(s.front())) {
        const auto trial = s;
        const auto ident = matchIdent(s);
        skipSpaces(s);
        if (!s.empty() && s.front() == ',') {
            s.remove_prefix(1);
            skipSpaces(s);
            int n = 0;
            if (matchInt(s, n)) {
                skipSpaces(s);
                if (!s.empty() && s.front() == ')') {
                    s.remove_prefix(1);
                    auto node = std::make_shared<Node>();
                    node->kind = Node::Kind::Count;
                    node->name = std::string(ident);
                    node->intValue = n;
                    return node;
                }
            }
        }
        // Setting comparator: starts with "Setting.name <op> val ".
        s = trial;
        if (matchKeyword(s, "Setting") && !s.empty() && s.front() == '.') {
            s.remove_prefix(1);
            const auto name = matchIdent(s);
            skipSpaces(s);
            bool sense = true;
            if (matchKeyword(s, "equals")) sense = true;
            else if (matchKeyword(s, "not_equal")) sense = false;
            else throw ParseError("Setting comparator missing 'equals'/'not_equal'");
            skipSpaces(s);
            const auto val = matchIdent(s);
            skipSpaces(s);
            if (s.empty() || s.front() != ')') throw ParseError("Expected ')' after Setting comparator");
            s.remove_prefix(1);
            auto node = std::make_shared<Node>();
            node->kind  = Node::Kind::Setting;
            node->name  = std::string(name);
            node->value = std::string(val);
            node->sense = sense;
            return node;
        }
    }

    // Fallthrough: parenthesised sub-expression. Restore s and parse normally.
    s = save;
    auto inner = parseInner(s, /*depth=*/1);
    skipSpaces(s);
    if (s.empty() || s.front() != ')') throw ParseError("Expected ')'");
    s.remove_prefix(1);
    return inner ? inner : makeFalse();
}

NodePtr parseInner(std::string_view& s, int depth) {
    NodePtr tree;
    while (!s.empty()) {
        skipSpaces(s);
        if (s.empty()) break;

        NodePtr current;

        if (s.front() == '(') {
            s.remove_prefix(1);
            current = parseParen(s);
        }
        else if (matchKeyword(s, "true"))  current = makeTrue();
        else if (matchKeyword(s, "false")) current = makeFalse();
        else if (matchKeyword(s, "and")) {
            auto right = parseInner(s, depth);
            auto node = std::make_shared<Node>();
            node->kind  = Node::Kind::And;
            node->left  = tree;
            node->right = right ? right : makeFalse();
            current = node;
        }
        else if (matchKeyword(s, "or")) {
            auto right = parseInner(s, depth);
            auto node = std::make_shared<Node>();
            node->kind  = Node::Kind::Or;
            node->left  = tree;
            node->right = right ? right : makeFalse();
            current = node;
        }
        else if (s.size() >= 5 && s.compare(0, 5, "Room.") == 0) {
            s.remove_prefix(5);
            const auto ident = matchIdent(s);
            std::string roomName(ident);
            for (auto& c : roomName) if (c == '_') c = ' ';
            auto node = std::make_shared<Node>();
            node->kind = Node::Kind::Room;
            node->name = std::move(roomName);
            current = node;
        }
        else if (isIdentStart(s.front())) {
            const auto ident = matchIdent(s);
            auto node = std::make_shared<Node>();
            node->kind = Node::Kind::Ident;
            node->name = std::string(ident);
            current = node;
        }
        else if (s.front() == ')') {
            if (depth > 0) break;
            throw ParseError("Unexpected ')'");
        }
        else {
            throw ParseError(std::string("Cannot parse remainder: ") + std::string(s));
        }

        tree = current;
    }
    return tree;
}

std::mutex g_cacheMutex;
std::unordered_map<std::string, NodePtr> g_cache;

}  // namespace

NodePtr parse(std::string_view expr) {
    {
        std::lock_guard lock(g_cacheMutex);
        const auto it = g_cache.find(std::string(expr));
        if (it != g_cache.end()) return it->second;
    }

    auto rest = expr;
    NodePtr ast;
    try {
        ast = parseInner(rest, /*depth=*/0);
    } catch (const ParseError& e) {
        throw ParseError(std::string("Failed to parse logic expression \"")
                         + std::string(expr) + "\": " + e.what());
    }
    if (!ast) ast = makeFalse();

    std::lock_guard lock(g_cacheMutex);
    g_cache.emplace(std::string(expr), ast);
    return ast;
}

void clearParseCache() {
    std::lock_guard lock(g_cacheMutex);
    g_cache.clear();
}

std::size_t parseCacheSize() {
    std::lock_guard lock(g_cacheMutex);
    return g_cache.size();
}

}  // namespace tpt::core::logic
