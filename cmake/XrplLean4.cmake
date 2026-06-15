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

set(lean_src ${CMAKE_SOURCE_DIR}/formal_verification)
set(lean_lake ${CMAKE_BINARY_DIR}/lean4)
if(APPLE)
    set(lean_suffix dylib)
else()
    set(lean_suffix so)
endif()
set(lean_lib ${lean_src}/.lake/build/lib/libXRPLModel.${lean_suffix})

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
        "formal_verification: priming the mathlib olean cache (one time)"
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

# build_lean.sh is incremental: mathlib compiles once, the model relinks on change.
add_custom_target(
    formal_verification
    BYPRODUCTS ${lean_lib}
    COMMAND
        ${CMAKE_COMMAND} -E env LEAN_BINDIR=${LEAN_BINDIR}
        LEAN_LIBDIR=${LEAN_LIBDIR} GMP_LIBDIR=${GMP_LIBDIR} sh
        ${lean_src}/scripts/build_lean.sh
    WORKING_DIRECTORY ${lean_src}
    COMMENT "Building the Lean formal-verification library"
    VERBATIM
)
add_dependencies(xrpld formal_verification)

find_package(lean4 REQUIRED)
message(STATUS "Lean FFI: enabled (formal_verification=ON)")
# libXRPLModel uses @rpath; xrpld needs rpath entries to it and the Lean runtime.
target_link_libraries(xrpld ${lean_lib} lean4::lean4)
set_property(
    TARGET xrpld
    APPEND
    PROPERTY
        BUILD_RPATH ${lean_src}/.lake/build/lib ${LEAN_LIBDIR} ${GMP_LIBDIR}
)
