# Build a stripped-down static library from aldelaro5/dolphin-memory-engine.
# The upstream repo's CMakeLists builds a Qt6 GUI app; we want only the IPC
# core (no Qt, no MemoryScanner, no MemoryWatch, no GUI) so we hand-pick the
# sources that the read/write/hook API depends on.

set(_dme_src "${dolphin_memory_engine_SOURCE_DIR}/Source")

set(_dme_sources
    "${_dme_src}/Common/MemoryCommon.cpp"
    "${_dme_src}/Common/PPC/PowerPCAssembler.cpp"
    "${_dme_src}/Common/PPC/PowerPCDisassembler.cpp"
    "${_dme_src}/DolphinProcess/DolphinAccessor.cpp"
)

if(WIN32)
    list(APPEND _dme_sources
        "${_dme_src}/DolphinProcess/Windows/WindowsDolphinProcess.cpp")
elseif(APPLE)
    list(APPEND _dme_sources
        "${_dme_src}/DolphinProcess/Mac/MacDolphinProcess.cpp")
else()
    list(APPEND _dme_sources
        "${_dme_src}/DolphinProcess/Linux/LinuxDolphinProcess.cpp")
endif()

add_library(dolphin_memory_engine STATIC ${_dme_sources})

target_include_directories(dolphin_memory_engine SYSTEM PUBLIC
    "${_dme_src}"
)

if(WIN32)
    # WindowsDolphinProcess.cpp uses Psapi (EnumProcessModules, ...).
    target_link_libraries(dolphin_memory_engine PUBLIC Psapi)
endif()

# Suppress upstream warnings — not ours to fix.
if(MSVC)
    target_compile_options(dolphin_memory_engine PRIVATE /W0)
else()
    target_compile_options(dolphin_memory_engine PRIVATE -w)
endif()

unset(_dme_src)
unset(_dme_sources)
