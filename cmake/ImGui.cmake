# Build Dear ImGui as a static library with the SDL3 + SDLRenderer3 backends.
# ImGui's upstream doesn't ship a CMakeLists.txt; we compose one here so the
# rest of the project can just link against the `imgui` target.

add_library(imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_demo.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp"
    # std::string-aware InputText overload (lives in misc/cpp upstream).
    "${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp"
)

target_include_directories(imgui SYSTEM PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
    "${imgui_SOURCE_DIR}/misc/cpp"
)

target_link_libraries(imgui PUBLIC SDL3::SDL3)

# ImGui itself triggers a few of MSVC's pickier warnings — keep them out of our
# build log since they aren't ours to fix.
if(MSVC)
    target_compile_options(imgui PRIVATE /W0)
else()
    target_compile_options(imgui PRIVATE -w)
endif()
