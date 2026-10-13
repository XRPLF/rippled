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

if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang") # Clang or AppleClang
    set(is_clang TRUE)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(is_gcc TRUE)
elseif(MSVC)
    set(is_msvc TRUE)
else()
    message(FATAL_ERROR "Unsupported C++ compiler: ${CMAKE_CXX_COMPILER_ID}")
endif()

# Xcode generator detection
if(CMAKE_GENERATOR STREQUAL "Xcode")
    set(is_xcode TRUE)
endif()

# --------------------------------------------------------------------
# Nix toolchain detection
# --------------------------------------------------------------------
# True when the C++ compiler resolves into the Nix store. CMAKE_CXX_COMPILER may
# be referenced through a symlink outside the store (a Nix profile, a /usr/bin
# alternative, ...), so resolve the real path before matching.
set(is_nix_compiler FALSE)
get_filename_component(_cxx_real "${CMAKE_CXX_COMPILER}" REALPATH)
if(_cxx_real MATCHES "^/nix/store/")
    set(is_nix_compiler TRUE)
endif()
unset(_cxx_real)

# True inside the Nix CI Docker image, identified by the /nix/ci-env tree it
# ships (see nix/docker/Dockerfile). The dev shell and bare systems don't have
# it, so it distinguishes the CI image from other Nix-compiler environments.
set(is_ci_image FALSE)
if(EXISTS "/nix/ci-env/bin")
    set(is_ci_image TRUE)
endif()

# --------------------------------------------------------------------
# Operating system detection
# --------------------------------------------------------------------
set(is_linux FALSE)
set(is_windows FALSE)
set(is_macos FALSE)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(is_linux TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(is_windows TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(is_macos TRUE)
endif()

# --------------------------------------------------------------------
# Architecture
# --------------------------------------------------------------------
set(is_amd64 FALSE)
set(is_arm64 FALSE)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(is_amd64 TRUE)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    set(is_arm64 TRUE)
else()
    message(FATAL_ERROR "Unknown architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

# --------------------------------------------------------------------
# Sanitizers
# --------------------------------------------------------------------
# SANITIZERS is injected by the Conan toolchain when a sanitizer build is
# requested (see conan/profiles/sanitizers). The flags are applied to the
# 'common' target in XrplSanitizers; this flag lets other modules know a
# sanitizer build is active without depending on that module.
if(DEFINED SANITIZERS)
    set(SANITIZERS_ENABLED TRUE)
else()
    set(SANITIZERS_ENABLED FALSE)
endif()
