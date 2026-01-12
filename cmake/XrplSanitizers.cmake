#[===================================================================[
   Configure sanitizers based on environment variables.

   This module reads the following environment variables:
   - SANITIZERS: The sanitizers to enable. Possible values:
     - "address"
     - "address,undefinedbehavior"
     - "thread"
     - "thread,undefinedbehavior"
     - "undefinedbehavior"

	   The compiler type and platform are detected in CompilationEnv.cmake.
   The sanitizer compile options are applied to the 'common' interface library
   which is linked to all targets in the project.
#]===================================================================]

include(CompilationEnv)

# Read environment variable
set(SANITIZERS $ENV{SANITIZERS})

if(NOT SANITIZERS)
  return()
endif()

message(STATUS "Configuring sanitizers: ${SANITIZERS}")

# Parse SANITIZERS value to determine which sanitizers to enable
set(enable_asan FALSE)
set(enable_tsan FALSE)
set(enable_ubsan FALSE)

# Normalize SANITIZERS into a list
set(san_list "${SANITIZERS}")
string(REPLACE "," ";" san_list "${san_list}")
separate_arguments(san_list)

foreach(san IN LISTS san_list)
    if(san STREQUAL "address")
        set(enable_asan TRUE)
    elseif(san STREQUAL "thread")
        set(enable_tsan TRUE)
    elseif(san STREQUAL "undefinedbehavior")
        set(enable_ubsan TRUE)
    else()
        message(FATAL_ERROR "Unsupported sanitizer type: ${san}"
              "Supported: address, thread, undefinedbehavior and their combinations.")
    endif()
endforeach()

# Frame pointer is required for meaningful stack traces. Sanitizers recommend minimum of -O1 for reasonable performance
set(SANITIZERS_COMPILE_FLAGS "-fno-omit-frame-pointer" "-O1")

# Build the sanitizer flags list
set(SANITIZERS_FLAGS)

if(enable_asan)
    list(APPEND SANITIZERS_FLAGS "address")
elseif(enable_tsan)
    list(APPEND SANITIZERS_FLAGS "thread")
endif()

if(enable_ubsan)
    # UB sanitizer flags
    if(is_clang)
        # Clang supports additional UB checks. More info here https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html
        list(APPEND SANITIZERS_FLAGS "undefined" "float-divide-by-zero" "unsigned-integer-overflow")
    else()
        list(APPEND SANITIZERS_FLAGS "undefined" "float-divide-by-zero")
    endif()
endif()

# Configure code model for GCC on amd64
# Use large code model for ASAN to avoid relocation errors
# Use medium code model for TSAN (large is not compatible with TSAN)
set(SANITIZERS_RELOCATION_FLAGS)

# Compiler-specific configuration
if(is_gcc)
    # Disable mold, gold and lld linkers for GCC with sanitizers
    # Use default linker (bfd/ld) which is more lenient with mixed code models
    # This is needed since the size of instrumented binary exceeds the limits set by mold, lld and gold linkers
    set(use_mold OFF CACHE BOOL "Use mold linker" FORCE)
    set(use_gold OFF CACHE BOOL "Use gold linker" FORCE)
    set(use_lld OFF CACHE BOOL "Use lld linker" FORCE)
    message(STATUS "  Disabled mold, gold, and lld linkers for GCC with sanitizers")

    # Suppress false positive warnings in GCC with stringop-overflow
    list(APPEND SANITIZERS_COMPILE_FLAGS "-Wno-stringop-overflow")

    if(is_amd64 AND enable_asan)
        message(STATUS "  Using large code model (-mcmodel=large)")
        list(APPEND SANITIZERS_COMPILE_FLAGS "-mcmodel=large")
        list(APPEND SANITIZERS_RELOCATION_FLAGS "-mcmodel=large")
    elseif(enable_tsan)
        # GCC doesn't support atomic_thread_fence with tsan. Suppress warnings.
        list(APPEND SANITIZERS_COMPILE_FLAGS "-Wno-tsan")
        message(STATUS "  Using medium code model (-mcmodel=medium)")
        list(APPEND SANITIZERS_COMPILE_FLAGS "-mcmodel=medium")
        list(APPEND SANITIZERS_RELOCATION_FLAGS "-mcmodel=medium")
    endif()

    # Join sanitizer flags with commas for -fsanitize option
    list(JOIN SANITIZERS_FLAGS "," SANITIZERS_FLAGS_STR)

    # Add sanitizer to compile and link flags
    list(APPEND SANITIZERS_COMPILE_FLAGS "-fsanitize=${SANITIZERS_FLAGS_STR}")
	    set(SANITIZERS_LINK_FLAGS "${SANITIZERS_RELOCATION_FLAGS}" "-fsanitize=${SANITIZERS_FLAGS_STR}")

elseif(is_clang)
    # Add ignorelist for Clang (GCC doesn't support this)
    # Use CMAKE_SOURCE_DIR to get the path to the ignorelist
    set(IGNORELIST_PATH "${CMAKE_SOURCE_DIR}/sanitizers/suppressions/sanitizer-ignorelist.txt")
    if(NOT EXISTS "${IGNORELIST_PATH}")
        message(FATAL_ERROR "Sanitizer ignorelist not found: ${IGNORELIST_PATH}")
    endif()

    list(APPEND SANITIZERS_COMPILE_FLAGS "-fsanitize-ignorelist=${IGNORELIST_PATH}")
    message(STATUS "  Using sanitizer ignorelist: ${IGNORELIST_PATH}")

    # Join sanitizer flags with commas for -fsanitize option
    list(JOIN SANITIZERS_FLAGS "," SANITIZERS_FLAGS_STR)

    # Add sanitizer to compile and link flags
    list(APPEND SANITIZERS_COMPILE_FLAGS "-fsanitize=${SANITIZERS_FLAGS_STR}")
    set(SANITIZERS_LINK_FLAGS "-fsanitize=${SANITIZERS_FLAGS_STR}")
endif()

message(STATUS "  Compile flags: ${SANITIZERS_COMPILE_FLAGS}")
message(STATUS "  Link flags: ${SANITIZERS_LINK_FLAGS}")

# Apply the sanitizer flags to the 'common' interface library
# This is the same library used by XrplCompiler.cmake
target_compile_options(common INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:${SANITIZERS_COMPILE_FLAGS}>
    $<$<COMPILE_LANGUAGE:C>:${SANITIZERS_COMPILE_FLAGS}>
)

# Apply linker flags
target_link_options(common INTERFACE ${SANITIZERS_LINK_FLAGS})

# Define SANITIZERS macro for BuildInfo.cpp
set(sanitizers_list)
if(enable_asan)
    list(APPEND sanitizers_list "ASAN")
endif()
if(enable_tsan)
    list(APPEND sanitizers_list "TSAN")
endif()
if(enable_ubsan)
    list(APPEND sanitizers_list "UBSAN")
endif()

if(sanitizers_list)
    list(JOIN sanitizers_list "." sanitizers_str)
    target_compile_definitions(common INTERFACE SANITIZERS=${sanitizers_str})
endif()
