#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <cstdio>
#include <iostream>

#include <nlohmann/json.hpp>

#include "cli/Headless.h"
#include "core/UserPrefs.h"
#include "game/oot/OcarinaOfTimeGame.h"
#include "game/tp/TwilightPrincessGame.h"
#include "memory/SourceFactory.h"
#include "p64/Project64Source.h"
#include "ui/Render.h"
#include "ui/Style.h"
#include "ui/UIState.h"
#include "util/CrashHandler.h"

namespace {

// Pick the active game based on which emulator process is currently running.
// Project64 → Ocarina of Time; everything else → Twilight Princess. This is
// the "Option B" detection in doc/game-module-handoff.md §5 — one emulator
// per game, no separate game-selector UI. Cheap: a process-list snapshot.
//
// `state` is borrowed for the game's lifetime. Both modules write the
// status-bar fields (sourceName / emulatorHooked / saveLoaded / gameId)
// during poll() while the State split is still pending (see handoff §3).
std::unique_ptr<tpt::game::GameModule> pickActiveGame(tpt::ui::State& state) {
    if (tpt::p64::Source::isAvailable()) {
        return std::make_unique<tpt::game::oot::OcarinaOfTimeGame>(state);
    }
    return std::make_unique<tpt::game::tp::TwilightPrincessGame>(state);
}

// Wipe game-typed fields out of `state` while preserving UI-shell prefs the
// user expects to outlive a game swap (selected check, autoHook, glitched
// flag). Needed because State is still a transitional shared bag: the UI
// shell reads game-typed fields like `allByStage` directly, so leaving them
// populated with stale TP data after swapping to OoT (or vice versa) would
// cause the wrong checks to render. When the State split lands (handoff §3
// /§7) this becomes a no-op.
void resetGameSpecificState(tpt::ui::State& state) {
    std::string selectedCheck = std::move(state.selectedCheck);
    const bool autoHook = state.autoHook;
    const bool glitched = state.glitched;
    state = tpt::ui::State{};
    state.selectedCheck = std::move(selectedCheck);
    state.autoHook = autoHook;
    state.glitched = glitched;
}

}  // namespace

int main(int argc, char** argv) {
    tpt::util::installCrashHandler();
    const auto opts = tpt::cli::parseArgs(argc, argv);
    if (!opts.parseError.empty()) {
        std::fprintf(stderr, "error: %s\n\n", opts.parseError.c_str());
        tpt::cli::printUsage(argv[0]);
        return 1;
    }
    if (opts.mode != tpt::cli::Mode::Gui) {
        return tpt::cli::runHeadless(opts);
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // Default window size tuned so the content fits at the DPI-scaled font.
    // Capped to the display's logical bounds with a small margin so the
    // window doesn't end up off-screen on a small monitor.
    int wantW = 1700, wantH = 1000;
    if (SDL_DisplayID primary = SDL_GetPrimaryDisplay(); primary) {
        SDL_Rect bounds{};
        if (SDL_GetDisplayUsableBounds(primary, &bounds)) {
            wantW = std::min(wantW, bounds.w - 60);
            wantH = std::min(wantH, bounds.h - 80);
        }
    }
    SDL_Window* window = SDL_CreateWindow(
        "TP Tracker", wantW, wantH,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    // Default theme + font. Tweak these constants to swap themes/fonts without
    // recompiling the UI; only font size is user-toggleable at runtime.
    tpt::ui::style::Settings styleSettings{};   // defaults: MutedDark + Inter
    styleSettings.fontSize = 16.0f;              // logical pixels (pre-DPI)

    // ImGui 1.92+ dynamically bakes glyphs at any requested size. Load Inter
    // once and let FontSizeBase / FontScaleDpi do the runtime sizing.
    tpt::ui::style::applyTheme(styleSettings.theme);
    tpt::ui::style::loadFonts(styleSettings.fontSize);
    tpt::ui::style::selectFont(styleSettings.font);

    // DPI scaling. SDL_GetWindowDisplayScale returns the user's content scale
    // (1.0 on 1080p @ 100%, 2.0 on 4K @ 200%, etc.). Apply to FontScaleDpi so
    // the rendered glyph size = FontSizeBase * displayScale, and to spacing
    // so widgets stay proportional.
    float displayScale = SDL_GetWindowDisplayScale(window);
    if (!(displayScale > 0.0f)) displayScale = 1.0f;
    ImGui::GetStyle().FontSizeBase           = styleSettings.fontSize;
    ImGui::GetStyle()._NextFrameFontSizeBase = styleSettings.fontSize;
    ImGui::GetStyle().FontScaleDpi = displayScale;
    ImGui::GetStyle().ScaleAllSizes(displayScale);

    bool fontReloadNeeded = false;  // unused with dynamic baking, kept for API
    bool autoSizedOnce    = false;  // true after we resize the window to fit content

    // Game module + memory source — chosen by which emulator is running.
    // Project64 ⇒ Ocarina of Time; Dolphin/Dusk (or nothing) ⇒ Twilight
    // Princess. Re-evaluated below whenever the current source is
    // disconnected, so launching Project64 mid-session (or closing it and
    // opening Dolphin) live-swaps the active game without a restart.
    tpt::ui::State state;
    state.glitched = opts.glitched;

    auto game   = pickActiveGame(state);
    auto memSrc = game->defaultSource();

    // Persistent user prefs — game-namespaced. The root JSON contains a
    // sub-object per game id ({"tp": {...}, "oot": {...}}); the active
    // module owns its sub-object via loadPrefs/savePrefs. Inactive games'
    // blobs round-trip untouched so switching games preserves their state.
    // CLI args win over saved values, so `--settings=...` always overrides.
    nlohmann::json prefsRoot = tpt::prefs::loadJson();
    game->loadPrefs(prefsRoot.value(game->id(), nlohmann::json::object()));
    if (!opts.settingsString.empty()) state.settingsString = opts.settingsString;
    // Reflect the (possibly CLI-overridden) state back into the root so
    // the on-disk file converges. Compared against future ticks to skip
    // redundant writes.
    prefsRoot[game->id()] = game->savePrefs();
    nlohmann::json lastSavedPrefsRoot = prefsRoot;

    if (!game->loadWorldData(std::cerr)) {
        std::fprintf(stderr, "warning: %s\n", state.error.c_str());
    }

    constexpr Uint64 kPollIntervalMs = 500;
    constexpr Uint64 kHookRetryIntervalMs = 1000;
    // Cheap diff against the last-saved snapshot; written only on change so
    // typing in the settings field doesn't churn the disk every frame.
    constexpr Uint64 kPrefsCheckIntervalMs = 1000;
    Uint64 lastHookAttempt = 0;
    Uint64 lastPoll = 0;
    Uint64 lastPrefsCheck = 0;

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(window)) {
                running = false;
            }
        }

        const Uint64 now = SDL_GetTicks();
        if (state.autoHook && memSrc && !memSrc->isConnected() &&
            now - lastHookAttempt > kHookRetryIntervalMs) {
            // Re-evaluate the source on every retry tick. Cheap
            // (process-list snapshot). Two things to check:
            //   1. Did the *game family* change? — e.g. user closed
            //      Dolphin and launched Project64, or vice versa. We
            //      swap the GameModule, flush prefs for the outgoing
            //      game, reset shared State, then reload prefs for the
            //      incoming game.
            //   2. Did the source within the same family change? — e.g.
            //      Dolphin → Dusk (both TP). The game module stays;
            //      only the source is replaced.
            const bool wantOoT = tpt::p64::Source::isAvailable();
            const bool isOoT   = game->id() == "oot";
            if (wantOoT != isOoT) {
                // Flush outgoing game's prefs synchronously so the swap
                // doesn't lose anything the user just typed.
                prefsRoot[game->id()] = game->savePrefs();
                if (prefsRoot != lastSavedPrefsRoot) {
                    tpt::prefs::saveJson(prefsRoot);
                    lastSavedPrefsRoot = prefsRoot;
                }
                resetGameSpecificState(state);
                game = pickActiveGame(state);
                game->loadPrefs(prefsRoot.value(game->id(), nlohmann::json::object()));
                if (!game->loadWorldData(std::cerr)) {
                    std::fprintf(stderr, "warning: %s\n", state.error.c_str());
                }
                // Force a re-size pass for the new game's content widths.
                autoSizedOnce = false;
            }
            memSrc = game->defaultSource();
            if (memSrc) memSrc->connect();
            lastHookAttempt = now;
        }
        if (memSrc && now - lastPoll > kPollIntervalMs) {
            game->poll(*memSrc);
            lastPoll = now;
        }
        if (now - lastPrefsCheck > kPrefsCheckIntervalMs) {
            nlohmann::json current = lastSavedPrefsRoot;
            current[game->id()] = game->savePrefs();
            if (current != lastSavedPrefsRoot) {
                tpt::prefs::saveJson(current);
                lastSavedPrefsRoot = std::move(current);
            }
            lastPrefsCheck = now;
        }

        // Font size changes apply directly via ImGuiStyle::FontSizeBase in
        // the slider callback (dynamic baking handles the rest).

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // Once the world data is loaded, auto-size the window to fit a
        // collapsed All-Checks tree plus inventory at the active font scale.
        // Runs once. Capped to the display's usable bounds.
        if (!autoSizedOnce && state.worldLoaded && !state.masterByStage.empty()) {
            // Drive width by the longest *check* name (with the [x] prefix),
            // since that's what's visible when the user expands a stage.
            // Also consider the worst-case inventory equipment line so the
            // right pane doesn't overflow even with all max-tier items.
            float maxCheckW = 0.0f;
            for (const auto& [stage, entries] : state.allByStage) {
                for (const auto& e : entries) {
                    const auto sz = ImGui::CalcTextSize(("[x] " + e.name).c_str());
                    maxCheckW = std::max(maxCheckW, sz.x);
                }
            }
            constexpr const char* kWorstInv =
                "Sword: Master Sword Light   Bow: Giant Quiver   Clawshot: Double Clawshot";
            const float invW = ImGui::CalcTextSize(kWorstInv).x;
            const float contentW = std::max(maxCheckW, invW);

            const float lineH       = ImGui::GetTextLineHeightWithSpacing();
            const float treeIndent  = 28.0f;   // tree node arrow + indent
            const float scrollbar   = 20.0f;
            const float colPadding  = 16.0f;
            const float colWidth    = contentW + treeIndent + scrollbar + colPadding;
            // 3 columns + table borders + window padding.
            const int desiredW = static_cast<int>(colWidth * 3.0f + 40.0f);
            // Height: status bar + tab bar + N collapsed tree headers + chrome.
            const int desiredH = static_cast<int>(
                lineH * (state.masterByStage.size() + 6) + 80.0f);

            int useW = desiredW, useH = desiredH;
            if (SDL_DisplayID p = SDL_GetDisplayForWindow(window); p) {
                SDL_Rect bounds{};
                if (SDL_GetDisplayUsableBounds(p, &bounds)) {
                    useW = std::min(useW, bounds.w - 60);
                    useH = std::min(useH, bounds.h - 80);
                }
            }
            SDL_SetWindowSize(window, useW, useH);
            autoSizedOnce = true;
        }

        tpt::ui::render::mainLayout(state, *game, styleSettings, fontReloadNeeded);

        ImGui::Render();
        SDL_SetRenderDrawColorFloat(renderer, 0.10f, 0.10f, 0.12f, 1.0f);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    // Final flush — captures any edit made in the last sub-second window
    // before the user closed the window. Cheap (<1ms for our payload).
    {
        nlohmann::json current = lastSavedPrefsRoot;
        current[game->id()] = game->savePrefs();
        if (current != lastSavedPrefsRoot) tpt::prefs::saveJson(current);
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
