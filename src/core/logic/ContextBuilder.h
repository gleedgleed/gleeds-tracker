#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "core/EventFlags.h"
#include "core/Items.h"
#include "core/QuestState.h"
#include "core/SeedHeader.h"
#include "core/SettingsString.h"
#include "core/logic/Context.h"

namespace tpt::core::logic {

// Convert an Inventory + QuestState + (optional) save-state-derived flag
// dicts into a fully-populated logic::Context. Predicates are registered
// via `registerPredicates` for you. Item names match the rando DSL.
Context buildContext(
    const Inventory& inv,
    const QuestState& qs,
    const std::unordered_map<std::string, bool>& eventFlags,
    const std::unordered_map<std::string, bool>& getItemFlags,
    bool glitched,
    const std::unordered_map<std::string, std::string>& settings);

// Helpers to derive event-flag / get-item-flag dicts from a save block.
std::unordered_map<std::string, bool> readAllEventFlags(std::span<const std::uint8_t> save);
std::unordered_map<std::string, bool> readAllGetItemFlags(std::span<const std::uint8_t> save);

// DSL setting-name overlays. Use seed-header first, then overlay the settings
// string for fuller coverage.
std::unordered_map<std::string, std::string> dslSettingsFromSeed(const SeedSettings& s);
std::unordered_map<std::string, std::string> dslSettingsFromParsed(const ParsedSettings& s);

// Map portal-display-name -> destination room in the web-gen graph.
// Returns the room names corresponding to currently-unlocked portals.
std::vector<std::string> warpRoomsFromPortals(const std::vector<PortalState>& portals);

}  // namespace tpt::core::logic
