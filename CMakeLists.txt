# Part of Beast

cmake_minimum_required (VERSION 3.2)

project (Beast)

set_property (GLOBAL PROPERTY USE_FOLDERS ON)

if (WIN32)
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP /W4 /wd4100 /D_SCL_SECURE_NO_WARNINGS=1 /D_CRT_SECURE_NO_WARNINGS=1")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SAFESEH:NO")
else()
    set(Boost_USE_STATIC_LIBS ON)
    set(Boost_USE_MULTITHREADED ON)
    find_package(Boost REQUIRED COMPONENTS filesystem program_options system)
    include_directories(${Boost_INCLUDE_DIRS})
    link_directories(${Boost_LIBRARY_DIR})

    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads)

    set(CMAKE_CXX_FLAGS
      "${CMAKE_CXX_FLAGS} -g -std=c++11 -Wall /Wextra /Wpedantic /Wconversion -Wno-unused-variable")
endif()

message ("cxx Flags: " ${CMAKE_CXX_FLAGS})

function(DoGroupSources curdir rootdir folder)
    file(GLOB children RELATIVE ${PROJECT_SOURCE_DIR}/${curdir} ${PROJECT_SOURCE_DIR}/${curdir}/*)
    foreach(child ${children})
        if(IS_DIRECTORY ${PROJECT_SOURCE_DIR}/${curdir}/${child})
            DoGroupSources(${curdir}/${child} ${rootdir} ${folder})
        elseif(${child} STREQUAL "CMakeLists.txt")
            source_group("" FILES ${PROJECT_SOURCE_DIR}/${curdir}/${child})
        else()
            string(REGEX REPLACE ^${rootdir} ${folder} groupname ${curdir})
            #set(groupname ${curdir})
            string(REPLACE "/" "\\" groupname ${groupname})
            source_group(${groupname} FILES ${PROJECT_SOURCE_DIR}/${curdir}/${child})
        endif()
    endforeach()
endfunction()

function(GroupSources curdir folder)
    DoGroupSources(${curdir} ${curdir} ${folder})
endfunction()

include_directories (extras)
include_directories (include)

file(GLOB_RECURSE BEAST_INCLUDES
    ${PROJECT_SOURCE_DIR}/include/beast/*.hpp
    ${PROJECT_SOURCE_DIR}/include/beast/*.ipp
    ${PROJECT_SOURCE_DIR}/extras/beast/*.hpp
    ${PROJECT_SOURCE_DIR}/extras/beast/*.ipp
)

add_subdirectory (examples)
add_subdirectory (test)
add_subdirectory (test/http)
add_subdirectory (test/websocket)

#enable_testing()
