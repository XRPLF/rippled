# cmake support for building rippled. The rippled specific settings
# below can be set at the command line as `-D<setting>=<value>`.
#
# * `target` is a period separated tuple from the sets
#    {gcc,clang,msvc} x {debug, release} x {unity, nounity} x {coverage} x {profile}
#
#     Example, build gcc debug nonunity build
#       -Dtarget=gcc.debug.nounity
#     Example, clang release unity build
#       -Dtarget=clang.release.unity
#     Example, visual studio debug unity build
#       -Dtarget=msvc.release.unity
#     Example, build gcc release unity build suited for profiling with perf
#       -Dtarget=gcc.release.unity.profile
#     Example, build gcc debug unity build suited for measuring code coverage
#     with gcov
#       -Dtarget=gcc.release.unity.coverage
#
#
#   The default is a unity debug build using gcc (linux), clang (osx), and
#   msvc (windows).
#
#   Note the generated Visual Studio solution will always have two projects,
#   one unity and one non-unity. If the `target` is unity, the default project
#   will be named `rippled` and second non-default (non-unity) project
#   will be called `rippled_classic`. Likewise, if the `target` is non-unity,
#   the project will have a default project called `rippled` (now non-unity)
#   and second non-default (unity) project `rippled_unity`. In either
#   case, only the `rippled` build will be enabled by default.
#
# * `assert` whether to enable asserts in release build
#
#      Example, enable asserts even in release builds
#        -Dassert=True
#
#    Default is not to enable asserts in release builds.
#
# * `san` enable clang sanitizers
#
#     Example, enable thread sanitizer
#       -Dsan=thread
#     Example, enable address sanitizer
#       -Dsan=address
#
# * `static`, on linux, link protobuf, openssl, libc++, and boost
#   statically.
#
#     Example, enable static linking
#       -Dstatic=True
#
# * `jemalloc`, on linux, enables jemalloc for heap profiling.
#
#    Example, enable jemalloc
#      -Djemallc=True
#
#########################################################
# CMAKE_C_COMPILER and CMAKE_CXX_COMPILER must be defined
# before the project statement; However, the project
# statement will clear CMAKE_BUILD_TYPE. CACHE variables,
# along with the order of this code, are used to work
# around these constraints.
#
# Don't put any code above or in this block, unless it
# has similar constraints.
cmake_minimum_required(VERSION 3.1.0)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/Builds/CMake")
include(CMakeFuncs)
set(openssl_min 1.0.2)
parse_target()
project(rippled)
#########################################################

if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
  set(dir "build")
  set(cmd "cmake")
  if (target)
    set(dir "${dir}/${target}")
    set(cmd "${cmd} -Dtarget=${target}")
  elseif(CMAKE_BUILD_TYPE)
    set(dir "${dir}/${CMAKE_BUILD_TYPE}")
    set(cmd "${cmd} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
  else()
    set(dir "${dir}/default")
  endif()
  set(cmd "${cmd} ${CMAKE_SOURCE_DIR}")

  message(FATAL_ERROR "Builds are not allowed in ${CMAKE_SOURCE_DIR}.\n"
    "Instead:\n"
    "1) Remove the CMakeCache.txt file and CMakeFiles directory "
   "from ${CMAKE_SOURCE_DIR}.\n"
    "2) Create a directory to hold your build files, for example: ${dir}.\n"
    "3) Change to that directory.\n"
    "4) Run cmake targetting ${CMAKE_SOURCE_DIR}, for example: ${cmd}")
endif()
if("${CMAKE_GENERATOR}" MATCHES "Visual Studio" AND
    NOT ("${CMAKE_GENERATOR}" MATCHES .*Win64.*))
  message(FATAL_ERROR "Visual Studio 32-bit build is unsupported. Use
    -G\"${CMAKE_GENERATOR} Win64\"")
endif()

setup_build_cache()

if(nonunity)
    get_cmake_property(allvars VARIABLES)
    string(REGEX MATCHALL "[^;]*(DEBUG|RELEASE)[^;]*" matchvars "${allvars}")
    foreach(var IN LISTS matchvars)
        string(REGEX REPLACE "(DEBUG|RELEASE)" "\\1CLASSIC" newvar ${var})
        set(${newvar} ${${var}})
    endforeach()

    get_cmake_property(allvars CACHE_VARIABLES)
    string(REGEX MATCHALL "[^;]*(DEBUG|RELEASE)[^;]*" matchvars "${allvars}")
    foreach(var IN LISTS matchvars)
        string(REGEX REPLACE "(DEBUG|RELEASE)" "\\1CLASSIC" newvar ${var})
        set(${newvar} ${${var}} CACHE STRING "Copied from ${var}")
    endforeach()
endif()

determine_build_type()

check_gcc4_abi()

############################################################

include_directories(
  src
  src/beast
  src/beast/include
  src/beast/extras
  src/nudb/include
  src/soci/src
  src/soci/include)

special_build_flags()

############################################################

use_boost(
  # resist the temptation to alphabetize these. coroutine
  # must come before context.
  chrono
  coroutine
  context
  date_time
  filesystem
  program_options
  regex
  serialization
  system
  thread)

use_pthread()

use_openssl(${openssl_min})

use_protobuf()

setup_build_boilerplate()

############################################################

if (is_clang)
  set(rocks_db_system_header --system-header-prefix=rocksdb2)
else()
  unset(rocks_db_system_header)
endif()

set(soci_extra_includes
  -I"${CMAKE_SOURCE_DIR}/"src/soci/src/core
  -I"${CMAKE_SOURCE_DIR}/"src/soci/include/private
  -I"${CMAKE_SOURCE_DIR}/"src/sqlite)

############################################################

# Unity sources
prepend(beast_unity_srcs
src/ripple/beast/unity/
beast_insight_unity.cpp
beast_net_unity.cpp
beast_hash_unity.cpp
beast_utility_unity.cpp)

prepend(ripple_unity_srcs
src/ripple/unity/
app_consensus.cpp
app_ledger.cpp
app_ledger_impl.cpp
app_main1.cpp
app_main2.cpp
app_misc.cpp
app_misc_impl.cpp
app_paths.cpp
app_tx.cpp
conditions.cpp
consensus.cpp
core.cpp
basics.cpp
crypto.cpp
ledger.cpp
net.cpp
overlay1.cpp
overlay2.cpp
peerfinder.cpp
json.cpp
protocol.cpp
resource.cpp
rpcx1.cpp
rpcx2.cpp
shamap.cpp
server.cpp)

prepend(test_unity_srcs
src/test/unity/
app_test_unity1.cpp
app_test_unity2.cpp
basics_test_unity.cpp
beast_test_unity1.cpp
beast_test_unity2.cpp
conditions_test_unity.cpp
consensus_test_unity.cpp
core_test_unity.cpp
json_test_unity.cpp
ledger_test_unity.cpp
overlay_test_unity.cpp
peerfinder_test_unity.cpp
protocol_test_unity.cpp
resource_test_unity.cpp
rpc_test_unity.cpp
server_test_unity.cpp
server_status_test_unity.cpp
shamap_test_unity.cpp
jtx_unity1.cpp
jtx_unity2.cpp
csf_unity.cpp)

list(APPEND rippled_src_unity ${beast_unity_srcs} ${ripple_unity_srcs} ${test_unity_srcs})

add_with_props(rippled_src_unity src/test/unity/nodestore_test_unity.cpp
-I"${CMAKE_SOURCE_DIR}/"src/rocksdb2/include
-I"${CMAKE_SOURCE_DIR}/"src/snappy/snappy
-I"${CMAKE_SOURCE_DIR}/"src/snappy/config
${rocks_db_system_header})

add_with_props(rippled_src_unity src/ripple/unity/nodestore.cpp
-I"${CMAKE_SOURCE_DIR}/"src/rocksdb2/include
-I"${CMAKE_SOURCE_DIR}/"src/snappy/snappy
-I"${CMAKE_SOURCE_DIR}/"src/snappy/config
${rocks_db_system_header})

add_with_props(rippled_src_unity src/ripple/unity/soci_ripple.cpp ${soci_extra_includes})

list(APPEND ripple_unity_srcs ${beast_unity_srcs} ${test_unity_srcs}
src/ripple/unity/nodestore.cpp
src/ripple/unity/soci_ripple.cpp
src/test/unity/nodestore_test_unity.cpp)

############################################################

# Non-unity sources
file(GLOB_RECURSE core_srcs src/ripple/core/*.cpp)
add_with_props(rippled_src_nonunity "${core_srcs}"
    -I"${CMAKE_SOURCE_DIR}/"src/soci/src/core
    -I"${CMAKE_SOURCE_DIR}/"src/sqlite)

set(non_unity_srcs ${core_srcs})

foreach(curdir
        beast/clock
        beast/container
        beast/hash
        beast/insight
        beast/net
        beast/utility
        app
        basics
        conditions
        crypto
        consensus
        json
        ledger
        legacy
        net
        overlay
        peerfinder
        protocol
        resource
        rpc
        server
        shamap)
    file(GLOB_RECURSE cursrcs src/ripple/${curdir}/*.cpp)
    list(APPEND rippled_src_nonunity "${cursrcs}")
    list(APPEND non_unity_srcs "${cursrcs}")
endforeach()

file(GLOB_RECURSE nodestore_srcs src/ripple/nodestore/*.cpp
    src/test/nodestore/*.cpp)

add_with_props(rippled_src_nonunity "${nodestore_srcs}"
    -I"${CMAKE_SOURCE_DIR}/"src/rocksdb2/include
    -I"${CMAKE_SOURCE_DIR}/"src/snappy/snappy
    -I"${CMAKE_SOURCE_DIR}/"src/snappy/config
    ${rocks_db_system_header})

list(APPEND non_unity_srcs "${nodestore_srcs}")

# unit test sources
foreach(curdir
        app
        basics
        beast
        conditions
        consensus
        core
        csf
        json
        jtx
        ledger
        nodestore
        overlay
        peerfinder
        protocol
        resource
        rpc
        server
        shamap
        unit_test)
    file(GLOB_RECURSE cursrcs src/test/${curdir}/*.cpp)
    list(APPEND test_srcs "${cursrcs}")
endforeach()

add_with_props(rippled_src_nonunity "${test_srcs}"
    -I"${CMAKE_SOURCE_DIR}/"src/rocksdb2/include
    -I"${CMAKE_SOURCE_DIR}/"src/snappy/snappy
    -I"${CMAKE_SOURCE_DIR}/"src/snappy/config
    ${rocks_db_system_header})

list(APPEND non_unity_srcs "${test_srcs}")

if(WIN32 OR is_xcode)
    # Rippled headers. Only needed for IDEs.
    file(GLOB_RECURSE rippled_headers src/*.h src/*.hpp *.md)
    list(APPEND rippled_headers Builds/CMake/CMakeFuncs.cmake)
    foreach(curdir
            beast/asio
            beast/core
            beast/crypto
            beast/cxx17
            proto
            validators
            websocket)
        file(GLOB_RECURSE cursrcs src/ripple/${curdir}/*.cpp)
        list(APPEND rippled_headers "${cursrcs}")
    endforeach()
    list(APPEND rippled_src_nonunity "${rippled_headers}")

    set_property(
        SOURCE ${rippled_headers}
        APPEND
        PROPERTY HEADER_FILE_ONLY
        true)
        # Doesn't work
        # $<OR:$<CONFIG:Debug>,$<CONFIG:Release>>)
endif()

if (WIN32 OR is_xcode)
  # Documentation sources. Only needed for IDEs.
  prepend(doc_srcs
    docs/
    source.dox)

  file(GLOB_RECURSE other_docs docs/*.md)
  list(APPEND doc_srcs "${other_docs}")
  set_property(
    SOURCE ${doc_srcs}
    APPEND
    PROPERTY HEADER_FILE_ONLY
    true)
    # Doesn't work
    # $<OR:$<CONFIG:Debug>,$<CONFIG:Release>>)
endif()

############################################################

add_with_props(rippled_src_all src/ripple/unity/soci.cpp
  ${soci_extra_includes})

if (NOT is_msvc)
  set(no_unused_w -Wno-unused-function)
else()
  unset(no_unused_w)
endif()

add_with_props(rippled_src_all src/ripple/unity/secp256k1.cpp
  -I"${CMAKE_SOURCE_DIR}/"src/secp256k1
  ${no_unused_w}
  )

foreach(cursrc
    src/ripple/unity/beast.cpp
    src/ripple/unity/lz4.c
    src/ripple/unity/protobuf.cpp
    src/ripple/unity/ripple.proto.cpp)

  add_with_props(rippled_src_all ${cursrc}
    ${rocks_db_system_header}
    )

endforeach()

if (NOT is_msvc)
  set(extra_props -Wno-array-bounds)
else()
  unset(extra_props)
endif()

add_with_props(rippled_src_all src/sqlite/sqlite_unity.c
  ${extra_props})

add_with_props(rippled_src_all src/ripple/unity/ed25519_donna.c
  -I"${CMAKE_SOURCE_DIR}/"src/ed25519-donna)

if (is_gcc)
  set(no_gcc_warnings -w)
else()
  unset(no_gcc_warnings)
endif()

add_with_props(rippled_src_all src/ripple/unity/rocksdb.cpp
  -I"${CMAKE_SOURCE_DIR}/"src/rocksdb2
  -I"${CMAKE_SOURCE_DIR}/"src/rocksdb2/include
  -I"${CMAKE_SOURCE_DIR}/"src/snappy/snappy
  -I"${CMAKE_SOURCE_DIR}/"src/snappy/config
  ${no_gcc_warnings} ${rocks_db_system_header})

if (NOT is_msvc)
  set(no_unused_w -Wno-unused-function)
endif()

add_with_props(rippled_src_all src/ripple/unity/snappy.cpp
  -I"${CMAKE_SOURCE_DIR}/"src/snappy/snappy
  -I"${CMAKE_SOURCE_DIR}/"src/snappy/config
  ${no_unused_w})

list(APPEND rippled_src_unity "${rippled_src_all}")
list(APPEND rippled_src_nonunity "${rippled_src_all}")

############################################################

if (WIN32 OR is_xcode)
  group_sources(src)
  group_sources(docs)
  group_sources(Builds)
endif()

if(unity)
    add_executable(rippled ${rippled_src_unity} ${PROTO_HDRS})
    add_executable(rippled_classic EXCLUDE_FROM_ALL ${rippled_src_nonunity} ${PROTO_HDRS})
    set(other_target rippled_classic)
else()
    add_executable(rippled ${rippled_src_nonunity} ${PROTO_HDRS})
    add_executable(rippled_unity EXCLUDE_FROM_ALL ${rippled_src_unity} ${PROTO_HDRS})
    set(other_target rippled_unity)
endif()
list(APPEND targets "rippled")
list(APPEND targets ${other_target})
# Not the same as EXCLUDE_FROM_ALL. Prevents Visual Studio from building the
# other_target when the user builds the solution (default when pressing <F7>)
set_property(TARGET ${other_target} PROPERTY EXCLUDE_FROM_DEFAULT_BUILD true)

find_package(Doxygen)
if(TARGET Doxygen::doxygen)
    # read the source config and make a modified one
    # that points the output files to our build directory
    FILE(READ "${CMAKE_SOURCE_DIR}/docs/source.dox" dox_content)
    string(REGEX REPLACE "[\t ]*OUTPUT_DIRECTORY[\t ]*=(.*)"
        "OUTPUT_DIRECTORY=${CMAKE_BINARY_DIR}\n\\1"
        new_config "${dox_content}")
    FILE(WRITE "${CMAKE_BINARY_DIR}/source.dox" "${new_config}")
    add_custom_target(docs
        COMMAND "${DOXYGEN_EXECUTABLE}" "${CMAKE_BINARY_DIR}/source.dox"
        BYPRODUCTS "${CMAKE_BINARY_DIR}/html_doc/index.html"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/docs"
        SOURCES "${doc_srcs}"
    )
else()
    message(WARNING
        "doxygen executable not found. docs target will not be buildable")
    if(${CMAKE_VERSION} VERSION_LESS "3.9.0")
        message("...consider updating to CMake 3.9.0 or greater for better doxygen support")
    endif()
endif()

set_startup_project(rippled)

foreach(target IN LISTS targets)
    target_link_libraries(${target}
      ${OPENSSL_LIBRARIES} ${PROTOBUF_LIBRARIES} ${SANITIZER_LIBRARIES})

    link_common_libraries(${target})
endforeach()

if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(WARNING "Rippled requires a 64 bit target architecture.\n"
    "The most likely cause of this warning is trying to build rippled with a 32-bit OS.")
endif()
