# Builds the Lean4 FFI library and links it into xrpld when formal_verification is on (default OFF).

if(NOT formal_verification)
    return()
endif()

if(NOT TARGET xrpld OR NOT tests)
    message(FATAL_ERROR "formal_verification=ON requires xrpld and tests")
endif()

foreach(_var IN ITEMS LEAN4_BINDIR LEAN4_DEPS_PACKAGES LEAN4_DEPS_ARCHIVE)
    if(NOT ${_var})
        message(
            FATAL_ERROR
            "formal_verification=ON needs ${_var} from the Conan toolchain"
        )
    endif()
endforeach()

find_package(lean4 REQUIRED)

set(lean4_src ${CMAKE_SOURCE_DIR}/formal_verification)
set(lean4_model_archive ${lean4_src}/.lake/build/lib/libXRPL_XRPLModel.a)

# Mount the dep packages (mathlib .olean files) so lake can resolve the model's imports.
set(lean4_lake ${lean4_src}/.lake)
file(MAKE_DIRECTORY ${lean4_lake})
file(REMOVE_RECURSE ${lean4_lake}/packages)
file(CREATE_LINK ${LEAN4_DEPS_PACKAGES} ${lean4_lake}/packages SYMBOLIC)

# Build the model archive where DEPENDS on the model sources so edits trigger a rebuild.
file(
    GLOB_RECURSE lean4_model_sources
    CONFIGURE_DEPENDS
    ${lean4_src}/XRPL/*.lean
)
add_custom_command(
    OUTPUT ${lean4_model_archive}
    COMMAND ${LEAN4_BINDIR}/lake build XRPLModel:static
    DEPENDS ${lean4_model_sources} ${lean4_src}/lakefile.toml
    WORKING_DIRECTORY ${lean4_src}
    COMMENT "formal_verification: Lean4 model build"
    VERBATIM
)
add_custom_target(lean4_model DEPENDS ${lean4_model_archive})

# Static link into xrpld
add_dependencies(xrpld lean4_model)
target_link_libraries(
    xrpld
    ${lean4_model_archive}
    ${LEAN4_DEPS_ARCHIVE}
    lean4::lean4
)
message(STATUS "formal_verification: Lean4 linked into xrpld")
