#pragma once

#include <filesystem>

#include <nlohmann/json.hpp>

namespace tpt::prefs {

// User preferences persisted to disk. The file is a single JSON object
// game-namespaced by GameModule::id() — e.g.
//   {
//     "tp":  { "settingsString": "...", "progressionRupeesOnly": false },
//     "oot": { ... }
//   }
// Each game module owns the contents of its own sub-object via
// GameModule::loadPrefs / savePrefs. Inactive games' blobs are preserved
// untouched on save so switching between games doesn't lose their state.

// Platform-appropriate user-data directory for the app. Created lazily
// by saveJson(). Resolved per-call (cheap) so an env-var change between
// calls is honored without an app restart.
//   Windows: %APPDATA%\tptracker
//   macOS:   $HOME/Library/Application Support/tptracker
//   Linux:   $XDG_CONFIG_HOME/tptracker  (fallback $HOME/.config/tptracker)
// Falls back to the current working directory if the relevant env var is
// unset.
std::filesystem::path userPrefsDir();

// `<userPrefsDir()>/preferences.json`.
std::filesystem::path userPrefsPath();

// Read the prefs file. Returns an empty JSON object on missing file,
// parse error, or filesystem error — never throws. Auto-migrates the
// legacy flat format (top-level "settingsString" / "progressionRupeesOnly"
// with no game id) by wrapping it under "tp".
nlohmann::json loadJson();

// Write the prefs file atomically (temp + rename). Silently no-ops on
// filesystem errors so the UI never has to handle a save failure.
void saveJson(const nlohmann::json& root);

}  // namespace tpt::prefs
