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

foreach(_var IN ITEMS LEAN4_BINDIR LEAN4_LIBDIR)
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

if(NOT EXISTS ${lean4_src}/.lake/packages/mathlib)
    message(
        STATUS
        "formal_verification: Lean4 downloading cached objects (one-time, this can take a few minutes)"
    )
    execute_process(
        COMMAND ${LEAN4_BINDIR}/lake exe cache get
        WORKING_DIRECTORY ${lean4_src}
        RESULT_VARIABLE _cache_result
        OUTPUT_VARIABLE _cache_log
        ERROR_VARIABLE _cache_log
    )
    if(NOT _cache_result EQUAL 0)
        message("${_cache_log}")
        message(FATAL_ERROR "lake exe cache get failed (${_cache_result})")
    endif()
endif()

# lake's :static archive trips ARG_MAX on mathlib, so the object count check below is the success test
set(lean_dep_targets
    ProofWidgets:static
    ImportGraph:static
    LeanSearchClient:static
    Plausible:static
    Aesop:static
    Qq:static
    Batteries:static
    Mathlib:static
)
message(
    STATUS
    "formal_verification: Lean4 compiling objects (one-time, this can take a few minutes)"
)
execute_process(
    COMMAND ${LEAN4_BINDIR}/lake build ${lean_dep_targets} XRPLModel:static
    WORKING_DIRECTORY ${lean4_src}
    OUTPUT_VARIABLE _lake_log
    ERROR_VARIABLE _lake_log
)

# Collect the compiled dependency objects. CONFIGURE_DEPENDS rescans them if the set changes.
file(
    GLOB_RECURSE lean4_dep_objects
    CONFIGURE_DEPENDS
    ${lean4_src}/.lake/packages/*.c.o.export
)
list(FILTER lean4_dep_objects EXCLUDE REGEX "/ir/Cache/")
list(LENGTH lean4_dep_objects lean4_dep_count)
if(lean4_dep_count LESS 7000)
    message("${_lake_log}")
    message(
        FATAL_ERROR
        "formal_verification: Lean4 only ${lean4_dep_count} objects found (expected >= 7000)"
    )
endif()
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
    COMMENT "Building the Lean4 formal-verification model"
    VERBATIM
)

add_library(lean4_dep_lib SHARED ${lean4_dep_objects})
add_dependencies(lean4_dep_lib lean4_model)
set_target_properties(lean4_dep_lib PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(
    lean4_dep_lib
    PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,${lean4_model_archive}>" lean4::lean4
)

add_dependencies(xrpld lean4_dep_lib)
target_link_libraries(xrpld lean4_dep_lib lean4::lean4)
set_property(TARGET xrpld APPEND PROPERTY BUILD_RPATH ${LEAN4_LIBDIR})

message(
    STATUS
    "formal_verification: Lean4 linked into xrpld (${lean4_dep_count} objects)"
)
