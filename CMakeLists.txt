cmake_minimum_required(VERSION 3.16)

if(POLICY CMP0074)
  cmake_policy(SET CMP0074 NEW)
endif()
if(POLICY CMP0077)
  cmake_policy(SET CMP0077 NEW)
endif()

# Fix "unrecognized escape" issues when passing CMAKE_MODULE_PATH on Windows.
file(TO_CMAKE_PATH "${CMAKE_MODULE_PATH}" CMAKE_MODULE_PATH)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Builds/CMake")

project(rippled)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# make GIT_COMMIT_HASH define available to all sources
find_package(Git)
if(Git_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} --git-dir=${CMAKE_CURRENT_SOURCE_DIR}/.git describe --always --abbrev=40
        OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE gch)
    if(gch)
        set(GIT_COMMIT_HASH "${gch}")
        message(STATUS gch: ${GIT_COMMIT_HASH})
        add_definitions(-DGIT_COMMIT_HASH="${GIT_COMMIT_HASH}")
    endif()
endif() #git

if(thread_safety_analysis)
  add_compile_options(-Wthread-safety -D_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS -DRIPPLE_ENABLE_THREAD_SAFETY_ANNOTATIONS)
  add_compile_options("-stdlib=libc++")
  add_link_options("-stdlib=libc++")
endif()

include (CheckCXXCompilerFlag)
include (FetchContent)
include (ExternalProject)
include (CMakeFuncs) # must come *after* ExternalProject b/c it overrides one function in EP
if (target)
  message (FATAL_ERROR "The target option has been removed - use native cmake options to control build")
endif ()

include(RippledSanity)
include(RippledVersion)
include(RippledSettings)
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

option(only_docs "Include only the docs target?" FALSE)
include(RippledDocs)
if(only_docs)
  return()
endif()

###

include(deps/Boost)
find_package(OpenSSL 1.1.1 REQUIRED)
set_target_properties(OpenSSL::SSL PROPERTIES
  INTERFACE_COMPILE_DEFINITIONS OPENSSL_NO_SSL2
)
set(SECP256K1_INSTALL TRUE)
add_subdirectory(src/secp256k1)
add_library(secp256k1::secp256k1 ALIAS secp256k1)
add_subdirectory(src/ed25519-donna)
find_package(lz4 REQUIRED)
# Target names with :: are not allowed in a generator expression.
# We need to pull the include directories and imported location properties
# from separate targets.
find_package(LibArchive REQUIRED)
find_package(SOCI REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Snappy REQUIRED)

option(rocksdb "Enable RocksDB" ON)
if(rocksdb)
  find_package(RocksDB REQUIRED)
  set_target_properties(RocksDB::rocksdb PROPERTIES
    INTERFACE_COMPILE_DEFINITIONS RIPPLE_ROCKSDB_AVAILABLE=1
  )
  target_link_libraries(ripple_libs INTERFACE RocksDB::rocksdb)
endif()

find_package(nudb REQUIRED)
find_package(date REQUIRED)

target_link_libraries(ripple_libs INTERFACE
  ed25519::ed25519
  LibArchive::LibArchive
  lz4::lz4
  OpenSSL::Crypto
  OpenSSL::SSL
  secp256k1::secp256k1
  soci::soci
  SQLite::SQLite3
)

# Work around changes to Conan recipe for now.
if(TARGET nudb::core)
  set(nudb nudb::core)
elseif(TARGET NuDB::nudb)
  set(nudb NuDB::nudb)
else()
  message(FATAL_ERROR "unknown nudb target")
endif()
target_link_libraries(ripple_libs INTERFACE ${nudb})

if(reporting)
  find_package(cassandra-cpp-driver REQUIRED)
  find_package(PostgreSQL REQUIRED)
  target_link_libraries(ripple_libs INTERFACE
    cassandra-cpp-driver::cassandra-cpp-driver
    PostgreSQL::PostgreSQL
  )
endif()

if(coverage)
  include(RippledCov)
endif()

###

include(RippledCore)
include(deps/Protobuf)
include(deps/gRPC)
include(RippledInstall)
include(RippledMultiConfig)
include(RippledValidatorKeys)
