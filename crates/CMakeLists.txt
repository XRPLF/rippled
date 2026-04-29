set(CORROSION_VERSION 0.6.1)

find_package(Corrosion ${CORROSION_VERSION} QUIET)
if(NOT Corrosion_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        Corrosion
        GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
        GIT_TAG v${CORROSION_VERSION}
    )
    FetchContent_MakeAvailable(Corrosion)
endif()

corrosion_import_crate(MANIFEST_PATH ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml)

# Umbrella target that aggregates all crate-generated code (cxxbridge headers,
# etc.). Build this before running clang-tidy so generated headers are present.
add_custom_target(xrpl_crates)

# add_xrpl_crate(<name> CRATE <crate> FILES <file>...) Creates a cxxbridge
# target <name>_cxxbridge and registers it with xrpl_crates.
function(add_xrpl_crate name)
    cmake_parse_arguments(ARG "" "CRATE" "FILES" ${ARGN})
    corrosion_add_cxxbridge(${name}_cxxbridge CRATE ${ARG_CRATE} FILES
                            ${ARG_FILES}
    )
    # CMake 4.x validates INTERFACE_SOURCES at link time, but generated headers
    # don't exist at configure time. Clearing skips the check while build-time
    # ordering is still enforced by the custom commands inside the target.
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
        set_target_properties(${name}_cxxbridge PROPERTIES INTERFACE_SOURCES "")
    endif()
    add_dependencies(xrpl_crates ${name}_cxxbridge)
endfunction()

add_xrpl_crate(rs_hello_world CRATE rs_hello_world FILES lib.rs)
