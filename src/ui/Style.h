#pragma once

namespace tpt::ui::style {

// Toggleable themes. Add new entries here + a case in applyTheme().
enum class Theme : int {
    ImGuiDefault = 0,
    MutedDark    = 1,
    Count
};

// Toggleable fonts. ImGuiDefault is the built-in ProggyClean (small pixel
// font); Inter is the bundled TTF.
enum class FontChoice : int {
    ImGuiDefault = 0,
    Inter        = 1,
    Count
};

const char* themeName(Theme t);
const char* fontName(FontChoice f);

// Apply a theme's color palette + style metrics. Cheap; safe to call every
// frame, but typically only called on toggle.
void applyTheme(Theme t);

// Load all bundled fonts (call once during startup, AFTER backend init but
// BEFORE the first NewFrame). Returns true if Inter loaded successfully.
bool loadFonts(float fontSizePx);

// Switch the active font. No-op if the requested font wasn't loaded.
void selectFont(FontChoice f);

// Rebuild the font atlas with a different size. Call between frames.
// Returns true on success. After this you must also call selectFont() again.
bool rebuildFontsAtSize(float fontSizePx);

// ImGui combo + slider widget for runtime style switching. Mutates the
// passed-in state and applies changes immediately. Mark `fontReloadNeeded`
// to true if the size changed (caller rebuilds before next frame).
struct Settings {
    Theme      theme    = Theme::MutedDark;
    FontChoice font     = FontChoice::Inter;
    float      fontSize = 16.0f;
};
void renderControls(Settings& s, bool& fontReloadNeeded);

}  // namespace tpt::ui::style
