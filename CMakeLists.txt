cmake_minimum_required(VERSION 3.21)

project(tptracker
    VERSION 0.1.0
    DESCRIPTION "Twilight Princess auto-tracker (C++ port)"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8 /MP)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)
include(WebGenSubmodule)
include(OoTRSubmodule)

add_executable(tptracker
    src/main.cpp
    src/util/CrashHandler.cpp
    src/dolphin/DolphinClient.cpp
    src/dusk/DuskSource.cpp
    src/p64/Project64Source.cpp
    src/memory/PatternScan.cpp
    src/memory/SourceFactory.cpp
    src/core/Region.cpp
    src/core/Items.cpp
    src/core/QuestState.cpp
    src/core/EventFlags.cpp
    src/core/SeedHeader.cpp
    src/core/SettingsString.cpp
    src/core/CheckSaveBindings.cpp
    src/core/CheckPlacements.cpp
    src/core/Stages.cpp
    src/core/UserPrefs.cpp
    src/core/logic/Jsonc.cpp
    src/core/logic/Parser.cpp
    src/core/logic/Evaluator.cpp
    src/core/logic/WorldData.cpp
    src/core/logic/Predicates.cpp
    src/core/logic/Reach.cpp
    src/core/logic/ContextBuilder.cpp
    src/cli/Headless.cpp
    src/game/tp/TwilightPrincessGame.cpp
    src/game/oot/OcarinaOfTimeGame.cpp
    src/game/oot/Checks.cpp
    src/game/oot/save/PlayerData.cpp
    src/game/oot/save/Inventory.cpp
    src/game/oot/save/SaveFlags.cpp
    src/game/oot/save/Xflags.cpp
    src/game/oot/logic/AliasTable.cpp
    src/game/oot/logic/ContextBuilder.cpp
    src/game/oot/logic/Evaluator.cpp
    src/game/oot/logic/JsonPrep.cpp
    src/game/oot/logic/Reach.cpp
    src/game/oot/logic/RuleParser.cpp
    src/game/oot/logic/WorldGraph.cpp
    src/ui/Render.cpp
    src/ui/Style.cpp
)

target_link_libraries(tptracker PRIVATE
    SDL3::SDL3
    imgui
    nlohmann_json::nlohmann_json
    dolphin_memory_engine
)

if(WIN32)
    # DbgHelp is used by src/dusk/DuskSource.cpp and the --dusk-probe CLI
    # mode to resolve symbols against a running Dusk process when a PDB is
    # in scope (development convenience). The signature-scanner code path
    # doesn't need it; this is a no-cost link.
    target_link_libraries(tptracker PRIVATE dbghelp)
endif()

target_include_directories(tptracker PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if(WIN32)
    add_custom_command(TARGET tptracker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:tptracker> $<TARGET_FILE_DIR:tptracker>
        COMMAND_EXPAND_LISTS
    )

    set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        PROPERTY VS_STARTUP_PROJECT tptracker)
endif()

# Copy the rando's room and check JSONC tree next to the exe so the loader
# can find it as <exedir>/data/world/. Re-runs only when the source tree
# changes; we use copy_directory_if_different so it's a no-op when stable.
# Source path is provided by WebGenSubmodule.cmake (sparse submodule under vendor/webgen).
set(_world_src "${WEBGEN_WORLD_DIR}")
add_custom_command(TARGET tptracker POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${_world_src}/Rooms"  "$<TARGET_FILE_DIR:tptracker>/data/world/Rooms"
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${_world_src}/Checks" "$<TARGET_FILE_DIR:tptracker>/data/world/Checks"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/data/check_save_bindings.json"
        "$<TARGET_FILE_DIR:tptracker>/data/check_save_bindings.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/data/oot/locations.json"
        "$<TARGET_FILE_DIR:tptracker>/data/oot/locations.json"
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${OOTR_WORLD_DIR}"
        "$<TARGET_FILE_DIR:tptracker>/data/oot-world"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${OOTR_LOGIC_HELPERS}"
        "$<TARGET_FILE_DIR:tptracker>/data/oot/LogicHelpers.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/data/check_placements.json"
        "$<TARGET_FILE_DIR:tptracker>/data/check_placements.json"
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/explanations"
        "$<TARGET_FILE_DIR:tptracker>/explanations"
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/data/fonts"
        "$<TARGET_FILE_DIR:tptracker>/data/fonts"
)
