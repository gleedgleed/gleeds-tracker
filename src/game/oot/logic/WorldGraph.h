#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace tpt::game::oot::logic {

// One outgoing edge from a Region: either to another Region (an "exit"),
// or to a Location/check that's "in" this region, or to an Event that
// becomes a virtual item when its rule passes.
//
// The rule string is stored unparsed here. Parsing happens later, once
// the OoT rule parser is in place — that lets the world-loader stage
// land independently of the parser stage.
struct Edge {
    std::string target;
    std::string rule;
};

// A region from OoTR's World/*.json files. Mirrors the OoTR `Region`
// concept directly: regions have outbound exits (to other regions),
// in-region locations (check names with access rules), and events
// (named flags that become "true" once their region is reached and
// the rule passes).
//
// We intentionally don't reuse src/core/logic/WorldData.h's `Room`
// type: OoT's graph has extra concepts (events, time-passes, savewarp,
// dungeon affiliation) that don't apply to TP. Once both games are
// running, a shared "Reach.cpp" stays generic by templating on whatever
// graph type its caller passes — same BFS, different node shape.
struct Region {
    std::string name;
    std::string dungeon;         // empty for overworld regions
    std::string savewarp;        // empty if no save-warp landing here
    bool        timePasses = false;
    std::vector<Edge> exits;
    std::vector<Edge> locations;
    std::vector<Edge> events;
};

using RegionMap = std::unordered_map<std::string, Region>;

class WorldGraphError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Load every region from `worldRoot/*.json`. The directory is expected
// to mirror OoT-Randomizer-Dev/data/World/ — at runtime the binary
// reads from <exedir>/data/oot-world/ which CMake populates at
// build time. Throws on missing directory or JSON parse failure.
RegionMap loadRegions(const std::filesystem::path& worldRoot);

}  // namespace tpt::game::oot::logic
