#include "game/oot/logic/RuleParser.h"

#include <cctype>
#include <string>
#include <utility>

namespace tpt::game::oot::logic {

namespace {

using tpt::core::logic::Node;
using tpt::core::logic::NodePtr;

bool isIdentStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
bool isIdentCont(char c) {
    return isIdentStart(c) || (c >= '0' && c <= '9');
}

class Parser {
public:
    explicit Parser(std::string_view src) : src_(src) {}

    NodePtr parse() {
        skipSpaces();
        if (src_.empty() || pos_ >= src_.size()) {
            throw RuleParseError("empty rule");
        }
        auto top = parseOr();
        skipSpaces();
        if (pos_ != src_.size()) {
            throw RuleParseError("trailing input at offset "
                                 + std::to_string(pos_));
        }
        return top;
    }

private:
    std::string_view src_;
    std::size_t pos_ = 0;

    void skipSpaces() {
        while (pos_ < src_.size() &&
               std::isspace(static_cast<unsigned char>(src_[pos_]))) {
            ++pos_;
        }
    }

    char peek() const { return pos_ < src_.size() ? src_[pos_] : '\0'; }

    bool consumeChar(char c) {
        if (peek() != c) return false;
        ++pos_;
        return true;
    }

    // Match a keyword followed by a non-ident character (or end). Doesn't
    // consume unless the match succeeds.
    bool matchKeyword(std::string_view kw) {
        if (src_.size() - pos_ < kw.size()) return false;
        if (src_.compare(pos_, kw.size(), kw) != 0) return false;
        const auto end = pos_ + kw.size();
        if (end < src_.size() && isIdentCont(src_[end])) return false;
        pos_ = end;
        return true;
    }

    // Match a literal operator (e.g. `==` or `!=`). Consumes on success.
    bool matchOp(std::string_view op) {
        if (src_.size() - pos_ < op.size()) return false;
        if (src_.compare(pos_, op.size(), op) != 0) return false;
        pos_ += op.size();
        return true;
    }

    // Greedy ident match. Returns empty string_view if no ident.
    std::string_view matchIdent() {
        if (pos_ >= src_.size() || !isIdentStart(src_[pos_])) return {};
        const auto start = pos_;
        while (pos_ < src_.size() && isIdentCont(src_[pos_])) ++pos_;
        return src_.substr(start, pos_ - start);
    }

    bool matchInt(int& out) {
        if (pos_ >= src_.size() || src_[pos_] < '0' || src_[pos_] > '9') {
            return false;
        }
        int v = 0;
        while (pos_ < src_.size() && src_[pos_] >= '0' && src_[pos_] <= '9') {
            v = v * 10 + (src_[pos_] - '0');
            ++pos_;
        }
        out = v;
        return true;
    }

    // Match an integer or floating-point literal, returning the verbatim
    // text. OoTR uses floats only inside specific function args
    // (`can_live_dmg(0.5, ...)`); we preserve the literal so the
    // evaluator can interpret it per-function when those predicates land.
    bool matchNumberText(std::string& out) {
        if (pos_ >= src_.size() || src_[pos_] < '0' || src_[pos_] > '9') {
            return false;
        }
        const auto start = pos_;
        while (pos_ < src_.size() && src_[pos_] >= '0' && src_[pos_] <= '9') {
            ++pos_;
        }
        if (pos_ < src_.size() && src_[pos_] == '.') {
            ++pos_;
            while (pos_ < src_.size() && src_[pos_] >= '0' && src_[pos_] <= '9') {
                ++pos_;
            }
        }
        out.assign(src_, start, pos_ - start);
        return true;
    }

    // Parse a single-quoted or double-quoted string. No escape handling
    // because OoTR rules don't use them.
    std::string matchString() {
        const char quote = peek();
        if (quote != '\'' && quote != '"') {
            throw RuleParseError("expected string at offset "
                                 + std::to_string(pos_));
        }
        ++pos_;
        std::string out;
        while (pos_ < src_.size() && src_[pos_] != quote) {
            out.push_back(src_[pos_]);
            ++pos_;
        }
        if (pos_ >= src_.size()) {
            throw RuleParseError("unterminated string starting at offset "
                                 + std::to_string(pos_ - out.size() - 1));
        }
        ++pos_;  // consume closing quote
        return out;
    }

    // Returns a mutable shared_ptr so the caller can fill in fields;
    // the implicit conversion to NodePtr (shared_ptr<const Node>) happens
    // when the result is stored or returned.
    std::shared_ptr<Node> makeNode(Node::Kind k) {
        auto n = std::make_shared<Node>();
        n->kind = k;
        return n;
    }

    // ----- Grammar -----

    NodePtr parseOr() {
        auto left = parseAnd();
        for (;;) {
            skipSpaces();
            if (!matchKeyword("or")) return left;
            auto right = parseAnd();
            auto n = makeNode(Node::Kind::Or);
            n->left  = std::move(left);
            n->right = std::move(right);
            left = std::move(n);
        }
    }

    NodePtr parseAnd() {
        auto left = parseNot();
        for (;;) {
            skipSpaces();
            if (!matchKeyword("and")) return left;
            auto right = parseNot();
            auto n = makeNode(Node::Kind::And);
            n->left  = std::move(left);
            n->right = std::move(right);
            left = std::move(n);
        }
    }

    NodePtr parseNot() {
        skipSpaces();
        if (matchKeyword("not")) {
            auto operand = parseNot();
            auto n = makeNode(Node::Kind::Not);
            n->left = std::move(operand);
            return n;
        }
        return parseCmp();
    }

    NodePtr parseCmp() {
        auto left = parseAtom();
        skipSpaces();

        // Comparison operators. `value` carries the operator text so the
        // evaluator can dispatch without separate node kinds. Order
        // matters: two-char ops must be tried before their single-char
        // prefixes (== before =, <= before <, >= before >).
        for (const char* op : {"==", "!=", "<=", ">=", "<", ">"}) {
            if (matchOp(op)) {
                auto right = parseAtom();
                auto n = makeNode(Node::Kind::Compare);
                n->left  = std::move(left);
                n->right = std::move(right);
                n->value = op;
                // Legacy: TP parser uses `sense` (true == eq, false ==
                // neq). Preserved for compatibility with any TP code that
                // still reads it; ignored for non-eq ops.
                n->sense = (std::string_view(op) == "==");
                return n;
            }
        }

        // Python `X in Y` — membership test on collection settings like
        // `'Deku Tree' in dungeon_shortcuts`. Emitted as a synthetic
        // Call("__in__", [needle, haystack]) for the evaluator to dispatch.
        if (matchKeyword("in")) {
            auto right = parseAtom();
            auto n = makeNode(Node::Kind::Call);
            n->name = "__in__";
            n->children.push_back(std::move(left));
            n->children.push_back(std::move(right));
            return n;
        }
        return left;
    }

    NodePtr parseAtom() {
        skipSpaces();
        if (pos_ >= src_.size()) {
            throw RuleParseError("unexpected end of input");
        }

        if (consumeChar('(')) return parseParen();

        // True / False / None literals — check before ident so we don't
        // emit them as identifiers.
        if (matchKeyword("True"))  return tpt::core::logic::makeTrue();
        if (matchKeyword("False")) return tpt::core::logic::makeFalse();
        // OoTR uses None very rarely in evaluated rules. Treat as False
        // (an item the player never has) — matches OoTR's evaluator
        // behavior where None acts like an absent item.
        if (matchKeyword("None"))  return tpt::core::logic::makeFalse();

        // String literal — for setting comparisons like `setting == 'value'`.
        if (peek() == '\'' || peek() == '"') {
            auto s = matchString();
            auto n = makeNode(Node::Kind::StringLit);
            n->value = std::move(s);
            return n;
        }

        // Numeric literal — appears as function-call args
        // (`can_live_dmg(0.5, False)`) and inside tuple form (handled in
        // parseParen). Preserved verbatim as a StringLit so a future
        // evaluator can interpret per-call.
        if (peek() >= '0' && peek() <= '9') {
            std::string numText;
            matchNumberText(numText);
            auto n = makeNode(Node::Kind::StringLit);
            n->value = std::move(numText);
            return n;
        }

        const auto ident = matchIdent();
        if (ident.empty()) {
            throw RuleParseError("expected expression at offset "
                                 + std::to_string(pos_));
        }

        // Function-call form: `ident(args)`.
        skipSpaces();
        if (peek() == '(') {
            ++pos_;
            auto call = makeNode(Node::Kind::Call);
            call->name = std::string(ident);
            skipSpaces();
            if (peek() != ')') {
                for (;;) {
                    call->children.push_back(parseOr());
                    skipSpaces();
                    if (consumeChar(',')) {
                        skipSpaces();
                        continue;
                    }
                    break;
                }
            }
            skipSpaces();
            if (!consumeChar(')')) {
                throw RuleParseError("expected ')' after call args at offset "
                                     + std::to_string(pos_));
            }
            return call;
        }

        // Subscript form: `setting[key]`. OoTR uses this to read dict-
        // valued settings like `skipped_trials[Forest]`. Emitted as a
        // synthetic Call("__index__", [collection, index]) — same shape
        // as Python's `__getitem__` op.
        if (peek() == '[') {
            ++pos_;
            skipSpaces();
            auto index = parseOr();
            skipSpaces();
            if (!consumeChar(']')) {
                throw RuleParseError("expected ']' after subscript at offset "
                                     + std::to_string(pos_));
            }
            auto coll = makeNode(Node::Kind::Ident);
            coll->name = std::string(ident);
            auto call = makeNode(Node::Kind::Call);
            call->name = "__index__";
            call->children.push_back(std::move(coll));
            call->children.push_back(std::move(index));
            return call;
        }

        // Bare identifier — most common case (items, settings, predicate
        // names, event names).
        auto n = makeNode(Node::Kind::Ident);
        n->name = std::string(ident);
        return n;
    }

    // Body of `(...)`. Two forms:
    //   - `(expr)`        — parenthesized expression
    //   - `(ident, int)`  — counted-item shorthand (becomes Kind::Count)
    NodePtr parseParen() {
        skipSpaces();
        auto inner = parseOr();
        skipSpaces();
        if (consumeChar(',')) {
            // Tuple form. OoTR requires first element to be a Name (ident)
            // or string literal. We accept Ident; that covers every rule
            // currently in the OoTR tree.
            if (inner->kind != Node::Kind::Ident) {
                throw RuleParseError(
                    "comma inside parens but first element isn't an "
                    "identifier (counted-item form expects "
                    "`(Item_Name, N)`) at offset " + std::to_string(pos_));
            }
            skipSpaces();
            int n = 0;
            if (!matchInt(n)) {
                // OoTR also allows a setting-ident as the count (resolved
                // at expand time). Until alias-expansion lands we accept
                // both: number → fixed count, ident → 1 (best-effort).
                const auto countIdent = matchIdent();
                if (countIdent.empty()) {
                    throw RuleParseError(
                        "expected integer or setting ident as count at "
                        "offset " + std::to_string(pos_));
                }
                n = 1;  // alias-expansion will refine this later
                (void)countIdent;
            }
            skipSpaces();
            if (!consumeChar(')')) {
                throw RuleParseError(
                    "expected ')' after counted-item at offset "
                    + std::to_string(pos_));
            }
            auto out = makeNode(Node::Kind::Count);
            out->name = inner->name;
            out->intValue = n;
            return out;
        }
        skipSpaces();
        if (!consumeChar(')')) {
            throw RuleParseError("expected ')' at offset "
                                 + std::to_string(pos_));
        }
        return inner;
    }
};

}  // namespace

NodePtr parseRule(std::string_view src) {
    return Parser(src).parse();
}

}  // namespace tpt::game::oot::logic
