cmake_minimum_required (VERSION 3.16)

if (POLICY CMP0074)
  cmake_policy(SET CMP0074 NEW)
endif ()

project (rippled)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# make GIT_COMMIT_HASH define available to all sources
find_package(Git)
if(Git_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --always --abbrev=40
        OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE gch)
    if(gch)
        set(GIT_COMMIT_HASH "${gch}")
        message(STATUS gch: ${GIT_COMMIT_HASH})
        add_definitions(-DGIT_COMMIT_HASH="${GIT_COMMIT_HASH}")
    endif()
endif() #git

if (thread_safety_analysis)
  add_compile_options(-Wthread-safety -D_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS -DRIPPLE_ENABLE_THREAD_SAFETY_ANNOTATIONS)
  add_compile_options("-stdlib=libc++")
  add_link_options("-stdlib=libc++")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Builds/CMake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Builds/CMake/deps")

include (CheckCXXCompilerFlag)
include (FetchContent)
include (ExternalProject)
include (CMakeFuncs) # must come *after* ExternalProject b/c it overrides one function in EP
include (ProcessorCount)
if (target)
  message (FATAL_ERROR "The target option has been removed - use native cmake options to control build")
endif ()

include(RippledSanity)
include(RippledVersion)
include(RippledSettings)
include(RippledNIH)
include(RippledRelease)
# this check has to remain in the top-level cmake
# because of the early return statement
if (packages_only)
  if (NOT TARGET rpm)
    message (FATAL_ERROR "packages_only requested, but targets were not created - is docker installed?")
  endif()
  return ()
endif ()
include(RippledCompiler)
include(RippledInterface)

###

include(deps/Boost)
include(deps/OpenSSL)
include(deps/Secp256k1)
include(deps/Ed25519-donna)
include(deps/Lz4)
include(deps/Libarchive)
include(deps/Sqlite)
include(deps/Soci)
include(deps/Snappy)
include(deps/Rocksdb)
include(deps/Nudb)
include(deps/date)
include(deps/Protobuf)
include(deps/gRPC)
include(deps/cassandra)
include(deps/Postgres)

###

include(RippledCore)
include(RippledInstall)
include(RippledCov)
include(RippledMultiConfig)
include(RippledDocs)
include(RippledValidatorKeys)
