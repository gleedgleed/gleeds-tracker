include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# SDL3 — window/input/GL/renderer
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_SHARED ON  CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)
set(SDL_TESTS  OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.2.0
    GIT_SHALLOW    TRUE
)

# Dear ImGui — widgets. Docking branch for dockable panes (we'll use it for the 3-column UI).
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
    GIT_SHALLOW    TRUE
)

# nlohmann/json — JSON / JSONC parsing for room and check data files.
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(SDL3 nlohmann_json)

# ImGui ships no CMakeLists.txt — download only, then build our own target.
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

include(ImGui)

# dolphin-memory-engine — the upstream repo builds a Qt GUI; we want only the
# IPC core. Download here, then build a stripped-down static library in
# DolphinMemoryEngine.cmake.
FetchContent_Declare(
    dolphin_memory_engine
    GIT_REPOSITORY https://github.com/aldelaro5/dolphin-memory-engine.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
    # Skip the Qt submodule — upstream uses it for the GUI app, we only want IPC.
    GIT_SUBMODULES ""
    GIT_SUBMODULES_RECURSE FALSE
)

FetchContent_GetProperties(dolphin_memory_engine)
if(NOT dolphin_memory_engine_POPULATED)
    FetchContent_Populate(dolphin_memory_engine)
endif()

include(DolphinMemoryEngine)
