#include "game/oot/logic/Evaluator.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace tpt::game::oot::logic {

namespace {

using tpt::core::logic::Node;
using tpt::core::logic::NodePtr;

// Per-call macro-arg binding. Pushed when entering a parameterized
// alias, popped when leaving. We carry it by const-ref through every
// recursive evaluation so each subtree sees the right substitutions.
using Scope = std::unordered_map<std::string, NodePtr>;

// `'Goron Tunic'` (string literal with a space) and `Goron_Tunic`
// (bare ident) refer to the same item. OoTR's `escape_name` converts
// the former to the latter; we do the same here.
std::string escapeName(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) out.push_back(c == ' ' ? '_' : c);
    return out;
}

}  // namespace

Evaluator::Evaluator(const Context& ctx, const AliasTable& aliases)
    : ctx_(ctx), aliases_(aliases) {}

// Forward-declare a stack of helpers; defined inside the class via
// static dispatch through the public evaluate() method.

namespace {

// Forward decls for mutual recursion across helpers.
bool evalImpl(const Evaluator& E, const Context& ctx,
              const AliasTable& aliases,
              const Node& node, const Scope& scope);

// Pull a "value as a string" out of a node, with the same semantics
// OoTR uses for setting comparisons: scope binding → setting →
// stringified-int (for item counts) → fallback empty.
std::optional<std::string> resolveSettingText(const Context& ctx,
                                              const Node& n,
                                              const Scope& scope) {
    if (n.kind == Node::Kind::StringLit) return n.value;
    if (n.kind == Node::Kind::Ident) {
        if (auto it = scope.find(n.name); it != scope.end()) {
            return resolveSettingText(ctx, *it->second, scope);
        }
        if (auto it = ctx.settings.find(n.name); it != ctx.settings.end()) {
            return it->second;
        }
        if (auto it = ctx.items.find(n.name); it != ctx.items.end()) {
            return std::to_string(it->second);
        }
        return std::nullopt;
    }
    return std::nullopt;
}

// Equality semantics for `item == Bow` (both sides idents not in
// settings): compare by name string. Matches OoTR RuleParser's
// "fast check" path at RuleParser.py:246.
std::optional<std::string> resolveIdentName(const Node& n,
                                            const Scope& scope) {
    if (n.kind == Node::Kind::Ident) {
        if (auto it = scope.find(n.name); it != scope.end()) {
            return resolveIdentName(*it->second, scope);
        }
        return n.name;
    }
    if (n.kind == Node::Kind::StringLit) return n.value;
    return std::nullopt;
}

// Count check for the LHS of `(Item, N)`. Same resolution as Ident-as-
// boolean but returns the integer count rather than a truthy bool.
int countOf(const Context& ctx, const AliasTable& aliases,
            const std::string& name, const Scope& scope) {
    if (auto it = scope.find(name); it != scope.end()) {
        if (it->second->kind == Node::Kind::Ident) {
            return countOf(ctx, aliases, it->second->name, scope);
        }
        // Bound to a complex expression — best we can do is treat
        // truthy as 1.
        // Recursive evaluation reuses the same scope.
        // Inline avoids needing Evaluator& here.
        // (countOf is only called from Count nodes, rare deep nesting.)
        // We don't have an Evaluator& handy; fall back to 0.
        return 0;
    }
    if (auto it = aliases.find(name); it != aliases.end() &&
        it->second.params.empty()) {
        // Plain alias: if it's a direct Ident, follow it. Otherwise we
        // can't recover a numeric count without re-running evaluation,
        // which we can do by recursing through evalImpl.
        if (it->second.body->kind == Node::Kind::Ident) {
            return countOf(ctx, aliases, it->second.body->name, scope);
        }
        return 0;
    }
    if (auto it = ctx.items.find(name); it != ctx.items.end()) {
        return it->second;
    }
    return 0;
}

// Convert a Count-arg expression to an integer. Used for
// `has_medallions(N)`, `(Item, N)`, etc.
int countArg(const Context& ctx, const Node& n, const Scope& scope) {
    if (n.kind == Node::Kind::StringLit) {
        try { return std::stoi(n.value); } catch (...) { return 0; }
    }
    if (n.kind == Node::Kind::Ident) {
        if (auto it = scope.find(n.name); it != scope.end()) {
            return countArg(ctx, *it->second, scope);
        }
        if (auto it = ctx.settings.find(n.name); it != ctx.settings.end()) {
            try { return std::stoi(it->second); } catch (...) { return 0; }
        }
    }
    return 0;
}

// Count distinct items from a fixed list — used by has_medallions/
// has_stones/has_dungeon_rewards.
int countDistinctItems(const Context& ctx,
                       std::initializer_list<const char*> names) {
    int n = 0;
    for (auto* name : names) {
        if (auto it = ctx.items.find(name); it != ctx.items.end() && it->second > 0) ++n;
    }
    return n;
}

bool evalIdent(const Context& ctx, const AliasTable& aliases,
               const std::string& name, const Scope& scope);

bool evalCall(const Context& ctx, const AliasTable& aliases,
              const Node& node, const Scope& scope);

bool evalCompare(const Context& ctx,
                 const Node& node, const Scope& scope) {
    const auto& op = node.value;
    const Node& left  = *node.left;
    const Node& right = *node.right;

    auto isSetting = [&](const Node& n) {
        // An ident is "a setting" if its name (after scope resolution)
        // appears in the settings map. We follow one level of scope
        // binding so a formal-arg bound to a setting still counts.
        if (n.kind != Node::Kind::Ident) return false;
        if (auto it = scope.find(n.name); it != scope.end()) {
            if (it->second->kind == Node::Kind::Ident) {
                return ctx.settings.find(it->second->name) != ctx.settings.end();
            }
            return false;
        }
        return ctx.settings.find(n.name) != ctx.settings.end();
    };

    const bool leftIsStr  = (left.kind  == Node::Kind::StringLit);
    const bool rightIsStr = (right.kind == Node::Kind::StringLit);

    // Setting-style comparison: at least one side is a string literal OR
    // a known setting ident. Covers both `setting == 'value'` and
    // `setting_a == setting_b` (e.g. `age == starting_age`).
    if (leftIsStr || rightIsStr || isSetting(left) || isSetting(right)) {
        auto l = resolveSettingText(ctx, left,  scope);
        auto r = resolveSettingText(ctx, right, scope);
        if (!l || !r) return ctx.permissive;
        if (op == "==") return *l == *r;
        if (op == "!=") return *l != *r;
        try {
            const double lv = std::stod(*l);
            const double rv = std::stod(*r);
            if (op == "<")  return lv <  rv;
            if (op == "<=") return lv <= rv;
            if (op == ">")  return lv >  rv;
            if (op == ">=") return lv >= rv;
        } catch (...) {}
        return false;
    }

    // Both-Ident comparison with no setting involvement: name-string
    // equality. Matches OoTR's RuleParser fast-check at line 246-251 —
    // used for `item == Bow` after substitution of a formal arg.
    auto l = resolveIdentName(left,  scope);
    auto r = resolveIdentName(right, scope);
    if (l && r) {
        if (op == "==") return *l == *r;
        if (op == "!=") return *l != *r;
    }
    return false;
}

// Hard cap on rule-evaluation recursion depth. Aliases that reference
// each other indirectly can in pathological cases exceed the host
// stack; bailing out with `false` keeps the BFS productive even if
// one rule's evaluation collapses. 256 is well above what a clean
// OoTR rule needs (longest chains observed are ~15).
constexpr int kMaxEvalDepth = 256;
thread_local int gEvalDepth = 0;
struct DepthGuard {
    DepthGuard()  { ++gEvalDepth; }
    ~DepthGuard() { --gEvalDepth; }
};

// When entering a parameterized macro call, the actual argument ASTs
// may reference idents that the *caller's* scope binds. Without
// resolving these at bind time, the callee's scope would store raw
// AST nodes that re-trigger scope lookups in the new (callee) scope,
// where the same name may be bound to itself — self-loop.
//
// We do a shallow substitution: if the arg root is an Ident bound in
// the caller's scope, store the bound value instead of the raw Ident.
// This matches OoTR's RuleParser, which textually substitutes formal
// args at macro-expansion time.
NodePtr substituteThroughScope(const NodePtr& arg, const Scope& callerScope) {
    if (!arg) return arg;
    if (arg->kind == Node::Kind::Ident) {
        if (auto it = callerScope.find(arg->name); it != callerScope.end()) {
            return it->second;
        }
    }
    return arg;
}

bool evalImpl(const Evaluator& E, const Context& ctx,
              const AliasTable& aliases,
              const Node& node, const Scope& scope) {
    DepthGuard guard;
    if (gEvalDepth > kMaxEvalDepth) return false;

    using K = Node::Kind;
    switch (node.kind) {
        case K::True:      return true;
        case K::False:     return false;
        case K::And:
            return evalImpl(E, ctx, aliases, *node.left, scope) &&
                   evalImpl(E, ctx, aliases, *node.right, scope);
        case K::Or:
            return evalImpl(E, ctx, aliases, *node.left, scope) ||
                   evalImpl(E, ctx, aliases, *node.right, scope);
        case K::Not:
            return !evalImpl(E, ctx, aliases, *node.left, scope);
        case K::Ident:
            return evalIdent(ctx, aliases, node.name, scope);
        case K::Count:
            return countOf(ctx, aliases, node.name, scope) >= node.intValue;
        case K::StringLit: {
            // Bare string literal — item-or-event reference.
            const auto escaped = escapeName(node.value);
            if (auto it = ctx.items.find(escaped); it != ctx.items.end()) {
                return it->second > 0;
            }
            if (ctx.events.count(escaped) || ctx.events.count(node.value)) {
                return true;
            }
            return false;
        }
        case K::Compare:
            return evalCompare(ctx, node, scope);
        case K::Call:
            return evalCall(ctx, aliases, node, scope);
        default:
            return false;
    }
}

bool evalIdent(const Context& ctx, const AliasTable& aliases,
               const std::string& name, const Scope& scope) {
    // 1. Scope binding
    if (auto it = scope.find(name); it != scope.end()) {
        // Recurse into the bound AST with the SAME scope. (See Evaluator.h
        // for resolution rules.)
        Evaluator dummy(ctx, aliases);
        return evalImpl(dummy, ctx, aliases, *it->second, scope);
    }
    // 2. Plain alias
    if (auto it = aliases.find(name);
        it != aliases.end() && it->second.params.empty()) {
        Evaluator dummy(ctx, aliases);
        return evalImpl(dummy, ctx, aliases, *it->second.body, scope);
    }
    // 3. Items
    if (auto it = ctx.items.find(name); it != ctx.items.end()) {
        return it->second > 0;
    }
    // 4. Settings — truthy iff "true" or non-"false"/non-empty
    if (auto it = ctx.settings.find(name); it != ctx.settings.end()) {
        if (it->second == "true")  return true;
        if (it->second == "false") return false;
        return !it->second.empty();
    }
    // 5. Events (a region might have set an event flag)
    if (ctx.events.count(name)) return true;
    return false;
}

bool evalCall(const Context& ctx, const AliasTable& aliases,
              const Node& node, const Scope& scope) {
    const auto& name = node.name;
    const auto& args = node.children;
    Evaluator dummy(ctx, aliases);

    // ----- Metarules ---------------------------------------------------------

    if (name == "at") {
        // at(region, rule): region must be in reachedRegions AND rule
        // must pass. The region is named by a StringLit (most common)
        // or an Ident (rare).
        if (args.size() != 2) return false;
        std::string region;
        if (args[0]->kind == Node::Kind::StringLit)      region = args[0]->value;
        else if (args[0]->kind == Node::Kind::Ident)     region = args[0]->name;
        else return false;
        if (!ctx.reachedRegions.count(region)) return false;
        return evalImpl(dummy, ctx, aliases, *args[1], scope);
    }
    if (name == "here") {
        // here(rule): evaluate in current context. The BFS treats
        // 'here' as a same-region guard which is automatically true
        // when the BFS is examining this region. Match by just
        // evaluating the inner rule.
        if (args.size() != 1) return false;
        return evalImpl(dummy, ctx, aliases, *args[0], scope);
    }

    // ----- has_* family ------------------------------------------------------

    if (name == "has_medallions") {
        if (args.empty()) return false;
        const int need = countArg(ctx, *args[0], scope);
        const int have = countDistinctItems(ctx, {
            "Forest_Medallion","Fire_Medallion","Water_Medallion",
            "Spirit_Medallion","Shadow_Medallion","Light_Medallion"});
        return have >= need;
    }
    if (name == "has_stones") {
        if (args.empty()) return false;
        const int need = countArg(ctx, *args[0], scope);
        const int have = countDistinctItems(ctx, {
            "Kokiri_Emerald","Goron_Ruby","Zora_Sapphire"});
        return have >= need;
    }
    if (name == "has_dungeon_rewards") {
        if (args.empty()) return false;
        const int need = countArg(ctx, *args[0], scope);
        const int have = countDistinctItems(ctx, {
            "Forest_Medallion","Fire_Medallion","Water_Medallion",
            "Spirit_Medallion","Shadow_Medallion","Light_Medallion",
            "Kokiri_Emerald","Goron_Ruby","Zora_Sapphire"});
        return have >= need;
    }
    if (name == "has_ocarina_buttons") {
        // We don't track individual ocarina buttons (vanilla = all owned
        // with the ocarina). Permissive default: assume all available
        // when an ocarina is owned.
        if (auto it = ctx.items.find("Ocarina");
            it != ctx.items.end() && it->second > 0) return true;
        return ctx.permissive;
    }
    if (name == "has_hearts" || name == "heart_count") {
        // We don't precisely model heart count for logic. Permissive
        // default — OoTR uses this rarely (only for LACS/bridge/Ganon).
        return ctx.permissive;
    }
    if (name == "has_bottle") {
        if (auto it = ctx.items.find("Bottle"); it != ctx.items.end()) {
            return it->second > 0;
        }
        return false;
    }
    if (name == "has_all_notes_for_song") {
        // Only matters when individual ocarina notes are shuffled.
        // Default: shuffle is off → all notes implicitly available.
        if (auto it = ctx.settings.find("shuffle_individual_ocarina_notes");
            it != ctx.settings.end()) {
            if (it->second == "false") return true;
        }
        return ctx.permissive;
    }
    if (name == "had_night_start") {
        return ctx.permissive;
    }
    if (name == "can_live_dmg") {
        // Damage-survivability — depends on damage_multiplier setting
        // and fairy/Nayru availability. Permissive for v1.
        return ctx.permissive;
    }
    if (name == "has_soul") {
        // Glitch-logic enemy souls; OoTR returns True unconditionally.
        return true;
    }
    if (name == "guarantee_hint") {
        return false;
    }
    if (name == "count_distinct" || name == "item_count" ||
        name == "item_name_count") {
        return ctx.permissive;
    }

    // ----- Synthetic ops from parser -----------------------------------------

    if (name == "__in__") {
        // `X in Y` — Y is typically a list-/dict-valued setting. We
        // don't yet parse those structures; permissive default.
        return ctx.permissive;
    }
    if (name == "__index__") {
        // `setting[key]` — same boat as __in__.
        return ctx.permissive;
    }
    if (name == "region_has_shortcuts") {
        return false;
    }

    // ----- Parameterized alias -----------------------------------------------

    if (auto it = aliases.find(name);
        it != aliases.end() && it->second.params.size() == args.size()) {
        // Fresh scope — macros are lexically scoped. Inheriting the
        // parent's bindings would let formal-arg names collide
        // unpredictably across nested macros.
        Scope newScope;
        for (std::size_t i = 0; i < it->second.params.size(); ++i) {
            newScope[it->second.params[i]] = substituteThroughScope(args[i], scope);
        }
        return evalImpl(dummy, ctx, aliases, *it->second.body, newScope);
    }

    // Unknown function — fall through. OoTR's parser would catch this
    // at gen time; we're more lenient and report it as "rule didn't pass".
    return false;
}

}  // namespace

bool Evaluator::evaluate(const Node& node) const {
    Scope rootScope;
    return evalImpl(*this, ctx_, aliases_, node, rootScope);
}

}  // namespace tpt::game::oot::logic
