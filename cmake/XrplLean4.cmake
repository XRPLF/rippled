# Builds the Lean 4 FFI library and links it into xrpld when formal_verification is on (default OFF).

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

foreach(_var IN ITEMS LEAN_BINDIR LEAN_LIBDIR GMP_LIBDIR)
    if(NOT ${_var})
        message(
            FATAL_ERROR
            "formal_verification=ON needs ${_var} from the Conan toolchain"
        )
    endif()
endforeach()

find_package(lean4 REQUIRED)
find_package(gmp REQUIRED)

set(lean_src ${CMAKE_SOURCE_DIR}/formal_verification)
set(lean_lake ${CMAKE_BINARY_DIR}/lean4)
set(lean_model_archive ${lean_src}/.lake/build/lib/libXRPL_XRPLModel.a)

# Redirect lake's .lake into the build tree, so the checkout stays clean.
file(MAKE_DIRECTORY ${lean_lake})
if(IS_SYMLINK ${lean_src}/.lake)
    file(REMOVE ${lean_src}/.lake)
endif()
if(NOT EXISTS ${lean_src}/.lake)
    file(CREATE_LINK ${lean_lake} ${lean_src}/.lake SYMBOLIC)
endif()
if(NOT EXISTS ${lean_src}/.lake/packages/mathlib)
    message(
        STATUS
        "formal_verification: downloading the prebuilt mathlib cache (one-time, this can take a few minutes)"
    )
    execute_process(
        COMMAND ${LEAN_BINDIR}/lake exe cache get
        WORKING_DIRECTORY ${lean_src}
        RESULT_VARIABLE _cache_result
    )
    if(NOT _cache_result EQUAL 0)
        message(FATAL_ERROR "lake exe cache get failed (${_cache_result})")
    endif()
endif()

# Compile the model + dependency objects (configure time). lake's exit code is unreliable (its
# :static archive trips ARG_MAX on mathlib), so the object-count check below is the success test.
set(lean_dep_targets
    ProofWidgets:static
    ImportGraph:static
    LeanSearchClient:static
    Plausible:static
    Aesop:static
    Qq:static
    Cli:static
    Batteries:static
    Mathlib:static
)
execute_process(
    COMMAND ${LEAN_BINDIR}/lake build ${lean_dep_targets} XRPLModel:static
    WORKING_DIRECTORY ${lean_src}
)

# Collect the compiled dependency objects. CONFIGURE_DEPENDS rescans them if the set changes.
file(
    GLOB_RECURSE lean_dep_objects
    CONFIGURE_DEPENDS
    ${lean_src}/.lake/packages/*.c.o.export
)
list(FILTER lean_dep_objects EXCLUDE REGEX "/ir/Cache/")
list(LENGTH lean_dep_objects lean_dep_count)
if(lean_dep_count LESS 7000)
    message(
        FATAL_ERROR
        "formal_verification: only ${lean_dep_count} Lean dependency objects found (expected >= 7000); did the lake build fail?"
    )
endif()
set_source_files_properties(
    ${lean_dep_objects}
    PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE
)

# Thousands of objects overflow the link command line; route them through a response file.
set(CMAKE_CXX_USE_RESPONSE_FILE_FOR_OBJECTS ON)

# Rebuild the model archive each build so model edits are picked up before the relink.
add_custom_target(
    lean_model
    COMMAND ${LEAN_BINDIR}/lake build XRPLModel:static
    BYPRODUCTS ${lean_model_archive}
    WORKING_DIRECTORY ${lean_src}
    COMMENT "Building the Lean formal-verification model"
    VERBATIM
)

# Whole-archive the model so its @[export] FFI symbols (unreferenced internally) aren't dropped.
add_library(XRPLModel SHARED ${lean_dep_objects})
add_dependencies(XRPLModel lean_model)
set_target_properties(XRPLModel PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(
    XRPLModel
    PRIVATE
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,${lean_model_archive}>"
        lean4::lean4
        gmp::libgmp
)

add_dependencies(xrpld XRPLModel)
target_link_libraries(xrpld XRPLModel lean4::lean4)
set_property(TARGET xrpld APPEND PROPERTY BUILD_RPATH ${LEAN_LIBDIR})
