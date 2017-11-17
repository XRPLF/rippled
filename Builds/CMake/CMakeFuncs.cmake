# This is a set of common functions and settings for rippled
# and derived products.

############################################################

cmake_minimum_required(VERSION 3.1.0)

if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
  message(WARNING "Builds are strongly discouraged in "
    "${CMAKE_SOURCE_DIR}.")
endif()

macro(parse_target)

  if (NOT target OR target STREQUAL "default")
    if (NOT CMAKE_BUILD_TYPE)
      set(CMAKE_BUILD_TYPE Debug)
    endif()
    string(TOLOWER ${CMAKE_BUILD_TYPE} target)
    if (APPLE)
      set(target clang.${target})
    elseif(WIN32)
      set(target msvc)
    else()
      set(target gcc.${target})
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
        elseif ($ENV{CC} MATCHES .*gcc.*)
          set(CMAKE_C_COMPILER $ENV{CC})
        else()
          find_program(CMAKE_C_COMPILER gcc)
        endif()

        if (DEFINED ENV{GNU_CXX})
          set(CMAKE_CXX_COMPILER $ENV{GNU_CXX})
        elseif ($ENV{CXX} MATCHES .*g\\+\\+.*)
          set(CMAKE_CXX_COMPILER $ENV{CXX})
        else()
          find_program(CMAKE_CXX_COMPILER g++)
        endif()
      endif()

      if (${cur_component} STREQUAL clang)
        if (DEFINED ENV{CLANG_CC})
          set(CMAKE_C_COMPILER $ENV{CLANG_CC})
        elseif ($ENV{CC} MATCHES .*clang.*)
          set(CMAKE_C_COMPILER $ENV{CC})
        else()
          find_program(CMAKE_C_COMPILER clang)
        endif()

        if (DEFINED ENV{CLANG_CXX})
          set(CMAKE_CXX_COMPILER $ENV{CLANG_CXX})
        elseif ($ENV{CXX} MATCHES .*clang.*)
          set(CMAKE_CXX_COMPILER $ENV{CXX})
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

      if (${cur_component} STREQUAL ci)
        # Workarounds that make various CI builds work, but that
        # we don't want in the general case.
        set(ci true)
        set(openssl_min 1.0.1)
      endif()

    endwhile()
  endif()

  if(CMAKE_C_COMPILER MATCHES "-NOTFOUND$" OR
    CMAKE_CXX_COMPILER MATCHES "-NOTFOUND$")
    message(FATAL_ERROR "Can not find appropriate compiler for target ${target}")
  endif()

  # If defined, promote the compiler path values to the CACHE, then
  # unset the locals to prevent shadowing. Some scenarios do not
  # need or want to find a compiler, such as -GNinja under Windows.
  # Setting these values in those case may prevent CMake from finding
  # a valid compiler.
  if (CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER ${CMAKE_C_COMPILER} CACHE FILEPATH
      "Path to a program" FORCE)
    unset(CMAKE_C_COMPILER)
  endif (CMAKE_C_COMPILER)
  if (CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER ${CMAKE_CXX_COMPILER} CACHE FILEPATH
      "Path to a program" FORCE)
    unset(CMAKE_CXX_COMPILER)
  endif (CMAKE_CXX_COMPILER)

  if (release)
    set(CMAKE_BUILD_TYPE Release)
  else()
    set(CMAKE_BUILD_TYPE Debug)
  endif()

  # ensure that the unity flags are set and exclusive
  if (NOT DEFINED unity OR unity)
    # Default to unity builds
    set(unity true)
    set(nonunity false)
  else()
    set(unity false)
    set(nonunity true)
  endif()

  if (NOT unity)
    set(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE}Classic)
  endif()
  # Promote this value to the CACHE, then unset the local
  # to prevent shadowing.
  set(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE INTERNAL
    "Choose the type of build, options are in CMAKE_CONFIGURATION_TYPES"
    FORCE)
  unset(CMAKE_BUILD_TYPE)

endmacro()

############################################################

macro(setup_build_cache)
  set(san "" CACHE STRING "On gcc & clang, add sanitizer
    instrumentation")
  set_property(CACHE san PROPERTY STRINGS ";address;thread")
  set(assert false CACHE BOOL "Enables asserts, even in release builds")
  set(static false CACHE BOOL
    "On linux, link protobuf, openssl, libc++, and boost statically")
  set(jemalloc false CACHE BOOL "Enables jemalloc for heap profiling")
  set(perf false CACHE BOOL "Enables flags that assist with perf recording")

  if (static AND (WIN32 OR APPLE))
    message(FATAL_ERROR "Static linking is only supported on linux.")
  endif()

  if (perf AND (WIN32 OR APPLE))
    message(FATAL_ERROR "perf flags are only supported on linux.")
  endif()

  if (${CMAKE_GENERATOR} STREQUAL "Unix Makefiles" AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
  endif()

  # Can't exclude files from configurations, so can't support both
  # unity and nonunity configurations at the same time
  if (NOT DEFINED unity OR unity)
    set(CMAKE_CONFIGURATION_TYPES
      Debug
      Release)
  else()
    set(CMAKE_CONFIGURATION_TYPES
      DebugClassic
      ReleaseClassic)
  endif()

  # Promote this value to the CACHE, then unset the local
  # to prevent shadowing.
  set(CMAKE_CONFIGURATION_TYPES
    ${CMAKE_CONFIGURATION_TYPES} CACHE STRING "" FORCE)
  unset(CMAKE_CONFIGURATION_TYPES)
endmacro()

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

macro(group_sources_in source_dir curdir)
  file(GLOB children RELATIVE ${source_dir}/${curdir}
    ${source_dir}/${curdir}/*)
  foreach (child ${children})
    if (IS_DIRECTORY ${source_dir}/${curdir}/${child})
      group_sources_in(${source_dir} ${curdir}/${child})
    else()
      string(REPLACE "/" "\\" groupname ${curdir})
      source_group(${groupname} FILES
        ${source_dir}/${curdir}/${child})
    endif()
  endforeach()
endmacro()

macro(group_sources curdir)
  group_sources_in(${PROJECT_SOURCE_DIR} ${curdir})
endmacro()

macro(add_with_props src_var files)
  list(APPEND ${src_var} ${files})
  foreach (arg ${ARGN})
    set(props "${props} ${arg}")
  endforeach()
  set_source_files_properties(
    ${files}
    PROPERTIES COMPILE_FLAGS
    ${props})
endmacro()

############################################################

macro(determine_build_type)
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
endmacro()

############################################################

macro(check_gcc4_abi)
  # Check if should use gcc4's ABI
  set(gcc4_abi false)

  if ($ENV{RIPPLED_OLD_GCC_ABI})
    set(gcc4_abi true)
  endif()

  if (is_gcc AND NOT gcc4_abi)
    if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 5)
      execute_process(COMMAND lsb_release -si OUTPUT_VARIABLE lsb)
      string(STRIP "${lsb}" lsb)
      if ("${lsb}" STREQUAL "Ubuntu")
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
endmacro()

############################################################

macro(special_build_flags)
  if (coverage)
    add_compile_options(-fprofile-arcs -ftest-coverage)
    append_flags(CMAKE_EXE_LINKER_FLAGS -fprofile-arcs -ftest-coverage)
  endif()

  if (profile)
    add_compile_options(-p -pg)
    append_flags(CMAKE_EXE_LINKER_FLAGS -p -pg)
  endif()
endmacro()

############################################################

# Params: Boost components to search for.
macro(use_boost)
    if ((NOT DEFINED BOOST_ROOT) AND (DEFINED ENV{BOOST_ROOT}))
        set(BOOST_ROOT $ENV{BOOST_ROOT})
    endif()
    file(TO_CMAKE_PATH "${BOOST_ROOT}" BOOST_ROOT)
    if(WIN32 OR CYGWIN)
        # Workaround for MSVC having two boost versions - x86 and x64 on same PC in stage folders
        if(DEFINED BOOST_ROOT)
            if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND IS_DIRECTORY ${BOOST_ROOT}/stage64/lib)
                set(Boost_LIBRARY_DIR ${BOOST_ROOT}/stage64/lib)
            else()
                set(Boost_LIBRARY_DIR ${BOOST_ROOT}/stage/lib)
            endif()
        endif()
    endif()

    if (is_clang AND DEFINED ENV{CLANG_BOOST_ROOT})
      set(BOOST_ROOT $ENV{CLANG_BOOST_ROOT})
    endif()

    set(Boost_USE_STATIC_LIBS on)
    set(Boost_USE_MULTITHREADED on)
    set(Boost_USE_STATIC_RUNTIME off)
    if(MSVC)
        find_package(Boost REQUIRED)
    else()
        find_package(Boost REQUIRED ${ARGN})
    endif()

    if (Boost_FOUND OR
        ((CYGWIN OR WIN32) AND Boost_INCLUDE_DIRS AND Boost_LIBRARY_DIRS))
      if(NOT Boost_FOUND)
        message(WARNING "Boost directory found, but not all components. May not be able to build.")
      endif()
      include_directories(SYSTEM ${Boost_INCLUDE_DIRS})
      link_directories(${Boost_LIBRARY_DIRS})
    else()
      message(FATAL_ERROR "Boost not found")
    endif()
endmacro()

macro(use_pthread)
  if (NOT WIN32)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads)
    add_compile_options(${CMAKE_THREAD_LIBS_INIT})
  endif()
endmacro()

macro(use_openssl openssl_min)
  if (APPLE AND NOT DEFINED ENV{OPENSSL_ROOT_DIR})
    find_program(HOMEBREW brew)
    if (NOT HOMEBREW STREQUAL "HOMEBREW-NOTFOUND")
      execute_process(COMMAND brew --prefix openssl
        OUTPUT_VARIABLE OPENSSL_ROOT_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif()
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
    # depending on how openssl is built, it might depend
    # on zlib. In fact, the openssl find package should
    # figure this out for us, but it does not currently...
    # so let's add zlib ourselves to the lib list
    find_package(ZLIB)

    if (static)
      set(CMAKE_FIND_LIBRARY_SUFFIXES tmp)
    endif()

    if (OPENSSL_FOUND)
      include_directories(${OPENSSL_INCLUDE_DIR})
      list(APPEND OPENSSL_LIBRARIES ${ZLIB_LIBRARIES})
    else()
      message(FATAL_ERROR "OpenSSL not found")
    endif()
    if (UNIX AND NOT APPLE AND ${OPENSSL_VERSION} VERSION_LESS ${openssl_min})
      message(FATAL_ERROR
        "Your openssl is Version: ${OPENSSL_VERSION}, ${openssl_min} or better is required.")
    endif()
  endif()
endmacro()

macro(use_protobuf)
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

  file(GLOB ripple_proto src/ripple/proto/*.proto)
  PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS ${ripple_proto})

  if (WIN32)
    include_directories(src/protobuf/src
      src/protobuf/vsprojects
      ${CMAKE_CURRENT_BINARY_DIR}/src/ripple/proto)
  endif()

endmacro()

############################################################

macro(setup_build_boilerplate)
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

  if (perf)
    add_compile_options(-fno-omit-frame-pointer)
  endif()

  ############################################################

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

    # use gold linker if available
    execute_process(
      COMMAND ${CMAKE_CXX_COMPILER} -fuse-ld=gold -Wl,--version
      ERROR_QUIET OUTPUT_VARIABLE LD_VERSION)
    # NOTE: THE gold linker inserts -rpath as DT_RUNPATH by default
    #  intead of DT_RPATH, so you might have slightly unexpected
    #  runtime ld behavior if you were expecting DT_RPATH.
    #  Specify --disable-new-dtags to gold if you do not want
    #  the default DT_RUNPATH behavior. This rpath treatment as well
    #  as static/dynamic selection means that gold does not currently
    #  have ideal default behavior when we are using jemalloc. Thus
    #  for simplicity we don't use it when jemalloc is requested.
    #  An alternative to disabling would be to figure out all the settings
    #  required to make gold play nicely with jemalloc.
    if (("${LD_VERSION}" MATCHES "GNU gold") AND (NOT jemalloc))
        append_flags(CMAKE_EXE_LINKER_FLAGS -fuse-ld=gold)
    endif ()
    unset(LD_VERSION)
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
  else()
    # CMAKE_CXX_FLAGS_RELEASE is created by CMake for most / all generators
    #  with defaults including /DNDEBUG or -DNDEBUG, and that value is stored
    #  in the cache. Override that locally so that the cache value will be
    #  avaiable if "assert" is ever changed.
    STRING(REGEX REPLACE "[-/]DNDEBUG" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
    STRING(REGEX REPLACE "[-/]DNDEBUG" "" CMAKE_CXX_FLAGS_RELEASECLASSIC "${CMAKE_CXX_FLAGS_RELEASECLASSIC}")
    STRING(REGEX REPLACE "[-/]DNDEBUG" "" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
    STRING(REGEX REPLACE "[-/]DNDEBUG" "" CMAKE_C_FLAGS_RELEASECLASSIC "${CMAKE_C_FLAGS_RELEASECLASSIC}")
  endif()

  if (jemalloc)
    find_package(jemalloc REQUIRED)
    add_definitions(-DPROFILE_JEMALLOC)
    include_directories(SYSTEM ${JEMALLOC_INCLUDE_DIRS})
    link_libraries(${JEMALLOC_LIBRARIES})
    get_filename_component(JEMALLOC_LIB_PATH ${JEMALLOC_LIBRARIES} DIRECTORY)
    set(CMAKE_BUILD_RPATH ${CMAKE_BUILD_RPATH} ${JEMALLOC_LIB_PATH})
  endif()

  if (NOT WIN32)
    add_definitions(-D_FILE_OFFSET_BITS=64)
    append_flags(CMAKE_CXX_FLAGS -frtti -std=c++14 -Wno-invalid-offsetof
      -DBOOST_COROUTINE_NO_DEPRECATION_WARNING -DBOOST_COROUTINES_NO_DEPRECATION_WARNING)
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

      # use ldd linker if available
      execute_process(
        COMMAND ${CMAKE_CXX_COMPILER} -fuse-ld=lld -Wl,--version
        ERROR_QUIET OUTPUT_VARIABLE LD_VERSION)
      if ("${LD_VERSION}" MATCHES "LLD")
        append_flags(CMAKE_EXE_LINKER_FLAGS -fuse-ld=lld)
      endif ()
      unset(LD_VERSION)
    endif()

    if (APPLE)
      add_definitions(-DBEAST_COMPILE_OBJECTIVE_CPP=1)
      add_compile_options(
        -Wno-deprecated -Wno-deprecated-declarations -Wno-unused-function)
    endif()

    if (is_gcc)
      add_compile_options(-Wno-unused-but-set-variable -Wno-unused-local-typedefs)
      add_compile_options($<$<OR:$<CONFIG:Debug>,$<CONFIG:DebugClassic>>:-O0>)
    endif (is_gcc)
  else(NOT WIN32)
    add_compile_options(
      /bigobj            # Increase object file max size
      /EHa               # ExceptionHandling all
      /fp:precise        # Floating point behavior
      /Gd                # __cdecl calling convention
      /Gm-               # Minimal rebuild: disabled
      /GR                # Enable RTTI
      /Gy-               # Function level linking: disabled
      /FS
      /MP                # Multiprocessor compilation
      /openmp-           # pragma omp: disabled
      /Zc:forScope       # Language conformance: for scope
      /Zi                # Generate complete debug info
      /errorReport:none  # No error reporting to Internet
      /nologo            # Suppress login banner
      /W3                # Warning level 3
      /WX-               # Disable warnings as errors
      /wd4018            # Disable signed/unsigned comparison warnings
      /wd4244            # Disable float to int possible loss of data warnings
      /wd4267            # Disable size_t to T possible loss of data warnings
      /wd4800            # Disable C4800(int to bool performance)
      /wd4503            # Decorated name length exceeded, name was truncated
      )
    add_definitions(
      -D_WIN32_WINNT=0x6000
      -D_SCL_SECURE_NO_WARNINGS
      -D_CRT_SECURE_NO_WARNINGS
      -DWIN32_CONSOLE
      -DNOMINMAX
      -DBOOST_COROUTINE_NO_DEPRECATION_WARNING
      -DBOOST_COROUTINES_NO_DEPRECATION_WARNING)
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

  if (static)
    append_flags(CMAKE_EXE_LINKER_FLAGS -static-libstdc++)
    # set_target_properties(ripple-libpp PROPERTIES LINK_SEARCH_START_STATIC 1)
    # set_target_properties(ripple-libpp PROPERTIES LINK_SEARCH_END_STATIC 1)
  endif()
endmacro()

############################################################

macro(create_build_folder cur_project)
  if (NOT WIN32)
    ADD_CUSTOM_TARGET(build_folder ALL
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}
      COMMENT "Creating build output folder")
    add_dependencies(${cur_project} build_folder)
  endif()
endmacro()

macro(set_startup_project cur_project)
  if (WIN32 AND NOT ci)
    if (CMAKE_VERSION VERSION_LESS 3.6)
      message(WARNING
        "Setting the VS startup project requires cmake 3.6 or later. Please upgrade.")
    endif()
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY
      VS_STARTUP_PROJECT ${cur_project})
  endif()
endmacro()

macro(link_common_libraries cur_project)
  if (NOT MSVC)
    target_link_libraries(${cur_project} ${Boost_LIBRARIES})
    target_link_libraries(${cur_project} dl)
    target_link_libraries(${cur_project} Threads::Threads)
    if (APPLE)
      find_library(app_kit AppKit)
      find_library(foundation Foundation)
      target_link_libraries(${cur_project}
        ${app_kit} ${foundation})
    else()
      target_link_libraries(${cur_project} rt)
    endif()
  else(NOT MSVC)
    target_link_libraries(${cur_project}
      $<$<OR:$<CONFIG:Debug>,$<CONFIG:DebugClassic>>:VC/static/ssleay32MTd>
      $<$<OR:$<CONFIG:Debug>,$<CONFIG:DebugClassic>>:VC/static/libeay32MTd>)
    target_link_libraries(${cur_project}
      $<$<OR:$<CONFIG:Release>,$<CONFIG:ReleaseClassic>>:VC/static/ssleay32MT>
      $<$<OR:$<CONFIG:Release>,$<CONFIG:ReleaseClassic>>:VC/static/libeay32MT>)
    target_link_libraries(${cur_project}
      legacy_stdio_definitions.lib Shlwapi kernel32 user32 gdi32 winspool comdlg32
      advapi32 shell32 ole32 oleaut32 uuid odbc32 odbccp32 crypt32)
  endif (NOT MSVC)
endmacro()
