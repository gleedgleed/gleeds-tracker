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

// The web-gen default settings as a DSL map (every setting at its generator
// default). Use as the baseline layer beneath dslSettingsFromSeed /
// dslSettingsFromParsed so unspecified settings resolve to the real default
// rather than the permissive fallback. See defaultParsedSettings().
std::unordered_map<std::string, std::string> dslDefaultSettings();

// Rooms the player can warp to right now: destinations of currently-unlocked
// portals — but ONLY if the player can actually warp, which needs wolf form
// (Shadow Crystal; `ctx` only carries it once transforming is unlocked).
// Portal switch flags can be set before the player can use them (e.g. a
// seed-pre-cleared province sets its portal switches at game start), so the
// wolf gate is required to avoid over-reporting reachability. Mirrors the
// web-gen's CanWarp().
std::vector<std::string> warpRoomsFromPortals(const std::vector<PortalState>& portals,
                                              const Context& ctx);

}  // namespace tpt::core::logic
