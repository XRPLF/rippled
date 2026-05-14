#[===================================================================[
   Apply sanitizer flags built by the Conan profile.

   Parsing, validation, and flag construction are performed in conan/profiles/sanitizers.
   This module reads the following CMake variables injected by the Conan toolchain via extra_variables:

   - SANITIZERS:                The active sanitizers (e.g. "address,undefinedbehavior").
   - SANITIZERS_COMPILER_FLAGS: Space-separated compiler flags.
   - SANITIZERS_LINKER_FLAGS:   Space-separated linker flags.

   The flags are applied to the 'common' interface library which is linked to all targets in the project.
#]===================================================================]

include_guard(GLOBAL)
include(CompilationEnv)

if(NOT DEFINED SANITIZERS)
    set(SANITIZERS_ENABLED FALSE)
    return()
endif()
set(SANITIZERS_ENABLED TRUE)

message(STATUS "=== Configuring Sanitizers ===")
message(STATUS "  SANITIZERS: ${SANITIZERS}")
message(STATUS "  Compile flags: ${SANITIZERS_COMPILER_FLAGS}")
message(STATUS "  Link flags: ${SANITIZERS_LINKER_FLAGS}")

# GCC with sanitizers is incompatible with mold, gold, and lld linkers.
# Namely, the instrumented binary exceeds size limits imposed by these linkers.
if(is_gcc)
    set(use_mold OFF CACHE BOOL "Use mold linker" FORCE)
    set(use_gold OFF CACHE BOOL "Use gold linker" FORCE)
    set(use_lld OFF CACHE BOOL "Use lld linker" FORCE)
    message(
        STATUS
        "  Disabled mold, gold, and lld linkers for GCC with sanitizers"
    )
endif()

# Flags arrive as space-separated strings; split into CMake lists before use
separate_arguments(
    sanitizers_compiler_flags
    UNIX_COMMAND
    "${SANITIZERS_COMPILER_FLAGS}"
)
separate_arguments(
    sanitizers_linker_flags
    UNIX_COMMAND
    "${SANITIZERS_LINKER_FLAGS}"
)

target_compile_options(
    common
    INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:${sanitizers_compiler_flags}>
        $<$<COMPILE_LANGUAGE:C>:${sanitizers_compiler_flags}>
)
target_link_options(common INTERFACE ${sanitizers_linker_flags})

# This module appends -fsanitize-ignorelist=<path> for Clang builds.
# The ignorelist path contains CMAKE_SOURCE_DIR, so it must be set here, rather than in the Conan profile.
# GCC does not support -fsanitize-ignorelist.
if(is_clang)
    set(ignorelist_path
        "${CMAKE_SOURCE_DIR}/sanitizers/suppressions/sanitizer-ignorelist.txt"
    )
    if(NOT EXISTS "${ignorelist_path}")
        message(
            FATAL_ERROR
            "Sanitizer ignorelist not found: ${ignorelist_path}"
        )
    endif()
    target_compile_options(
        common
        INTERFACE
            $<$<COMPILE_LANGUAGE:CXX>:-fsanitize-ignorelist=${ignorelist_path}>
            $<$<COMPILE_LANGUAGE:C>:-fsanitize-ignorelist=${ignorelist_path}>
    )
    message(STATUS "  Ignorelist: ${ignorelist_path}")
endif()

# Define SANITIZERS macro for BuildInfo.cpp
set(sanitizers_list)
if(SANITIZERS MATCHES "address")
    set(enable_asan ON)
    list(APPEND sanitizers_list "ASAN")
endif()
if(SANITIZERS MATCHES "thread")
    set(enable_tsan ON)
    list(APPEND sanitizers_list "TSAN")
endif()
if(SANITIZERS MATCHES "undefinedbehavior")
    set(enable_ubsan ON)
    list(APPEND sanitizers_list "UBSAN")
endif()

if(sanitizers_list)
    list(JOIN sanitizers_list "." sanitizers_str)
    target_compile_definitions(common INTERFACE SANITIZERS=${sanitizers_str})
endif()
