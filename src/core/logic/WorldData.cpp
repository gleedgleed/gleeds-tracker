#include "core/logic/WorldData.h"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "core/logic/Jsonc.h"

namespace tpt::core::logic {

namespace {

std::string readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw WorldDataError("cannot open " + p.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Parse a JSONC file into nlohmann::json. Wraps parse errors with file context.
nlohmann::json parseJsonc(const std::filesystem::path& p) {
    const auto stripped = stripJsonc(readFile(p));
    try {
        return nlohmann::json::parse(stripped);
    } catch (const std::exception& e) {
        throw WorldDataError("JSON parse error in " + p.string() + ": " + e.what());
    }
}

std::vector<std::filesystem::path> jsoncFiles(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> out;
    if (!std::filesystem::exists(root)) {
        throw WorldDataError("missing directory " + root.string());
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().extension() == ".jsonc") {
            out.push_back(entry.path());
        }
    }
    return out;
}

}  // namespace

RoomMap loadRooms(const std::filesystem::path& worldRoot) {
    RoomMap out;
    const auto roomsDir = worldRoot / "Rooms";
    for (const auto& path : jsoncFiles(roomsDir)) {
        const auto j = parseJsonc(path);
        if (!j.is_array()) {
            throw WorldDataError("expected array at top of " + path.string());
        }
        for (const auto& r : j) {
            Room room;
            room.name   = r.at("RoomName").get<std::string>();
            room.region = r.value("Region", "");

            if (auto it = r.find("Exits"); it != r.end()) {
                for (const auto& e : *it) {
                    Exit ex;
                    ex.target      = e.at("ConnectedArea").get<std::string>();
                    ex.req         = e.value("Requirements",        "false");
                    ex.reqGlitched = e.value("GlitchedRequirements", "false");
                    room.exits.push_back(std::move(ex));
                }
            }
            if (auto it = r.find("Checks"); it != r.end()) {
                for (const auto& c : *it) {
                    if (c.is_string()) {
                        auto s = c.get<std::string>();
                        if (!s.empty()) room.checks.push_back(std::move(s));
                    }
                }
            }
            out.emplace(room.name, std::move(room));
        }
    }
    return out;
}

CheckMap loadChecks(const std::filesystem::path& worldRoot) {
    CheckMap out;
    const auto checksDir = worldRoot / "Checks";
    for (const auto& path : jsoncFiles(checksDir)) {
        const auto j = parseJsonc(path);

        Check chk;
        chk.name        = path.stem().string();
        chk.req         = j.value("requirements",        "false");
        chk.reqGlitched = j.value("glitchedRequirements", "false");
        chk.itemId      = j.value("itemId", "");

        if (auto it = j.find("checkCategory"); it != j.end() && it->is_array()) {
            for (const auto& c : *it) {
                if (c.is_string()) chk.categories.push_back(c.get<std::string>());
            }
        }
        out.emplace(chk.name, std::move(chk));
    }
    return out;
}

}  // namespace tpt::core::logic
