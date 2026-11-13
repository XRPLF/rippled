# Builds the Lean4 FFI library and links it into xrpld when formal_verification is on (default OFF).

if(NOT formal_verification)
    return()
endif()

if(NOT TARGET xrpld OR NOT tests)
    message(FATAL_ERROR "formal_verification=ON requires xrpld and tests")
endif()

foreach(_var IN ITEMS LEAN4_BINDIR LEAN4_DEPS_PACKAGES)
    if(NOT ${_var})
        message(
            FATAL_ERROR
            "formal_verification=ON needs ${_var} from the Conan toolchain"
        )
    endif()
endforeach()

find_package(lean4 REQUIRED)
find_package(lean4-deps REQUIRED)

set(lean4_src ${CMAKE_SOURCE_DIR}/formal_verification)
# CMake's own lake workspace, separate from formal_verification/.lake (see
# XrplLean4ModelBuild.cmake).
set(lean4_work ${CMAKE_BINARY_DIR}/formal_verification)
set(lean4_model_archive ${lean4_work}/.lake/build/lib/libXRPL_XRPLModel.a)

# Always the Conan toolchain, never a lean/lake on PATH: lake records Lean's
# githash in its build traces, so a same-version but differently-built lean
# would invalidate the prebuilt mathlib and silently re-elaborate all of it.
set(lean4_lake ${LEAN4_BINDIR}/lake)

# Fail at configure time rather than as an opaque "cannot execute" mid-build.
execute_process(
    COMMAND ${lean4_lake} --version
    RESULT_VARIABLE lean4_lake_result
    OUTPUT_QUIET
    ERROR_QUIET
)
if(NOT lean4_lake_result EQUAL 0)
    message(
        FATAL_ERROR
        "formal_verification: '${lean4_lake}' cannot be executed. The lean4 "
        "package ships upstream binaries that need the FHS loader "
        "/lib64/ld-linux-x86-64.so.2. On NixOS, provide it by enabling nix-ld "
        "(programs.nix-ld.enable = true)."
    )
endif()

# Mount the dep packages (mathlib .olean files) so lake can resolve the model's imports.
set(lean4_lake_dir ${lean4_work}/.lake)
file(MAKE_DIRECTORY ${lean4_lake_dir})
file(REMOVE_RECURSE ${lean4_lake_dir}/packages)
file(CREATE_LINK ${LEAN4_DEPS_PACKAGES} ${lean4_lake_dir}/packages SYMBOLIC)

# Build the model archive where DEPENDS on the model sources so edits trigger a rebuild.
file(
    GLOB_RECURSE lean4_model_sources
    CONFIGURE_DEPENDS
    ${lean4_src}/XRPL/*.lean
)
add_custom_command(
    OUTPUT ${lean4_model_archive}
    COMMAND
        ${CMAKE_COMMAND} -DLEAN4_LAKE=${lean4_lake} -DLEAN4_SRC=${lean4_src}
        -DLEAN4_WORK=${lean4_work} -P
        ${CMAKE_CURRENT_LIST_DIR}/XrplLean4ModelBuild.cmake
    DEPENDS
        ${lean4_model_sources}
        ${lean4_src}/XRPL.lean
        ${lean4_src}/lakefile.toml
        ${lean4_src}/lake-manifest.json
        ${lean4_src}/lean-toolchain
        ${CMAKE_CURRENT_LIST_DIR}/XrplLean4ModelBuild.cmake
    COMMENT "formal_verification: Lean4 model build"
    VERBATIM
)
add_custom_target(lean4_model DEPENDS ${lean4_model_archive})

# Static link into xrpld
add_dependencies(xrpld lean4_model)
target_link_libraries(
    xrpld
    ${lean4_model_archive}
    lean4-deps::lean4-deps
    lean4::lean4
)
message(STATUS "formal_verification: Lean4 linked into xrpld")
