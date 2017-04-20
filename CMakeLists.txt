# Part of Beast

cmake_minimum_required (VERSION 3.5.2)

project (Beast)

set_property (GLOBAL PROPERTY USE_FOLDERS ON)

if (MSVC)
    # /wd4244 /wd4127
    add_definitions (-D_WIN32_WINNT=0x0601)
    add_definitions (-D_SCL_SECURE_NO_WARNINGS=1)
    add_definitions (-D_CRT_SECURE_NO_WARNINGS=1)

    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4100 /wd4244 /wd4251 /MP /W4 /bigobj")
    set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Ob2 /Oi /Ot /GL")
    set (CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /Oi /Ot")

    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SAFESEH:NO")
    set (CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")

    # for RelWithDebInfo builds, disable incremental linking
    # since CMake sets it ON by default for that build type and it
    # causes warnings
    #
    string (REPLACE "/INCREMENTAL" "/INCREMENTAL:NO" replacement_flags
        ${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO})
    set (CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO ${replacement_flags})

else()
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads)

    set(CMAKE_CXX_FLAGS
      "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Wextra -Wpedantic -Wno-unused-parameter")
endif()

#-------------------------------------------------------------------------------
#
# Boost
#

option (Boost_USE_STATIC_LIBS "Use static libraries for boost" ON)

set (Boost_NO_SYSTEM_PATHS ON)
set (Boost_USE_MULTITHREADED ON)

unset (Boost_INCLUDE_DIR CACHE)
unset (Boost_LIBRARY_DIRS CACHE)
find_package (Boost REQUIRED COMPONENTS
    coroutine
    context
    filesystem
    program_options
    system
    thread
    )

include_directories (SYSTEM ${Boost_INCLUDE_DIRS})
link_libraries (${Boost_LIBRARIES})

if (MSVC)
    add_definitions (-DBOOST_ALL_NO_LIB) # disable autolinking
elseif (MINGW)
    link_libraries(ws2_32 mswsock)
endif()

add_definitions (-DBOOST_COROUTINES_NO_DEPRECATION_WARNING=1) # for asio

#-------------------------------------------------------------------------------
#
# OpenSSL
#

if (APPLE AND NOT DEFINED ENV{OPENSSL_ROOT_DIR})
  find_program(HOMEBREW brew)
  if (NOT HOMEBREW STREQUAL "HOMEBREW-NOTFOUND")
    execute_process(COMMAND brew --prefix openssl
      OUTPUT_VARIABLE OPENSSL_ROOT_DIR
      OUTPUT_STRIP_TRAILING_WHITESPACE)
  endif()
endif()

find_package(OpenSSL)

#
#-------------------------------------------------------------------------------

function(DoGroupSources curdir rootdir folder)
    file(GLOB children RELATIVE ${PROJECT_SOURCE_DIR}/${curdir} ${PROJECT_SOURCE_DIR}/${curdir}/*)
    foreach(child ${children})
        if(IS_DIRECTORY ${PROJECT_SOURCE_DIR}/${curdir}/${child})
            DoGroupSources(${curdir}/${child} ${rootdir} ${folder})
        elseif(${child} STREQUAL "CMakeLists.txt")
            source_group("" FILES ${PROJECT_SOURCE_DIR}/${curdir}/${child})
        else()
            string(REGEX REPLACE ^${rootdir} ${folder} groupname ${curdir})
            string(REPLACE "/" "\\" groupname ${groupname})
            source_group(${groupname} FILES ${PROJECT_SOURCE_DIR}/${curdir}/${child})
        endif()
    endforeach()
endfunction()

function(GroupSources curdir folder)
    DoGroupSources(${curdir} ${curdir} ${folder})
endfunction()

#-------------------------------------------------------------------------------

if ("${VARIANT}" STREQUAL "coverage")
    set(CMAKE_CXX_FLAGS
      "${CMAKE_CXX_FLAGS} -fprofile-arcs -ftest-coverage")
    set(CMAKE_BUILD_TYPE RELWITHDEBINFO)
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lgcov")
elseif ("${VARIANT}" STREQUAL "asan")
    set(CMAKE_CXX_FLAGS
      "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
    set(CMAKE_BUILD_TYPE RELWITHDEBINFO)
elseif ("${VARIANT}" STREQUAL "usan")
    set(CMAKE_CXX_FLAGS
      "${CMAKE_CXX_FLAGS} -fsanitize=undefined -fno-omit-frame-pointer")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=undefined")
    set(CMAKE_BUILD_TYPE RELWITHDEBINFO)
elseif ("${VARIANT}" STREQUAL "debug")
    set(CMAKE_BUILD_TYPE DEBUG)
elseif ("${VARIANT}" STREQUAL "release")
    set(CMAKE_BUILD_TYPE RELEASE)
endif()

include_directories (extras)
include_directories (include)

set(ZLIB_SOURCES
    ${PROJECT_SOURCE_DIR}/test/zlib/zlib-1.2.8/crc32.h
    ${PROJECT_SOURCE_DIR}/test/zlib/zlib-1.2.8/deflate.h
    ${PROJECT_SOURCE_DIR}/test/zlib/zlib-1.2.8/inffast.h
    ${PROJECT_SOURCE_DIR}/test/zlib/zlib-1.2.8/inffixed.h
    ${PROJECT_SOURCE_DIR}/test/zlib/zlib-1.2.8/inflate.h
    ${PROJECT_SOURCE_DIR}/test/zlib/zlib-1.2.8/inftrees.h
    ${PROJECT_SOURCE_DIR}/test/zlib/zlib-1.2.8/trees.h
    ${PROJECT_SOURCE_DIR}/test/zlib/zlib-1.2.8/zlib.h
    ${PROJECT_SOURCE_DIR}/test/zlib/zlib-1.2.8/zutil.h
    ${PROJECT_SOURCE_DIR}/test/zlib/zlib-1.2.8/adler32.c
    ${PROJECT_SOURCE_DIR}/test/zlib/zlib-1.2.8/compress.c
    ${PROJECT_SOURCE_DIR}/test/zlib/zlib-1.2.8/crc32.c
    ${PROJECT_SOURCE_DIR}/test/zlib/zlib-1.2.8/deflate.c
    ${PROJECT_SOURCE_DIR}/test/zlib/zlib-1.2.8/infback.c
    ${PROJECT_SOURCE_DIR}/test/zlib/zlib-1.2.8/inffast.c
    ${PROJECT_SOURCE_DIR}/test/zlib/zlib-1.2.8/inflate.c
    ${PROJECT_SOURCE_DIR}/test/zlib/zlib-1.2.8/inftrees.c
    ${PROJECT_SOURCE_DIR}/test/zlib/zlib-1.2.8/trees.c
    ${PROJECT_SOURCE_DIR}/test/zlib/zlib-1.2.8/uncompr.c
    ${PROJECT_SOURCE_DIR}/test/zlib/zlib-1.2.8/zutil.c
)

file(GLOB_RECURSE BEAST_INCLUDES
    ${PROJECT_SOURCE_DIR}/include/beast/*.hpp
    ${PROJECT_SOURCE_DIR}/include/beast/*.ipp
)

file(GLOB_RECURSE EXTRAS_INCLUDES
    ${PROJECT_SOURCE_DIR}/extras/beast/*.hpp
    ${PROJECT_SOURCE_DIR}/extras/beast/*.ipp
)

add_subdirectory (examples)
if (NOT OPENSSL_FOUND)
    message("OpenSSL not found. Not building examples/ssl")
else()
    add_subdirectory (examples/ssl)
endif()

add_subdirectory (test)
add_subdirectory (test/core)
add_subdirectory (test/http)
add_subdirectory (test/websocket)
add_subdirectory (test/zlib)
