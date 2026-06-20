# Builds the Lean4 FFI library and links it into xrpld when formal_verification is on (default OFF).

if(NOT formal_verification)
    return()
endif()

if(WIN32)
    message(
        FATAL_ERROR
        "formal_verification is unsupported on native Windows; use WSL, macOS, or Linux."
    )
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

set(lean4_src ${CMAKE_SOURCE_DIR}/formal_verification)
set(lean4_model_archive ${lean4_src}/.lake/build/lib/libXRPL_XRPLModel.a)

# Mount the prebuilt dependency objects (mathlib + deps) from the lean4-deps package
set(lean4_lake ${lean4_src}/.lake)
file(MAKE_DIRECTORY ${lean4_lake})
file(REMOVE_RECURSE ${lean4_lake}/packages)
file(CREATE_LINK ${LEAN4_DEPS_PACKAGES} ${lean4_lake}/packages SYMBOLIC)

# Collect the prebuilt dependency objects from the lean4-deps package.
file(
    GLOB_RECURSE lean4_dep_objects
    CONFIGURE_DEPENDS
    ${LEAN4_DEPS_PACKAGES}/*.c.o.export
)
list(FILTER lean4_dep_objects EXCLUDE REGEX "/ir/Cache/")

# Mark these as prebuilt, generated object files so CMake links them directly instead of compiling.
set_source_files_properties(
    ${lean4_dep_objects}
    PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE
)

# Rebuild the model archive each build so model edits are picked up before the relink.
add_custom_target(
    lean4_model
    COMMAND ${LEAN4_BINDIR}/lake build XRPLModel:static
    BYPRODUCTS ${lean4_model_archive}
    WORKING_DIRECTORY ${lean4_src}
    COMMENT "formal_verification: Lean4 model build"
    VERBATIM
)

# Assemble the shared FFI lib: dep objects + model archive + lean4 runtime.
add_library(XRPL_XRPLModel SHARED ${lean4_dep_objects})
add_dependencies(XRPL_XRPLModel lean4_model)
set_target_properties(XRPL_XRPLModel PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(
    XRPL_XRPLModel
    PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,${lean4_model_archive}>"
    PUBLIC lean4::lean4
)

target_link_libraries(xrpld XRPL_XRPLModel)
message(STATUS "formal_verification: Lean4 linked into xrpld")
