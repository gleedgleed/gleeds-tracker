#include "ui/Style.h"

#include <imgui.h>

#include <array>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

namespace tpt::ui::style {

namespace {

// Loaded fonts, indexed by FontChoice. nullptr = not available.
std::array<ImFont*, static_cast<int>(FontChoice::Count)> g_fonts{};

std::filesystem::path exeDir() {
#ifdef _WIN32
    char buf[260] = {};
    if (GetModuleFileNameA(nullptr, buf, sizeof(buf)) > 0) {
        return std::filesystem::path(buf).parent_path();
    }
#endif
    return std::filesystem::current_path();
}

void applyImGuiDefaultTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowPadding = ImVec2(8, 8);
    s.FramePadding  = ImVec2(4, 3);
    s.ItemSpacing   = ImVec2(8, 4);
    s.WindowRounding = 0.0f;
    s.FrameRounding  = 0.0f;
    s.TabRounding    = 0.0f;
    s.ScrollbarRounding = 0.0f;
    s.GrabRounding   = 0.0f;
}

// Muted dark with a sage/teal accent. Less aggressively blue than the
// ImGui default, slightly roomier spacing.
void applyMutedDarkTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowPadding   = ImVec2(10, 8);
    s.FramePadding    = ImVec2(6, 4);
    s.ItemSpacing     = ImVec2(8, 5);
    s.WindowRounding  = 4.0f;
    s.FrameRounding   = 3.0f;
    s.TabRounding     = 3.0f;
    s.ScrollbarRounding = 4.0f;
    s.GrabRounding    = 3.0f;
    s.WindowBorderSize = 1.0f;
    s.FrameBorderSize  = 0.0f;

    ImVec4* c = s.Colors;
    // Surface tones — warm-leaning charcoals.
    c[ImGuiCol_WindowBg]            = ImVec4(0.13f, 0.13f, 0.14f, 1.00f);
    c[ImGuiCol_ChildBg]             = ImVec4(0.11f, 0.11f, 0.12f, 1.00f);
    c[ImGuiCol_PopupBg]             = ImVec4(0.10f, 0.10f, 0.11f, 0.96f);
    c[ImGuiCol_Border]              = ImVec4(0.27f, 0.30f, 0.32f, 0.55f);
    c[ImGuiCol_BorderShadow]        = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    c[ImGuiCol_FrameBg]             = ImVec4(0.18f, 0.20f, 0.22f, 0.85f);
    c[ImGuiCol_FrameBgHovered]      = ImVec4(0.30f, 0.40f, 0.40f, 0.55f);
    c[ImGuiCol_FrameBgActive]       = ImVec4(0.35f, 0.50f, 0.50f, 0.65f);

    c[ImGuiCol_TitleBg]             = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
    c[ImGuiCol_TitleBgActive]       = ImVec4(0.15f, 0.18f, 0.18f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]    = ImVec4(0.08f, 0.08f, 0.09f, 0.75f);

    c[ImGuiCol_MenuBarBg]           = ImVec4(0.12f, 0.13f, 0.13f, 1.00f);
    c[ImGuiCol_ScrollbarBg]         = ImVec4(0.05f, 0.05f, 0.06f, 0.55f);
    c[ImGuiCol_ScrollbarGrab]       = ImVec4(0.30f, 0.32f, 0.33f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered]= ImVec4(0.40f, 0.45f, 0.45f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.50f, 0.60f, 0.55f, 1.00f);

    // Sage/teal accent (replaces ImGui's bright blue).
    const ImVec4 accent       = ImVec4(0.42f, 0.66f, 0.58f, 1.00f);
    const ImVec4 accentHover  = ImVec4(0.52f, 0.78f, 0.68f, 1.00f);
    const ImVec4 accentActive = ImVec4(0.32f, 0.55f, 0.48f, 1.00f);

    c[ImGuiCol_CheckMark]      = accent;
    c[ImGuiCol_SliderGrab]     = accent;
    c[ImGuiCol_SliderGrabActive] = accentActive;
    c[ImGuiCol_Button]         = ImVec4(0.22f, 0.27f, 0.27f, 0.85f);
    c[ImGuiCol_ButtonHovered]  = accentHover;
    c[ImGuiCol_ButtonActive]   = accentActive;
    c[ImGuiCol_Header]         = ImVec4(0.22f, 0.30f, 0.28f, 0.55f);
    c[ImGuiCol_HeaderHovered]  = ImVec4(accentHover.x, accentHover.y, accentHover.z, 0.55f);
    c[ImGuiCol_HeaderActive]   = ImVec4(accentActive.x, accentActive.y, accentActive.z, 0.85f);
    c[ImGuiCol_Separator]      = ImVec4(0.27f, 0.30f, 0.32f, 0.50f);
    c[ImGuiCol_SeparatorHovered]= accentHover;
    c[ImGuiCol_SeparatorActive] = accentActive;
    c[ImGuiCol_ResizeGrip]     = ImVec4(0.22f, 0.30f, 0.28f, 0.40f);
    c[ImGuiCol_ResizeGripHovered]= accentHover;
    c[ImGuiCol_ResizeGripActive] = accentActive;

    c[ImGuiCol_Tab]            = ImVec4(0.16f, 0.18f, 0.18f, 0.85f);
    c[ImGuiCol_TabHovered]     = accentHover;
    c[ImGuiCol_TabActive]      = ImVec4(0.25f, 0.34f, 0.32f, 1.00f);
    c[ImGuiCol_TabUnfocused]   = ImVec4(0.13f, 0.14f, 0.14f, 0.85f);
    c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.20f, 0.24f, 0.23f, 1.00f);

    c[ImGuiCol_PlotLines]      = accent;
    c[ImGuiCol_PlotHistogram]  = accent;
    c[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.45f);
    c[ImGuiCol_NavHighlight]   = accent;

    c[ImGuiCol_TableHeaderBg]      = ImVec4(0.16f, 0.18f, 0.18f, 1.00f);
    c[ImGuiCol_TableBorderStrong]  = ImVec4(0.27f, 0.30f, 0.32f, 1.00f);
    c[ImGuiCol_TableBorderLight]   = ImVec4(0.22f, 0.24f, 0.26f, 1.00f);
    c[ImGuiCol_TableRowBg]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_TableRowBgAlt]      = ImVec4(1.00f, 1.00f, 1.00f, 0.04f);

    c[ImGuiCol_Text]               = ImVec4(0.92f, 0.92f, 0.92f, 1.00f);
    c[ImGuiCol_TextDisabled]       = ImVec4(0.55f, 0.57f, 0.58f, 1.00f);
}

}  // namespace

const char* themeName(Theme t) {
    switch (t) {
        case Theme::ImGuiDefault: return "ImGui Default";
        case Theme::MutedDark:    return "Muted Dark";
        default:                  return "?";
    }
}

const char* fontName(FontChoice f) {
    switch (f) {
        case FontChoice::ImGuiDefault: return "ImGui Default";
        case FontChoice::Inter:        return "Inter";
        default:                       return "?";
    }
}

void applyTheme(Theme t) {
    switch (t) {
        case Theme::ImGuiDefault: applyImGuiDefaultTheme(); break;
        case Theme::MutedDark:    applyMutedDarkTheme();    break;
        default:                  applyImGuiDefaultTheme(); break;
    }
}

bool loadFonts(float fontSizePx) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    // Index 0: built-in default. Stays available so we can toggle back.
    g_fonts[static_cast<int>(FontChoice::ImGuiDefault)] = io.Fonts->AddFontDefault();

    const auto interPath = exeDir() / "data" / "fonts" / "Inter-Regular.ttf";
    g_fonts[static_cast<int>(FontChoice::Inter)] = nullptr;
    if (std::filesystem::exists(interPath)) {
        ImFontConfig cfg;
        cfg.OversampleH = 3;
        cfg.OversampleV = 1;
        cfg.PixelSnapH  = false;
        g_fonts[static_cast<int>(FontChoice::Inter)] =
            io.Fonts->AddFontFromFileTTF(interPath.string().c_str(), fontSizePx, &cfg);
    }
    io.Fonts->Build();
    return g_fonts[static_cast<int>(FontChoice::Inter)] != nullptr;
}

void selectFont(FontChoice f) {
    auto* font = g_fonts[static_cast<int>(f)];
    if (!font) return;
    ImGui::GetIO().FontDefault = font;
}

bool rebuildFontsAtSize(float fontSizePx) {
    return loadFonts(fontSizePx);
}

void renderControls(Settings& s, bool& /*fontReloadNeeded*/) {
    // Theme and font selection are configured via constants in main.cpp now
    // (style::Settings initializers). Only font size is exposed at runtime.
    // Modern ImGui (1.92+) dynamically bakes glyphs at any requested size,
    // so we just modify FontSizeBase directly — no atlas rebuild needed.
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::SliderFloat("##FontSize", &s.fontSize, 8.0f, 32.0f, "%.0f px")) {
        // Both fields are required: FontSizeBase is the persistent value,
        // _NextFrameFontSizeBase is the one NewFrame() actually reads. ImGui's
        // own demo mirrors this; see imgui.cpp's "FontSizeBase" DragFloat.
        ImGui::GetStyle().FontSizeBase           = s.fontSize;
        ImGui::GetStyle()._NextFrameFontSizeBase = s.fontSize;
    }
}

}  // namespace tpt::ui::style
