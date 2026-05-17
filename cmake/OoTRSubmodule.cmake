# Bootstrap the OoT-Randomizer (Dev) submodule with sparse-checkout.
# The tracker only consumes data/World/**.json and data/LogicHelpers.json from
# the upstream repo, so we trim the working tree to the `data/World` cone. In
# cone mode this automatically includes files at `data/*` (covering
# LogicHelpers.json), so a single entry covers both inputs.
#
# This runs at configure time. It is idempotent: once the marker file exists,
# all git invocations are skipped.

set(_ootr_path "submodules/ootr")
set(_ootr_dir  "${CMAKE_SOURCE_DIR}/${_ootr_path}")
set(_ootr_data "${_ootr_dir}/data")
set(_ootr_marker "${_ootr_data}/World")

if(NOT EXISTS "${_ootr_marker}")
    find_package(Git REQUIRED)

    if(NOT EXISTS "${_ootr_dir}/.git")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} submodule update --init --no-checkout ${_ootr_path}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMAND_ERROR_IS_FATAL ANY)
    endif()

    execute_process(
        COMMAND ${GIT_EXECUTABLE} sparse-checkout init --cone
        WORKING_DIRECTORY ${_ootr_dir}
        COMMAND_ERROR_IS_FATAL ANY)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} sparse-checkout set data/World
        WORKING_DIRECTORY ${_ootr_dir}
        COMMAND_ERROR_IS_FATAL ANY)

    execute_process(
        COMMAND ${GIT_EXECUTABLE} submodule update ${_ootr_path}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMAND_ERROR_IS_FATAL ANY)
endif()

if(NOT EXISTS "${_ootr_marker}")
    message(FATAL_ERROR
        "OoT-Randomizer world data missing at ${_ootr_marker}.\n"
        "Recover manually:\n"
        "  git submodule update --init ${_ootr_path}\n"
        "  cd ${_ootr_dir}\n"
        "  git sparse-checkout set data/World")
endif()

set(OOTR_WORLD_DIR     "${_ootr_data}/World"             CACHE INTERNAL "Sparse OoTR world tree")
set(OOTR_LOGIC_HELPERS "${_ootr_data}/LogicHelpers.json" CACHE INTERNAL "OoTR logic helpers file")
