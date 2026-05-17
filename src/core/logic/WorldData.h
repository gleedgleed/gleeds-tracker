#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace tpt::core::logic {

struct Exit {
    std::string target;
    std::string req;          // glitchless requirement expression
    std::string reqGlitched;  // glitched requirement expression
};

struct Room {
    std::string name;
    std::string region;
    std::vector<Exit> exits;
    std::vector<std::string> checks;
};

struct Check {
    std::string name;
    std::string req;
    std::string reqGlitched;
    std::vector<std::string> categories;
    std::string itemId;       // empty if absent
};

// Indexed by room name / check name for O(1) lookup.
using RoomMap  = std::unordered_map<std::string, Room>;
using CheckMap = std::unordered_map<std::string, Check>;

class WorldDataError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// Walk worldRoot/Rooms/**/*.jsonc and load every room. Throws on missing
// directory or malformed JSONC.
RoomMap  loadRooms (const std::filesystem::path& worldRoot);

// Walk worldRoot/Checks/**/*.jsonc.
CheckMap loadChecks(const std::filesystem::path& worldRoot);

}  // namespace tpt::core::logic
