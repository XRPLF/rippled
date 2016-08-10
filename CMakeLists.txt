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

if (NOT target AND NOT CMAKE_BUILD_TYPE)
  if (APPLE)
    set(target clang.debug)
  else()
    set(target gcc.debug)
  endif()
endif()

if (target)
  # Parse the target
  set(remaining ${target})
  while (remaining)
    # get the component up to the next dot or end
    string(REGEX REPLACE "^\\.?([^\\.]+).*$" "\\1" cur_component ${remaining})
    string(REGEX REPLACE "^\\.?[^\\.]+(.*$)" "\\1" remaining ${remaining})

    if (${cur_component} STREQUAL gcc)
      if (DEFINED ENV{GNU_CC})
        set(CMAKE_C_COMPILER $ENV{GNU_CC})
      elseif ($ENV{CXX} MATCHES .*gcc.*)
        set(CMAKE_CXX_COMPILER $ENV{CC})
      else()
        find_program(CMAKE_C_COMPILER gcc)
      endif()

      if (DEFINED ENV{GNU_CXX})
        set(CMAKE_C_COMPILER $ENV{GNU_CXX})
      elseif ($ENV{CXX} MATCHES .*g\\+\\+.*)
        set(CMAKE_C_COMPILER $ENV{CC})
      else()
        find_program(CMAKE_CXX_COMPILER g++)
      endif()
    endif()

    if (${cur_component} STREQUAL clang)
      if (DEFINED ENV{CLANG_CC})
        set(CMAKE_C_COMPILER $ENV{CLANG_CC})
      elseif ($ENV{CXX} MATCHES .*clang.*)
        set(CMAKE_CXX_COMPILER $ENV{CC})
      else()
        find_program(CMAKE_C_COMPILER clang)
      endif()

      if (DEFINED ENV{CLANG_CXX})
        set(CMAKE_C_COMPILER $ENV{CLANG_CXX})
      elseif ($ENV{CXX} MATCHES .*clang.*)
        set(CMAKE_C_COMPILER $ENV{CC})
      else()
        find_program(CMAKE_CXX_COMPILER clang++)
      endif()
    endif()

    if (${cur_component} STREQUAL msvc)
      # TBD
    endif()

    if (${cur_component} STREQUAL unity)
      set(unity true)
      set(nonunity false)
    endif()

    if (${cur_component} STREQUAL nounity)
      set(unity false)
      set(nonunity true)
    endif()

    if (${cur_component} STREQUAL debug)
      set(release false)
    endif()

    if (${cur_component} STREQUAL release)
      set(release true)
    endif()

    if (${cur_component} STREQUAL coverage)
      set(coverage true)
      set(debug true)
    endif()

    if (${cur_component} STREQUAL profile)
      set(profile true)
    endif()

  endwhile()

  if (release)
    set(CMAKE_BUILD_TYPE Release)
  else()
    set(CMAKE_BUILD_TYPE Debug)
  endif()

  if (NOT unity)
    set(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE}Classic)
  endif()

endif()

if (NOT DEFINED unity)
  set(unity true)
  set(nonunity false)
endif()

set(san "" CACHE STRING "On gcc & clang, add sanitizer instrumentation")
set_property(CACHE san PROPERTY STRINGS ";address;thread")
set(assert false CACHE BOOL "Enables asserts, even in release builds")
set(static false CACHE BOOL
  "On linux, link protobuf, openssl, libc++, and boost statically")

if (static AND (WIN32 OR APPLE))
  message(FATAL_ERROR "Static linking is only supported on linux.")
endif()

if (${CMAKE_GENERATOR} STREQUAL "Unix Makefiles" AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# Can't exclude files from configurations, so can't support both
# unity and nonunity configurations at the same time
if (unity)
  set(CMAKE_CONFIGURATION_TYPES
    Debug
    Release)
else()
  set(CMAKE_CONFIGURATION_TYPES
    DebugClassic
    ReleaseClassic)
endif()

set(CMAKE_CONFIGURATION_TYPES
  ${CMAKE_CONFIGURATION_TYPES} CACHE STRING "" FORCE)

project(rippled)

############################################################

function(prepend var prefix)
  set(listVar "")
  foreach(f ${ARGN})
    list(APPEND listVar "${prefix}${f}")
  endforeach(f)
  set(${var} "${listVar}" PARENT_SCOPE)
endfunction()

macro(append_flags name)
  foreach (arg ${ARGN})
    set(${name} "${${name}} ${arg}")
  endforeach()
endmacro()

macro(group_sources curdir)
  file(GLOB children RELATIVE ${PROJECT_SOURCE_DIR}/${curdir}
    ${PROJECT_SOURCE_DIR}/${curdir}/*)
  foreach (child ${children})
    if (IS_DIRECTORY ${PROJECT_SOURCE_DIR}/${curdir}/${child})
      group_sources(${curdir}/${child})
    else()
      string(REPLACE "/" "\\" groupname ${curdir})
      source_group(${groupname} FILES
        ${PROJECT_SOURCE_DIR}/${curdir}/${child})
    endif()
  endforeach()
endmacro()

macro(add_with_props files)
  list(APPEND src ${files})
  foreach (arg ${ARGN})
    set(props "${props} ${arg}")
  endforeach()
  set_source_files_properties(
    ${files}
    PROPERTIES COMPILE_FLAGS
    ${props})
endmacro()

############################################################

if ("${CMAKE_CXX_COMPILER_ID}" MATCHES ".*Clang") # both Clang and AppleClang
  set(is_clang true)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  set(is_gcc true)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
  set(is_msvc true)
endif()

if (${CMAKE_GENERATOR} STREQUAL "Xcode")
  set(is_xcode true)
else()
  set(is_xcode false)
endif()

if (NOT is_gcc AND NOT is_clang AND NOT is_msvc)
  message("Current compiler is ${CMAKE_CXX_COMPILER_ID}")
  message(FATAL_ERROR "Missing compiler. Must be GNU, Clang, or MSVC")
endif()

############################################################
# Check if should use gcc4's ABI
set(gcc4_abi false)

if ($ENV{RIPPLED_OLD_GCC_ABI})
  set(gcc4_abi true)
endif()

if (is_gcc AND NOT gcc4_abi)
  if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 5)
    execute_process(COMMAND lsb_release -si OUTPUT_VARIABLE lsb)
    string(STRIP ${lsb} lsb)
    if (${lsb} STREQUAL "Ubuntu")
      execute_process(COMMAND lsb_release -sr OUTPUT_VARIABLE lsb)
      string(STRIP ${lsb} lsb)
      if (${lsb} VERSION_LESS 15.1)
        set(gcc4_abi true)
      endif()
    endif()
  endif()
endif()

if (gcc4_abi)
  add_definitions(-D_GLIBCXX_USE_CXX11_ABI=0)
endif()

############################################################

include_directories(src src/beast src/beast/include src/beast/extras
  src/soci/src src/soci/include)

if (coverage)
  add_compile_options(-fprofile-arcs -ftest-coverage)
  append_flags(CMAKE_EXE_LINKER_FLAGS -fprofile-arcs -ftest-coverage)
endif()

if (profile)
  add_compile_options(-p -pg)
  append_flags(CMAKE_EXE_LINKER_FLAGS -p -pg)
endif()

############################################################

if (NOT WIN32)
  if (is_clang AND DEFINED ENV{CLANG_BOOST_ROOT})
    set(BOOST_ROOT $ENV{CLANG_BOOST_ROOT})
  endif()


  set(Boost_USE_STATIC_LIBS on)
  set(Boost_USE_MULTITHREADED on)
  set(Boost_USE_STATIC_RUNTIME off)
  find_package(Boost COMPONENTS
    coroutine
    context
    date_time
    filesystem
    program_options
    regex
    system
    thread)

  if (Boost_FOUND)
    include_directories(SYSTEM ${Boost_INCLUDE_DIRS})
  else()
    message(FATAL_ERROR "Boost not found")
  endif()
endif()

if (APPLE)
  # swd TBD fixme
  file(GLOB OPENSSL_ROOT_DIR /usr/local/Cellar/openssl/*)
  # set(OPENSSL_ROOT_DIR /usr/local/Cellar/openssl)
endif()

if (WIN32)
  if (DEFINED ENV{OPENSSL_ROOT})
    include_directories($ENV{OPENSSL_ROOT}/include)
    link_directories($ENV{OPENSSL_ROOT}/lib)
  endif()
else()
  if (static)
    set(tmp CMAKE_FIND_LIBRARY_SUFFIXES)
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
  endif()

  find_package(OpenSSL)

  if (static)
    set(CMAKE_FIND_LIBRARY_SUFFIXES tmp)
  endif()

  if (OPENSSL_FOUND)
    include_directories(${OPENSSL_INCLUDE_DIR})
  else()
    message(FATAL_ERROR "OpenSSL not found")
  endif()
  if (UNIX AND NOT APPLE AND ${OPENSSL_VERSION} VERSION_LESS 1.0.2)
    message(FATAL_ERROR
      "Your openssl is Version: ${OPENSSL_VERSION}, rippled requires 1.0.2 or better.")
  endif()
endif()

if (WIN32)
  if (DEFINED ENV{PROTOBUF_ROOT})
    include_directories($ENV{PROTOBUF_ROOT}/src)
    link_directories($ENV{PROTOBUF_ROOT}/src/.libs)
  endif()

  # Modified from FindProtobuf.cmake
  FUNCTION(PROTOBUF_GENERATE_CPP SRCS HDRS PROTOFILES)
    # argument parsing
    IF(NOT PROTOFILES)
      MESSAGE(SEND_ERROR "Error: PROTOBUF_GENERATE_CPP() called without any proto files")
      RETURN()
    ENDIF()

    SET(OUTPATH ${CMAKE_CURRENT_BINARY_DIR})
    SET(PROTOROOT ${CMAKE_CURRENT_SOURCE_DIR})
    # the real logic
    SET(${SRCS})
    SET(${HDRS})
    FOREACH(PROTOFILE ${PROTOFILES})
      # ensure that the file ends with .proto
      STRING(REGEX MATCH "\\.proto$$" PROTOEND ${PROTOFILE})
      IF(NOT PROTOEND)
        MESSAGE(SEND_ERROR "Proto file '${PROTOFILE}' does not end with .proto")
      ENDIF()

      GET_FILENAME_COMPONENT(PROTO_PATH ${PROTOFILE} PATH)
      GET_FILENAME_COMPONENT(ABS_FILE ${PROTOFILE} ABSOLUTE)
      GET_FILENAME_COMPONENT(FILE_WE ${PROTOFILE} NAME_WE)

      STRING(REGEX MATCH "^${PROTOROOT}" IN_ROOT_PATH ${PROTOFILE})
      STRING(REGEX MATCH "^${PROTOROOT}" IN_ROOT_ABS_FILE ${ABS_FILE})

      IF(IN_ROOT_PATH)
        SET(MATCH_PATH ${PROTOFILE})
      ELSEIF(IN_ROOT_ABS_FILE)
        SET(MATCH_PATH ${ABS_FILE})
      ELSE()
        MESSAGE(SEND_ERROR "Proto file '${PROTOFILE}' is not in protoroot '${PROTOROOT}'")
      ENDIF()

      # build the result file name
      STRING(REGEX REPLACE "^${PROTOROOT}(/?)" "" ROOT_CLEANED_FILE ${MATCH_PATH})
      STRING(REGEX REPLACE "\\.proto$$" "" EXT_CLEANED_FILE ${ROOT_CLEANED_FILE})

      SET(CPP_FILE "${OUTPATH}/${EXT_CLEANED_FILE}.pb.cc")
      SET(H_FILE "${OUTPATH}/${EXT_CLEANED_FILE}.pb.h")

      LIST(APPEND ${SRCS} "${CPP_FILE}")
      LIST(APPEND ${HDRS} "${H_FILE}")

      ADD_CUSTOM_COMMAND(
        OUTPUT "${CPP_FILE}" "${H_FILE}"
        COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPATH}
        COMMAND ${PROTOBUF_PROTOC_EXECUTABLE}
        ARGS "--cpp_out=${OUTPATH}" --proto_path "${PROTOROOT}" "${MATCH_PATH}"
        DEPENDS ${ABS_FILE}
        COMMENT "Running C++ protocol buffer compiler on ${MATCH_PATH} with root ${PROTOROOT}, generating: ${CPP_FILE}"
        VERBATIM)

    ENDFOREACH()

    SET_SOURCE_FILES_PROPERTIES(${${SRCS}} ${${HDRS}} PROPERTIES GENERATED TRUE)
    SET(${SRCS} ${${SRCS}} PARENT_SCOPE)
    SET(${HDRS} ${${HDRS}} PARENT_SCOPE)

  ENDFUNCTION()

  set(PROTOBUF_PROTOC_EXECUTABLE Protoc) # must be on path
else()
  if (static)
    set(tmp CMAKE_FIND_LIBRARY_SUFFIXES)
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
  endif()

  find_package(Protobuf REQUIRED)

  if (static)
    set(CMAKE_FIND_LIBRARY_SUFFIXES tmp)
  endif()

  if (is_clang AND DEFINED ENV{CLANG_PROTOBUF_ROOT})
    link_directories($ENV{CLANG_PROTOBUF_ROOT}/src/.libs)
    include_directories($ENV{CLANG_PROTOBUF_ROOT}/src)
  else()
    include_directories(${PROTOBUF_INCLUDE_DIRS})
  endif()
endif()
include_directories(${CMAKE_CURRENT_BINARY_DIR})


############################################################

if (NOT WIN32 AND san)
  add_compile_options(-fsanitize=${san} -fno-omit-frame-pointer)

  append_flags(CMAKE_EXE_LINKER_FLAGS
    -fsanitize=${san})

  string(TOLOWER ${san} ci_san)
  if (${ci_san} STREQUAL address)
    set(SANITIZER_LIBRARIES asan)
    add_definitions(-DSANITIZER=ASAN)
  endif()
  if (${ci_san} STREQUAL thread)
    set(SANITIZER_LIBRARIES tsan)
    add_definitions(-DSANITIZER=TSAN)
  endif()
endif()

############################################################

file(GLOB ripple_proto src/ripple/proto/*.proto)
PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS ${ripple_proto})

if (WIN32)
  include_directories(src/protobuf/src
    src/protobuf/vsprojects
    ${CMAKE_CURRENT_BINARY_DIR}/src/ripple/proto)
endif()

add_definitions(
  -DOPENSSL_NO_SSL2
  -DDEPRECATED_IN_MAC_OS_X_VERSION_10_7_AND_LATER
  -DHAVE_USLEEP=1
  -DSOCI_CXX_C11=1
  -D_SILENCE_STDEXT_HASH_DEPRECATION_WARNINGS
  -DBOOST_NO_AUTO_PTR
  )

if (is_gcc)
  add_compile_options(-Wno-unused-but-set-variable -Wno-deprecated)
endif()

# Generator expressions are not supported in add_definitions, use set_property instead
set_property(
  DIRECTORY
  APPEND
  PROPERTY COMPILE_DEFINITIONS
  $<$<OR:$<CONFIG:Debug>,$<CONFIG:DebugClassic>>:DEBUG _DEBUG>)

if (NOT assert)
  set_property(
    DIRECTORY
    APPEND
    PROPERTY COMPILE_DEFINITIONS
    $<$<OR:$<BOOL:${profile}>,$<CONFIG:Release>,$<CONFIG:ReleaseClassic>>:NDEBUG>)
endif()

if (NOT WIN32)
  add_definitions(-D_FILE_OFFSET_BITS=64)
  append_flags(CMAKE_CXX_FLAGS -frtti -std=c++14 -Wno-invalid-offsetof)
  add_compile_options(-Wall -Wno-sign-compare -Wno-char-subscripts -Wno-format
    -Wno-unused-local-typedefs -g)
  # There seems to be an issue using generator experssions with multiple values,
  # split the expression
  add_compile_options($<$<OR:$<CONFIG:Release>,$<CONFIG:ReleaseClassic>>:-O3>)
  add_compile_options($<$<OR:$<CONFIG:Release>,$<CONFIG:ReleaseClassic>>:-fno-strict-aliasing>)
  append_flags(CMAKE_EXE_LINKER_FLAGS -rdynamic -g)

  if (is_clang)
    add_compile_options(
      -Wno-redeclared-class-member -Wno-mismatched-tags -Wno-deprecated-register)
    add_definitions(-DBOOST_ASIO_HAS_STD_ARRAY)
  endif()

  if (APPLE)
    add_definitions(-DBEAST_COMPILE_OBJECTIVE_CPP=1
      -DNO_LOG_UNHANDLED_EXCEPTIONS)
    add_compile_options(
      -Wno-deprecated -Wno-deprecated-declarations -Wno-unused-variable -Wno-unused-function)
  endif()

  if (is_gcc)
    add_compile_options(-Wno-unused-but-set-variable -Wno-unused-local-typedefs)
    add_compile_options($<$<OR:$<CONFIG:Debug>,$<CONFIG:DebugClassic>>:-O0>)
  endif (is_gcc)
else(NOT WIN32)
  add_compile_options(
    /bigobj              # Increase object file max size
    /EHa                 # ExceptionHandling all
    /fp:precise          # Floating point behavior
    /Gd                  # __cdecl calling convention
    /Gm-                 # Minimal rebuild: disabled
    /GR                  # Enable RTTI
    /Gy-                 # Function level linking: disabled
    /FS
    /MP                  # Multiprocessor compilation
    /openmp-             # pragma omp: disabled
    /Zc:forScope         # Language extension: for scope
    /Zi                  # Generate complete debug info
    /errorReport:none    # No error reporting to Internet
    /nologo              # Suppress login banner
    /W3                  # Warning level 3
    /WX-                 # Disable warnings as errors
    /wd"4018"
    /wd"4244"
    /wd"4267"
    /wd"4800"            # Disable C4800(int to bool performance)
    /wd"4503"            # Decorated name length exceeded, name was truncated
    )
  add_definitions(
    -D_WIN32_WINNT=0x6000
    -D_SCL_SECURE_NO_WARNINGS
    -D_CRT_SECURE_NO_WARNINGS
    -DWIN32_CONSOLE
    -DNOMINMAX)
  append_flags(CMAKE_EXE_LINKER_FLAGS
    /DEBUG
    /DYNAMICBASE
    /ERRORREPORT:NONE
    /MACHINE:X64
    /MANIFEST
    /nologo
    /NXCOMPAT
    /SUBSYSTEM:CONSOLE
    /TLBID:1)


  # There seems to be an issue using generator experssions with multiple values,
  # split the expression
  # /GS  Buffers security check: enable
  add_compile_options($<$<OR:$<CONFIG:Debug>,$<CONFIG:DebugClassic>>:/GS>)
  # /MTd Language: Multi-threaded Debug CRT
  add_compile_options($<$<OR:$<CONFIG:Debug>,$<CONFIG:DebugClassic>>:/MTd>)
  # /Od  Optimization: Disabled
  add_compile_options($<$<OR:$<CONFIG:Debug>,$<CONFIG:DebugClassic>>:/Od>)
  # /RTC1 Run-time error checks:
  add_compile_options($<$<OR:$<CONFIG:Debug>,$<CONFIG:DebugClassic>>:/RTC1>)

# Generator expressions are not supported in add_definitions, use set_property instead
set_property(
  DIRECTORY
  APPEND
  PROPERTY COMPILE_DEFINITIONS
  $<$<OR:$<CONFIG:Debug>,$<CONFIG:DebugClassic>>:_CRTDBG_MAP_ALLOC>)

  # /MT Language: Multi-threaded CRT
  add_compile_options($<$<OR:$<CONFIG:Release>,$<CONFIG:ReleaseClassic>>:/MT>)
  add_compile_options($<$<OR:$<CONFIG:Release>,$<CONFIG:ReleaseClassic>>:/Ox>)
  # /Ox Optimization: Full

endif (NOT WIN32)

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

if (WIN32 OR is_xcode OR unity)
  prepend(beast_unity_srcs
    src/ripple/beast/unity/
    beast_clock_unity.cpp
    beast_container_unity.cpp
    beast_insight_unity.cpp
    beast_net_unity.cpp
    beast_nudb_unity.cpp
    beast_utility_unity.cpp)

  prepend(ripple_unity_srcs
    src/ripple/unity/
    app_ledger.cpp
    app_main.cpp
    app_misc.cpp
    app_paths.cpp
    app_tests.cpp
    app_tx.cpp
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
    server.cpp
    test.cpp)

  list(APPEND src ${beast_unity_srcs} ${ripple_unity_srcs})

  add_with_props(src/ripple/unity/nodestore.cpp
    -I"${CMAKE_SOURCE_DIR}/"src/rocksdb2/include
    -I"${CMAKE_SOURCE_DIR}/"src/snappy/snappy
    -I"${CMAKE_SOURCE_DIR}/"src/snappy/config
    ${rocks_db_system_header})

  add_with_props(src/ripple/unity/soci_ripple.cpp ${soci_extra_includes})

  set(unity_srcs ${beast_unity_srcs} ${ripple_unity_srcs}
    src/ripple/unity/nodestore.cpp
    src/ripple/unity/soci_ripple.cpp)

  set_property(
    SOURCE ${unity_srcs}
    APPEND
    PROPERTY HEADER_FILE_ONLY
    ${nonunity})
  # Doesn't work
  # $<OR:$<CONFIG:DebugClassic>,$<CONFIG:ReleaseClassic>>)

endif ()

if (WIN32 OR is_xcode OR NOT unity)
  file(GLOB_RECURSE core_srcs src/ripple/core/*.cpp)
  add_with_props("${core_srcs}"
    -I"${CMAKE_SOURCE_DIR}/"src/soci/src/core
    -I"${CMAKE_SOURCE_DIR}/"src/sqlite)

  set(non_unity_srcs ${core_srcs})

  foreach(curdir
      beast/clock beast/container beast/insight beast/net beast/nudb beast/utility
      app basics crypto json ledger legacy net overlay peerfinder protocol rpc
      shamap server test)
    file(GLOB_RECURSE cursrcs src/ripple/${curdir}/*.cpp)
    list(APPEND src "${cursrcs}")
    list(APPEND non_unity_srcs "${cursrcs}")
  endforeach()

  file(GLOB_RECURSE nodestore_srcs src/ripple/nodestore/*.cpp)

  add_with_props("${nodestore_srcs}"
    -I"${CMAKE_SOURCE_DIR}/"src/rocksdb2/include
    -I"${CMAKE_SOURCE_DIR}/"src/snappy/snappy
    -I"${CMAKE_SOURCE_DIR}/"src/snappy/config
    ${rocks_db_system_header})

  list(APPEND non_unity_srcs "${nodestore_srcs}")

  set_property(
    SOURCE ${non_unity_srcs}
    APPEND
    PROPERTY HEADER_FILE_ONLY
    ${unity})
    # Doesn't work
    # $<OR:$<CONFIG:Debug>,$<CONFIG:Release>>)
endif()

############################################################

add_with_props(src/ripple/unity/soci.cpp
  ${soci_extra_includes})

if (NOT is_msvc)
  set(no_unused_w -Wno-unused-function)
else()
  unset(no_unused_w)
endif()

add_with_props(src/ripple/unity/secp256k1.cpp
  -I"${CMAKE_SOURCE_DIR}/"src/secp256k1
  ${no_unused_w}
  )

foreach(cursrc
    src/ripple/beast/unity/beast_hash_unity.cpp
    src/ripple/unity/beast.cpp
    src/ripple/unity/lz4.c
    src/ripple/unity/protobuf.cpp
    src/ripple/unity/ripple.proto.cpp
    src/ripple/unity/resource.cpp
    src/ripple/unity/websocket02.cpp)

  add_with_props(${cursrc}
    ${rocks_db_system_header}
    )

endforeach()

if (NOT is_msvc)
  set(extra_props -Wno-array-bounds)
else()
  unset(extra_props)
endif()

add_with_props(src/sqlite/sqlite_unity.c
  ${extra_props})

if (NOT is_msvc)
  set_source_files_properties(src/ripple/unity/beastc.c
    PROPERTIES COMPILE_FLAGS
    -Wno-array-bounds)
endif()

add_with_props(src/ripple/unity/ed25519.c
  -I"${CMAKE_SOURCE_DIR}/"src/ed25519-donna)

if (is_gcc)
  set(no_init_w -Wno-maybe-uninitialized)
else()
  unset(no_init_w)
endif()

add_with_props(src/ripple/unity/rocksdb.cpp
  -I"${CMAKE_SOURCE_DIR}/"src/rocksdb2
  -I"${CMAKE_SOURCE_DIR}/"src/rocksdb2/include
  -I"${CMAKE_SOURCE_DIR}/"src/snappy/snappy
  -I"${CMAKE_SOURCE_DIR}/"src/snappy/config
  ${no_init_w} ${rocks_db_system_header})

if (NOT is_msvc)
  set(no_unused_w -Wno-unused-function)
endif()

add_with_props(src/ripple/unity/snappy.cpp
  -I"${CMAKE_SOURCE_DIR}/"src/snappy/snappy
  -I"${CMAKE_SOURCE_DIR}/"src/snappy/config
  ${no_unused_w})

if (APPLE AND is_clang)
  list(APPEND src src/ripple/unity/beastobjc.mm)
endif()

############################################################

if (WIN32 OR is_xcode)
  group_sources(src)
endif()

add_executable(rippled ${src} ${PROTO_HDRS})

if (static)
  append_flags(CMAKE_EXE_LINKER_FLAGS -static-libstdc++)
  # set_target_properties(rippled PROPERTIES LINK_SEARCH_START_STATIC 1)
  # set_target_properties(rippled PROPERTIES LINK_SEARCH_END_STATIC 1)
endif()

if (WIN32)
  if (CMAKE_VERSION VERSION_LESS 3.6)
    message(WARNING
      "Setting the VS startup project requires cmake 3.6 or later. Please upgrade.")
  endif()
  set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT rippled)
endif()

target_link_libraries(rippled
  ${Boost_LIBRARIES} ${OPENSSL_LIBRARIES} ${PROTOBUF_LIBRARIES} ${SANITIZER_LIBRARIES})


if (NOT WIN32)
  target_link_libraries(rippled dl)
  if (APPLE)
    find_library(app_kit AppKit)
    find_library(foundation Foundation)
    target_link_libraries(rippled
      crypto ssl ${app_kit} ${foundation})
  else()
    target_link_libraries(rippled rt)
  endif()
else(NOT WIN32)
  target_link_libraries(rippled
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:DebugClassic>>:VC/static/ssleay32MTd>
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:DebugClassic>>:VC/static/libeay32MTd>)
  target_link_libraries(rippled
    $<$<OR:$<CONFIG:Release>,$<CONFIG:ReleaseClassic>>:VC/static/ssleay32MT>
    $<$<OR:$<CONFIG:Release>,$<CONFIG:ReleaseClassic>>:VC/static/libeay32MT>)
  target_link_libraries(rippled
    legacy_stdio_definitions.lib Shlwapi kernel32 user32 gdi32 winspool comdlg32
    advapi32 shell32 ole32 oleaut32 uuid odbc32 odbccp32)
endif (NOT WIN32)
