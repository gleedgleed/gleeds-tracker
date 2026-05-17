#include "game/oot/logic/WorldGraph.h"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "game/oot/logic/JsonPrep.h"

namespace tpt::game::oot::logic {

namespace {

void loadOneFile(const std::filesystem::path& path, RegionMap& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw WorldGraphError("cannot open " + path.string());
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    const std::string prepared = prepOotJson(buf.str());

    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(prepared);
    } catch (const std::exception& e) {
        throw WorldGraphError("JSON parse failed for " + path.string()
                              + ": " + e.what());
    }
    if (!doc.is_array()) {
        throw WorldGraphError(path.string() + " is not a top-level array");
    }

    for (const auto& entry : doc) {
        Region r;
        if (entry.contains("region_name") && entry["region_name"].is_string()) {
            r.name = entry["region_name"].get<std::string>();
        } else {
            throw WorldGraphError("region without region_name in " + path.string());
        }
        if (entry.contains("dungeon") && entry["dungeon"].is_string()) {
            r.dungeon = entry["dungeon"].get<std::string>();
        }
        if (entry.contains("savewarp") && entry["savewarp"].is_string()) {
            r.savewarp = entry["savewarp"].get<std::string>();
        }
        if (entry.contains("time_passes")) {
            // OoTR uses either true booleans or a list of subregions
            // depending on the entry. We accept either; non-bool values
            // are interpreted as "true" since the presence of the field
            // alone signals that time passes here.
            if (entry["time_passes"].is_boolean()) {
                r.timePasses = entry["time_passes"].get<bool>();
            } else {
                r.timePasses = true;
            }
        }

        auto absorbDict = [&](const char* key, std::vector<Edge>& out_v) {
            if (!entry.contains(key) || !entry[key].is_object()) return;
            for (auto it = entry[key].begin(); it != entry[key].end(); ++it) {
                Edge e;
                e.target = it.key();
                if (it.value().is_string()) {
                    e.rule = it.value().get<std::string>();
                } else if (it.value().is_boolean()) {
                    // `true` / `false` literals appear in some entries.
                    e.rule = it.value().get<bool>() ? "True" : "False";
                } else {
                    // Unknown shape — store the JSON serialization so
                    // the parser stage can complain with full context.
                    e.rule = it.value().dump();
                }
                out_v.push_back(std::move(e));
            }
        };
        absorbDict("exits",     r.exits);
        absorbDict("locations", r.locations);
        absorbDict("events",    r.events);

        // Duplicate region_name across files is an error — silently
        // dropping the second would hide breakage.
        if (out.count(r.name)) {
            throw WorldGraphError("duplicate region '" + r.name + "' in "
                                  + path.string());
        }
        out.emplace(r.name, std::move(r));
    }
}

}  // namespace

RegionMap loadRegions(const std::filesystem::path& worldRoot) {
    namespace fs = std::filesystem;
    if (!fs::exists(worldRoot) || !fs::is_directory(worldRoot)) {
        throw WorldGraphError("world root not found: " + worldRoot.string());
    }
    RegionMap out;
    for (const auto& entry : fs::directory_iterator(worldRoot)) {
        if (!entry.is_regular_file()) continue;
        const auto& p = entry.path();
        if (p.extension() != ".json") continue;

        // OoTR ships parallel vanilla + Master Quest layouts for every
        // dungeon ("Deku Tree.json" + "Deku Tree MQ.json", etc.). Both
        // define the same region names with different contents; only
        // one variant is active per seed. Until settings-string parsing
        // tells us which dungeons are MQ for the loaded seed, we skip
        // every MQ file and load vanilla only. See doc/oot-todo.md.
        const auto stem = p.stem().string();
        if (stem.size() >= 3 &&
            stem.compare(stem.size() - 3, 3, " MQ") == 0) {
            continue;
        }

        loadOneFile(p, out);
    }
    if (out.empty()) {
        throw WorldGraphError("no regions loaded from " + worldRoot.string());
    }
    return out;
}

}  // namespace tpt::game::oot::logic
