# Custom FindSDL3 module for RPCS3 to handle static vs system SDL3 config
# and avoid CMake 4.x inclusion errors with build-tree export files.

if(NOT USE_SYSTEM_SDL)
    set(SDL3_FOUND TRUE)
    set(SDL3_VERSION "3.4.8")
    if(NOT TARGET SDL3::SDL3 AND TARGET SDL3-static)
        add_library(SDL3::SDL3 ALIAS SDL3-static)
    endif()
else()
    # In system mode, find the config package
    find_package(SDL3 CONFIG QUIET)
    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(SDL3
        REQUIRED_VARS SDL3_FOUND
        VERSION_VAR SDL3_VERSION
    )
endif()
