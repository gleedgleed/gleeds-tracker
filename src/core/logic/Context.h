#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace tpt::core::logic {

class Context;

// Predicate: resolves an identifier (function call) to a bool given the
// current context. Registered in Context::predicates.
using Predicate = std::function<bool(const Context&)>;

class Context {
  public:
    // Item name (DSL snake_case) -> integer count owned.
    std::unordered_map<std::string, int> items;

    // Predicate name -> resolver. Identifiers that aren't here fall back to
    // an item-count check (>= 1).
    std::unordered_map<std::string, Predicate> predicates;

    // Rooms currently considered reachable. Owned by the BFS engine; the
    // evaluator only reads it.
    std::unordered_set<std::string> reachedRooms;

    // If true, exits/checks evaluate against `glitchedRequirements`.
    bool glitched = false;

    // If true, Setting comparators on unknown settings return true; if false,
    // they return false. Mirrors Python's permissive_settings.
    bool permissiveSettings = true;

    // Save-state-derived fields used by some predicates.
    int  darkClearLevel = 0;

    // Event flag name -> bool. From save's dSv_event_c.
    std::unordered_map<std::string, bool> eventFlags;

    // First-time-got item bits: item name -> bool. From dSv_player_get_item_c.
    std::unordered_map<std::string, bool> getItemFlags;

    // Seed settings (from TPR header scan + settings string overlay):
    // setting_name -> value (enum string, "true"/"false", or stringified int).
    std::unordered_map<std::string, std::string> settings;
};

}  // namespace tpt::core::logic
