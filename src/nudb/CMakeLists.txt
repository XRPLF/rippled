cmake_minimum_required (VERSION 3.2)

project (nudb)

set_property (GLOBAL PROPERTY USE_FOLDERS ON)

if (WIN32)
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP /W4 /wd4100 /D _WIN32_WINNT=0x0600 /D_SCL_SECURE_NO_WARNINGS=1 /D_CRT_SECURE_NO_WARNINGS=1")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SAFESEH:NO")
else ()
    set (Boost_USE_STATIC_LIBS ON)
    set (Boost_USE_MULTITHREADED ON)
    find_package (Boost REQUIRED COMPONENTS filesystem program_options system thread)
    include_directories (SYSTEM ${Boost_INCLUDE_DIRS})
    link_directories (${Boost_LIBRARY_DIR})

    set (THREADS_PREFER_PTHREAD_FLAG ON)
    find_package (Threads)

    set (CMAKE_CXX_FLAGS
      "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Wpedantic")
endif ()

if ("${VARIANT}" STREQUAL "coverage")
    set (CMAKE_CXX_FLAGS
      "${CMAKE_CXX_FLAGS} -fprofile-arcs -ftest-coverage")
    set (CMAKE_BUILD_TYPE RELWITHDEBINFO)
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lgcov")
elseif ("${VARIANT}" STREQUAL "asan")
    set (CMAKE_CXX_FLAGS
      "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
    set (CMAKE_BUILD_TYPE RELWITHDEBINFO)
elseif ("${VARIANT}" STREQUAL "usan")
    set (CMAKE_CXX_FLAGS
      "${CMAKE_CXX_FLAGS} -fsanitize=undefined -fno-omit-frame-pointer")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=undefined")
    set (CMAKE_BUILD_TYPE RELWITHDEBINFO)
elseif ("${VARIANT}" STREQUAL "debug")
    set (CMAKE_BUILD_TYPE DEBUG)
elseif ("${VARIANT}" STREQUAL "release")
    set (CMAKE_BUILD_TYPE RELEASE)
endif ()

function (DoGroupSources curdir rootdir folder)
    file (GLOB children RELATIVE ${PROJECT_SOURCE_DIR}/${curdir} ${PROJECT_SOURCE_DIR}/${curdir}/*)
    foreach (child ${children})
        if (IS_DIRECTORY ${PROJECT_SOURCE_DIR}/${curdir}/${child})
            DoGroupSources (${curdir}/${child} ${rootdir} ${folder})
        elseif (${child} STREQUAL "CMakeLists.txt")
            source_group ("" FILES ${PROJECT_SOURCE_DIR}/${curdir}/${child})
        else ()
            string (REGEX REPLACE ^${rootdir} ${folder} groupname ${curdir})
            #set (groupname ${curdir})
            string (REPLACE "/" "\\" groupname ${groupname})
            source_group (${groupname} FILES ${PROJECT_SOURCE_DIR}/${curdir}/${child})
        endif ()
    endforeach ()
endfunction ()

function (GroupSources curdir folder)
    DoGroupSources (${curdir} ${curdir} ${folder})
endfunction ()

include_directories (
    include
    extras
    extras/beast/extras
    )

file (GLOB_RECURSE BEAST_INCLUDES
    ${PROJECT_SOURCE_DIR}/extras/beast/extras/beast/unit_test/*.hpp
    ${PROJECT_SOURCE_DIR}/extras/beast/extras/beast/unit_test/*.ipp
)

file (GLOB_RECURSE EXTRAS_INCLUDES
    ${PROJECT_SOURCE_DIR}/extras/nudb/*
)

file (GLOB_RECURSE NUDB_INCLUDES
    ${PROJECT_SOURCE_DIR}/include/nudb/*
)

add_subdirectory (bench)
add_subdirectory (examples)
add_subdirectory (test)
add_subdirectory (tools)
