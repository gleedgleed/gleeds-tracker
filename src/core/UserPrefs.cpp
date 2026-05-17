#include "core/UserPrefs.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

namespace tpt::prefs {

namespace {

constexpr const char* kAppDirName  = "tptracker";
constexpr const char* kFileName    = "preferences.json";

std::string envOr(const char* name) {
    if (const char* v = std::getenv(name); v && *v) return v;
    return {};
}

// Detect the pre-game-namespaced format (flat top-level TP keys) and
// wrap it under "tp" so existing user installs migrate transparently.
nlohmann::json migrateLegacyFormat(nlohmann::json j) {
    if (!j.is_object()) return nlohmann::json::object();
    const bool hasLegacy =
        j.contains("settingsString") || j.contains("progressionRupeesOnly");
    const bool hasGameNs = j.contains("tp") || j.contains("oot");
    if (!hasLegacy || hasGameNs) return j;

    nlohmann::json migrated = nlohmann::json::object();
    migrated["tp"] = std::move(j);
    return migrated;
}

}  // namespace

std::filesystem::path userPrefsDir() {
    namespace fs = std::filesystem;
#if defined(_WIN32)
    if (auto v = envOr("APPDATA"); !v.empty()) {
        return fs::path(v) / kAppDirName;
    }
#elif defined(__APPLE__)
    if (auto v = envOr("HOME"); !v.empty()) {
        return fs::path(v) / "Library" / "Application Support" / kAppDirName;
    }
#else
    if (auto v = envOr("XDG_CONFIG_HOME"); !v.empty()) {
        return fs::path(v) / kAppDirName;
    }
    if (auto v = envOr("HOME"); !v.empty()) {
        return fs::path(v) / ".config" / kAppDirName;
    }
#endif
    std::error_code ec;
    auto cwd = fs::current_path(ec);
    return ec ? fs::path{".", fs::path::generic_format} : cwd;
}

std::filesystem::path userPrefsPath() {
    return userPrefsDir() / kFileName;
}

nlohmann::json loadJson() {
    std::ifstream in(userPrefsPath(), std::ios::binary);
    if (!in) return nlohmann::json::object();
    try {
        auto j = nlohmann::json::parse(
            in, /*cb*/ nullptr, /*allow_exceptions*/ true,
            /*ignore_comments*/ true);
        return migrateLegacyFormat(std::move(j));
    } catch (const std::exception&) {
        return nlohmann::json::object();
    }
}

void saveJson(const nlohmann::json& root) {
    namespace fs = std::filesystem;
    const auto dir = userPrefsDir();

    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return;

    const auto finalPath = dir / kFileName;
    const auto tempPath  = dir / (std::string(kFileName) + ".tmp");
    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out << root.dump(2);
        if (!out) return;
    }
    fs::rename(tempPath, finalPath, ec);
    if (ec) {
        fs::remove(finalPath, ec);
        fs::rename(tempPath, finalPath, ec);
    }
}

}  // namespace tpt::prefs
