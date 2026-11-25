find_package(Corrosion REQUIRED)

corrosion_import_crate(MANIFEST_PATH ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml)

# The generated C++ lands in the build tree, so put a .clang-tidy next to it to
# keep clang-tidy from analyzing code we don't own.
configure_file(
    generated.clang-tidy
    "${CMAKE_CURRENT_BINARY_DIR}/.clang-tidy"
    COPYONLY
)

add_custom_target(xrpl_crates)
add_dependencies(tidy_prerequisites xrpl_crates)

# On macOS, ld warns `ignoring duplicate libraries` when linking a crate.
# Corrosion is the source of both duplicates it names:
#
# * The crate archive and its cxxbridge archive, because
#   `corrosion_add_cxxbridge` makes the two depend on each other, and CMake
#   repeats a static library cycle on the link line so single-pass linkers can
#   resolve it. (LINK_INTERFACE_MULTIPLICITY can only raise that count.)
# * `-lSystem`, which Corrosion copies from rustc's `native-static-libs` even
#   though the compiler driver always links libSystem.
#
# ld needs neither: it resolves the cycle from one copy of each archive and
# links libSystem once. So silence the warning rather than rewrite Corrosion's
# link interface, which the cycle is also part of. The option itself is old —
# Xcode 15 is only where the warning became the default — and the check below
# leaves it out on a linker that does not know it.
if(is_macos)
    include(CheckLinkerFlag)
    check_linker_flag(
        CXX
        -Wl,-no_warn_duplicate_libraries
        have_no_warn_duplicate_libraries
    )
endif()

function(add_xrpl_crate name)
    cmake_parse_arguments(ARG "" "CRATE" "FILES" ${ARGN})
    # `cc` picks its runtime flag from `crt-static` alone, so it compiles a
    # crate's C++ with `-MT`; Debug needs `-MTd` (to match cmake/XrplCompiler.cmake).
    if(is_msvc)
        corrosion_set_env_vars(${ARG_CRATE} "$<$<CONFIG:Debug>:CXXFLAGS=-MTd>")
    endif()
    corrosion_add_cxxbridge(
        ${name}_cxxbridge
        CRATE ${ARG_CRATE}
        FILES ${ARG_FILES}
    )
    # Generated cxxbridge headers don't exist at configure time; CMake 3.28+
    # validates INTERFACE_SOURCES on consuming targets. Clear it to skip the
    # existence check — build-time ordering is enforced by the custom commands.
    set_target_properties(${name}_cxxbridge PROPERTIES INTERFACE_SOURCES "")
    if(have_no_warn_duplicate_libraries)
        target_link_options(
            ${name}_cxxbridge
            INTERFACE -Wl,-no_warn_duplicate_libraries
        )
    endif()
    add_dependencies(xrpl_crates ${name}_cxxbridge)
endfunction()

add_xrpl_crate(rs_hello_world CRATE rs_hello_world FILES lib.rs)
