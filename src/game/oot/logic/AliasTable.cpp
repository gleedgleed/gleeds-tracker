#include "game/oot/logic/AliasTable.h"

#include <cctype>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "game/oot/logic/JsonPrep.h"
#include "game/oot/logic/RuleParser.h"

namespace tpt::game::oot::logic {

namespace {

bool isIdentStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
bool isIdentCont(char c) {
    return isIdentStart(c) || (c >= '0' && c <= '9');
}

// Split a LogicHelpers key into `(name, params)`. The key is either a
// bare ident or `name(p1, p2, ...)`.
//   "Hookshot"        → ("Hookshot", [])
//   "can_play(song)"  → ("can_play", ["song"])
//   "has_projectile(for_age)" → ("has_projectile", ["for_age"])
bool splitKey(const std::string& key, std::string& name,
              std::vector<std::string>& params) {
    name.clear();
    params.clear();
    std::size_t i = 0;
    while (i < key.size() && isIdentCont(key[i])) {
        name.push_back(key[i]);
        ++i;
    }
    if (name.empty()) return false;
    while (i < key.size() && std::isspace(static_cast<unsigned char>(key[i]))) ++i;
    if (i == key.size()) return true;          // bare alias, no params
    if (key[i] != '(') return false;
    ++i;
    while (i < key.size()) {
        while (i < key.size() && std::isspace(static_cast<unsigned char>(key[i]))) ++i;
        if (i < key.size() && key[i] == ')') { ++i; break; }
        std::string p;
        while (i < key.size() && isIdentCont(key[i])) {
            p.push_back(key[i]);
            ++i;
        }
        if (p.empty()) return false;
        params.push_back(std::move(p));
        while (i < key.size() && std::isspace(static_cast<unsigned char>(key[i]))) ++i;
        if (i < key.size() && key[i] == ',') { ++i; continue; }
        if (i < key.size() && key[i] == ')') { ++i; break; }
        return false;
    }
    return true;
}

}  // namespace

AliasTable loadAliases(const std::filesystem::path& helpersJson,
                       std::ostream& errlog) {
    AliasTable out;
    std::ifstream in(helpersJson, std::ios::binary);
    if (!in) {
        throw AliasLoadError("cannot open " + helpersJson.string());
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    const std::string prepared = prepOotJson(buf.str());

    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(prepared);
    } catch (const std::exception& e) {
        throw AliasLoadError("JSON parse failed for "
                             + helpersJson.string() + ": " + e.what());
    }
    if (!doc.is_object()) {
        throw AliasLoadError(helpersJson.string() + " is not a JSON object");
    }

    for (auto it = doc.begin(); it != doc.end(); ++it) {
        if (!it.value().is_string()) continue;
        Alias a;
        std::string name;
        if (!splitKey(it.key(), name, a.params)) {
            errlog << "OoT aliases: invalid key '" << it.key() << "' — skipped\n";
            continue;
        }
        try {
            a.body = parseRule(it.value().get<std::string>());
        } catch (const std::exception& e) {
            errlog << "OoT aliases: parse failed for '" << it.key()
                   << "': " << e.what() << "\n";
            continue;
        }
        out.emplace(std::move(name), std::move(a));
    }
    return out;
}

}  // namespace tpt::game::oot::logic
