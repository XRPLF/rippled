# Our normal build only ever compiles `.cpp` files, so a header is only ever
# checked through whatever translation unit happens to include it. A header that
# is missing an `#include` is never caught as long as every `.cpp` that uses it
# includes its missing dependency first. To check a header on its own we compile
# it directly as a translation unit.
#
# Compiling the header itself - rather than a `.cpp` wrapper that includes it -
# gives two checks at once:
#   * the compiler fails if the header is not self-contained, i.e. it uses a
#     declaration that is not available (directly or transitively); and
#   * the header is the *main file* of its `compile_commands.json` entry, so
#     clang-tidy's misc-include-cleaner analyses (and can --fix) the header's own
#     includes - flagging a dependency that is only available transitively, which
#     a plain compile cannot catch. A wrapper would be the main file instead, and
#     include-cleaner never looks inside the headers a main file includes.
#
# The objects are never linked anywhere; we build them only for these checks.

# Verify that the headers under headers_dir compile on their own, using the
# compile environment of an existing target so each header is compiled exactly as
# that target compiles it. This works for both add_module libraries and the xrpld
# and test binaries: a library's isolated public and private include directories
# and a binary's `-I src` both live in its INCLUDE_DIRECTORIES, and the modules or
# libraries it links live in its LINK_LIBRARIES. We copy those usage requirements
# through generator expressions (rather than linking ${target}, which is
# impossible for an executable), evaluated at generation time so they capture
# requirements the caller adds after this runs. The verify library is created
# once; call this repeatedly to add more header directories.
#
# verify_target_headers(target headers_dir)
function(verify_target_headers target headers_dir)
    set(verify ${target}.verify)
    if(NOT TARGET ${verify})
        add_library(${verify} OBJECT EXCLUDE_FROM_ALL)
        # A unity build would concatenate the headers into a single translation
        # unit, where a header missing an include could be satisfied by one that
        # precedes it in the blob - exactly the bug we want to catch.
        set_target_properties(${verify} PROPERTIES UNITY_BUILD OFF)
        target_include_directories(
            ${verify}
            PRIVATE $<TARGET_PROPERTY:${target},INCLUDE_DIRECTORIES>
        )
        target_compile_definitions(
            ${verify}
            PRIVATE $<TARGET_PROPERTY:${target},COMPILE_DEFINITIONS>
        )
        target_compile_options(
            ${verify}
            PRIVATE $<TARGET_PROPERTY:${target},COMPILE_OPTIONS>
        )
        target_link_libraries(
            ${verify}
            PRIVATE $<TARGET_PROPERTY:${target},LINK_LIBRARIES>
        )
        add_dependencies(verify-headers ${verify})
    endif()
    _verify_add_headers(${verify} "${headers_dir}")
endfunction()

# Add every .h/.hpp under dir to target as a directly-compiled C++ translation
# unit. .ipp files are inline-implementation fragments included by their owning
# header (often after a class declaration), so they are not self-contained on
# their own and are verified transitively when that header is verified.
function(_verify_add_headers target dir)
    file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${dir}/*.h" "${dir}/*.hpp")
    if(NOT headers)
        return()
    endif()
    # `-xc++` forces the header to be compiled as a C++ translation unit; a lone
    # `.h` is otherwise treated as a header to precompile. `#pragma once` is
    # harmless (and warns) when the header is the main file, so silence it.
    # Compiled on its own, a header legitimately defines constants and static or
    # template functions that nothing in this single translation unit uses (they
    # exist for the files that include it), so the resulting unused-entity
    # warnings are expected and must not fail the build under -Werror.
    set_source_files_properties(
        ${headers}
        PROPERTIES
            LANGUAGE CXX
            COMPILE_OPTIONS
                "-xc++;-Wno-pragma-once-outside-header;-Wno-unused-const-variable;-Wno-unused-function"
    )
    target_sources(${target} PRIVATE ${headers})
endfunction()
