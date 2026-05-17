#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace tpt::game::oot::logic {

// State the evaluator queries when resolving a rule. Populated by
// ContextBuilder from the decoded SaveFlags + Inventory + PlayerData;
// then mutated by Reach's BFS as it discovers new regions and triggers
// events.
struct Context {
    // Items the player owns. Keys use OoTR's snake-case naming
    // (`Hookshot`, `Bow`, `Progressive_Strength_Upgrade`, `Bomb_Bag`,
    // `Forest_Medallion`, `Zeldas_Lullaby`, `Gold_Skulltula_Token`,
    // …). Values are counts: 1 for binary items, N for counted items
    // (skulltulas, hookshot tier 1 vs longshot tier 2, etc.).
    std::unordered_map<std::string, int> items;

    // OoTR settings — name → string value. Booleans stored as "true" /
    // "false"; enums as their enum value ("open" / "closed"); ints as
    // decimal string. Unknown settings (looked up against this map by
    // Compare or __in__) fall back to `permissive` semantics.
    std::unordered_map<std::string, std::string> settings;

    // Events that have triggered. Populated by the reach BFS as
    // region-scoped events become reachable + their rule passes.
    // Reference target for OoTR rules that name events by their string
    // form (e.g. `'Defeat Queen Gohma'`).
    std::unordered_set<std::string> events;

    // Regions BFS has marked reachable. Used by `at(region, rule)` and
    // by region-name string literals in rules.
    std::unordered_set<std::string> reachedRegions;

    // Player's current form. Drives `is_adult` / `is_child` and
    // age-dependent item usability (Megaton Hammer = adult-only, etc.).
    bool isAdult = false;

    // Permissive mode: unknown settings comparisons resolve to true.
    // Effect is "show what could be reachable assuming favorable
    // settings" — overestimates but stays useful until settings-string
    // parsing lands.
    bool permissive = true;
};

}  // namespace tpt::game::oot::logic
