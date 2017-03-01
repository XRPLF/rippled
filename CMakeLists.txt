# !!! The official build system is SConstruct !!!
# This is an experimental cmake build file for rippled
#
# cmake support in rippled. Currently supports:
#
#  * unity/nounity debug/release
#  * running protobuf
#  * sanitizer builds
#  * optional release build with assert turned on
#  * `target` variable to easily set compiler/debug/unity
#     (i.e. -Dtarget=gcc.debug.nounity)
#  * gcc/clang/visual studio/xcode
#  * linux/mac/win
#  * gcc 4 ABI, when needed
#  * ninja builds
#  * check openssl version on linux
#  * static builds (swd TBD: needs to be tested by building & deploying on different systems)
#
# TBD:
#  * jemalloc support
#  * count
#  * Windows protobuf compiler puts generated file in src directory instead of build directory.
#
# Notes:
#  * Use the -G"Visual Studio 14 2015 Win64" generator on Windows. Without this
#    a 32-bit project will be created. There is no way to set the generator or
#    force a 64-bit build in CMakeLists.txt (setting CMAKE_GENERATOR_PLATFORM won't work).
#    The best solution may be to wrap cmake with a script.
#
#  * It is not possible to generate a visual studio project on linux or
#    mac. The visual studio generator is only available on windows.
#
#  * The visual studio project can be _either_ unity or
#    non-unity (selected at generation time).  It does not appear possible
#    to disable compilation based on configuration.
#
#  * Language is _much_ worse than python, poor documentation and "quirky"
#    language support (for example, generator expressions can only be used
#    in limited contexts and seem to work differently based on
#    context (set_property can set multiple values, add_compile_options
#    can not/or is buggy)
#
#  * Could not call out to `sed` because cmake messed with the regular
#    expression before calling the external command. I did not see a way
#    around this.
#
#  * Makefile generators want to be single target. It wants a separate
#    directory for each target type. I saw some mentions on the web for
#    ways around this bug haven't look into it. The visual studio project
#    does support debug/release configurations in the same project (but
#    not unity/non-unity).

############################################################

cmake_minimum_required(VERSION 3.1.0)

# The project command can override some computed values.
# Don't put any code above this line.
project(rippled)

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

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/Builds/CMake")
include(CMakeFuncs)

set(openssl_min 1.0.2)

parse_target()

if (NOT DEFINED unity)
  set(unity true)
  set(nonunity false)
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
beast_utility_unity.cpp)

prepend(ripple_unity_srcs
src/ripple/unity/
app_consensus.cpp
app_ledger.cpp
app_main.cpp
app_misc.cpp
app_paths.cpp
app_tx.cpp
conditions.cpp
consensus.cpp
core.cpp
basics.cpp
crypto.cpp
ledger.cpp
net.cpp
overlay.cpp
peerfinder.cpp
json.cpp
protocol.cpp
rpcx.cpp
shamap.cpp
server.cpp)

prepend(test_unity_srcs
src/test/unity/
app_test_unity.cpp
basics_test_unity.cpp
beast_test_unity.cpp
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
shamap_test_unity.cpp
jtx_unity.cpp
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
        core
        json
        ledger
        nodestore
        overlay
        peerfinder
        protocol
        resource
        rpc
        server
        shamap
        jtx
        csf)
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
    file(GLOB_RECURSE rippled_headers src/*.h src/*.hpp)
    list(APPEND rippled_headers Builds/CMake/CMakeFuncs.cmake)
    foreach(curdir
            beast/asio
            beast/core
            beast/crypto
            beast/cxx17
            beast/hash
            proto
            resource
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
    Jamfile.v2
    boostbook.dtd
    index.xml
    main.qbk
    quickref.xml
    reference.xsl
    source.dox)

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
    src/ripple/beast/unity/beast_hash_unity.cpp
    src/ripple/unity/beast.cpp
    src/ripple/unity/lz4.c
    src/ripple/unity/protobuf.cpp
    src/ripple/unity/ripple.proto.cpp
    src/ripple/unity/resource.cpp)

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
  set(no_init_w -Wno-maybe-uninitialized)
else()
  unset(no_init_w)
endif()

add_with_props(rippled_src_all src/ripple/unity/rocksdb.cpp
  -I"${CMAKE_SOURCE_DIR}/"src/rocksdb2
  -I"${CMAKE_SOURCE_DIR}/"src/rocksdb2/include
  -I"${CMAKE_SOURCE_DIR}/"src/snappy/snappy
  -I"${CMAKE_SOURCE_DIR}/"src/snappy/config
  ${no_init_w} ${rocks_db_system_header})

if (NOT is_msvc)
  set(no_unused_w -Wno-unused-function)
endif()

add_with_props(rippled_src_all src/ripple/unity/snappy.cpp
  -I"${CMAKE_SOURCE_DIR}/"src/snappy/snappy
  -I"${CMAKE_SOURCE_DIR}/"src/snappy/config
  ${no_unused_w})

if (APPLE AND is_clang)
  list(APPEND rippled_src_all src/ripple/unity/beastobjc.mm)
endif()

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

find_program(
  B2_EXE
  NAMES b2
  HINTS ${BOOST_ROOT}
  PATHS ${BOOST_ROOT}
  DOC "Location of the b2 build executable from Boost")
if(${B2_EXE} STREQUAL "B2_EXE-NOTFOUND")
  message(WARNING
    "Boost b2 executable not found. docs target will not be buildable")
elseif(NOT BOOST_ROOT)
    if(Boost_INCLUDE_DIRS)
        set(BOOST_ROOT ${Boost_INCLUDE_DIRS})
    else()
        get_filename_component(BOOST_ROOT ${B2_EXE} DIRECTORY)
    endif()
endif()
# The value for BOOST_ROOT will be determined based on
#   1) The environment BOOST_ROOT
#   2) The Boost_INCLUDE_DIRS found by `get_boost`
#   3) The folder the `b2` executable is found in.
# If those checks don't yield the correct path, BOOST_ROOT
# can be defined on the cmake command line:
#    cmake <path> -DBOOST_ROOT=<boost_path>
if(BOOST_ROOT)
    set(B2_PARAMS "-sBOOST_ROOT=${BOOST_ROOT}")
endif()

# Find bash to help Windows avoid file association problems
find_program(
    BASH_EXE
    NAMES bash sh
    DOC "Location of the bash shell executable"
    )
if(${BASH_EXE} STREQUAL "BASH_EXE-NOTFOUND")
    message(WARNING
        "Unable to find bash executable. docs target may not be buildable")
    set(BASH_EXE "")
endif()

add_custom_target(docs
  COMMAND ${CMAKE_COMMAND} -E env "PATH=$ENV{PATH} " ${BASH_EXE} ./makeqbk.sh
  COMMAND ${B2_EXE} ${B2_PARAMS}
  BYPRODUCTS "${CMAKE_SOURCE_DIR}/docs/html/index.html"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/docs"
  SOURCES "${doc_srcs}"
  )

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
