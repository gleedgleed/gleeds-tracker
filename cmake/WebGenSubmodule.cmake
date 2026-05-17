# Bootstrap the Randomizer-Web-Generator submodule with sparse-checkout.
# The tracker only consumes Generator/World/{Rooms,Checks}/**.jsonc, so we
# trim the working tree to just those two paths. The .git object database is
# still fetched in full, but disk and clone-traversal cost stay small.
#
# This runs at configure time. It is idempotent: once the marker file exists,
# all git invocations are skipped.

set(_webgen_path "submodules/webgen")
set(_webgen_dir  "${CMAKE_SOURCE_DIR}/${_webgen_path}")
set(_webgen_world "${_webgen_dir}/Generator/World")
set(_webgen_marker "${_webgen_world}/Rooms")

if(NOT EXISTS "${_webgen_marker}")
    find_package(Git REQUIRED)

    # If the submodule has never been cloned on this machine, init it without
    # populating the working tree — we want sparse rules in place first.
    if(NOT EXISTS "${_webgen_dir}/.git")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} submodule update --init --no-checkout ${_webgen_path}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMAND_ERROR_IS_FATAL ANY)
    endif()

    # Configure sparse-checkout. Both commands are idempotent — re-running with
    # the same pattern set is a no-op.
    execute_process(
        COMMAND ${GIT_EXECUTABLE} sparse-checkout init --cone
        WORKING_DIRECTORY ${_webgen_dir}
        COMMAND_ERROR_IS_FATAL ANY)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} sparse-checkout set
                Generator/World/Rooms Generator/World/Checks
        WORKING_DIRECTORY ${_webgen_dir}
        COMMAND_ERROR_IS_FATAL ANY)

    # Materialize the pinned commit into the (now sparse) working tree.
    execute_process(
        COMMAND ${GIT_EXECUTABLE} submodule update ${_webgen_path}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMAND_ERROR_IS_FATAL ANY)
endif()

if(NOT EXISTS "${_webgen_marker}")
    message(FATAL_ERROR
        "Web generator world graph missing at ${_webgen_marker}.\n"
        "Recover manually:\n"
        "  git submodule update --init ${_webgen_path}\n"
        "  cd ${_webgen_dir}\n"
        "  git sparse-checkout set Generator/World/Rooms Generator/World/Checks")
endif()

set(WEBGEN_WORLD_DIR "${_webgen_world}" CACHE INTERNAL "Sparse webgen World tree")
