set(CORROSION_VERSION 0.6.1)
# SHA256 of the release tarball referenced below. Refresh together with
# CORROSION_VERSION:
#   curl -sL https://github.com/corrosion-rs/corrosion/archive/refs/tags/v<ver>.tar.gz | shasum -a 256
set(CORROSION_SHA256
    e9e95b1ee2bad52681f347993fb1a5af5cce458c5ce8a2636c9e476e4babf8e3
)

find_package(Corrosion ${CORROSION_VERSION} QUIET)

if(NOT Corrosion_FOUND)
    if(NOT WIN32)
        message(
            FATAL_ERROR
            "Corrosion ${CORROSION_VERSION} is required to build the Rust crates "
            "but was not found. Install it from your package manager or from "
            "https://github.com/corrosion-rs/corrosion (v${CORROSION_VERSION}), "
            "then re-run CMake. Only Windows builds may download it automatically."
        )
    endif()

    include(FetchContent)
    FetchContent_Declare(
        Corrosion
        URL
            https://github.com/corrosion-rs/corrosion/archive/refs/tags/v${CORROSION_VERSION}.tar.gz
        URL_HASH SHA256=${CORROSION_SHA256}
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    )
    FetchContent_MakeAvailable(Corrosion)
endif()

corrosion_import_crate(MANIFEST_PATH ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml)

# The generated C++ lands in the build tree, so put a .clang-tidy next to it to
# keep clang-tidy from analyzing code we don't own.
configure_file(
    generated.clang-tidy
    "${CMAKE_CURRENT_BINARY_DIR}/.clang-tidy"
    COPYONLY
)

add_custom_target(xrpl_crates)

function(add_xrpl_crate name)
    cmake_parse_arguments(ARG "" "CRATE" "FILES" ${ARGN})
    corrosion_add_cxxbridge(${name}_cxxbridge CRATE ${ARG_CRATE} FILES
                            ${ARG_FILES}
    )
    # Generated cxxbridge headers don't exist at configure time; CMake 3.28+
    # validates INTERFACE_SOURCES on consuming targets. Clear it to skip the
    # existence check — build-time ordering is enforced by the custom commands.
    set_target_properties(${name}_cxxbridge PROPERTIES INTERFACE_SOURCES "")
    add_dependencies(xrpl_crates ${name}_cxxbridge)
endfunction()

add_xrpl_crate(rs_hello_world CRATE rs_hello_world FILES lib.rs)
