# Shared detection of compiler, operating system, and architecture.
#
# This module centralizes environment detection so that other CMake modules can use the same variables instead of
# repeating checks on CMAKE_* and built-in platform variables.

# Only run once per configure step.
include_guard(GLOBAL)

# --------------------------------------------------------------------
# Compiler detection (C++)
# --------------------------------------------------------------------
set(is_clang FALSE)
set(is_gcc FALSE)
set(is_msvc FALSE)
set(is_xcode FALSE)

if (CMAKE_CXX_COMPILER_ID MATCHES ".*Clang") # Clang or AppleClang
    set(is_clang TRUE)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(is_gcc TRUE)
elseif (MSVC)
    set(is_msvc TRUE)
else ()
    message(FATAL_ERROR "Unsupported C++ compiler: ${CMAKE_CXX_COMPILER_ID}")
endif ()

# Xcode generator detection
if (CMAKE_GENERATOR STREQUAL "Xcode")
    set(is_xcode TRUE)
endif ()

# --------------------------------------------------------------------
# Operating system detection
# --------------------------------------------------------------------
set(is_linux FALSE)
set(is_windows FALSE)
set(is_macos FALSE)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(is_linux TRUE)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(is_windows TRUE)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(is_macos TRUE)
endif ()

# --------------------------------------------------------------------
# Architecture
# --------------------------------------------------------------------
set(is_amd64 FALSE)
set(is_arm64 FALSE)
if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(is_amd64 TRUE)
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    set(is_arm64 TRUE)
else ()
    message(FATAL_ERROR "Unknown architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif ()
