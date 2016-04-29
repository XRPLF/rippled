# Part of Beast

cmake_minimum_required (VERSION 3.5)

project (Beast)

set_property (GLOBAL PROPERTY USE_FOLDERS ON)

if (WIN32)
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /wd4100 /D_SCL_SECURE_NO_WARNINGS=1 /D_CRT_SECURE_NO_WARNINGS=1")
else()
endif()

message ("cxx Flags: " ${CMAKE_CXX_FLAGS})

macro(GroupSources curdir)
    file(GLOB children RELATIVE ${PROJECT_SOURCE_DIR}/${curdir} ${PROJECT_SOURCE_DIR}/${curdir}/*)
    foreach(child ${children})
        if(IS_DIRECTORY ${PROJECT_SOURCE_DIR}/${curdir}/${child})
            GroupSources(${curdir}/${child})
        elseif(${child} STREQUAL "CMakeLists.txt")
            source_group("" FILES ${PROJECT_SOURCE_DIR}/${curdir}/${child})
        else()
            string(REPLACE "/" "\\" groupname ${curdir})
            string(REPLACE "src" "Common" groupname ${groupname})
            source_group(${groupname} FILES ${PROJECT_SOURCE_DIR}/${curdir}/${child})
        endif()
    endforeach()
endmacro()

include_directories (include)

file(GLOB_RECURSE BEAST_INCLUDES
    ${PROJECT_SOURCE_DIR}/include/beast/*.hpp
    ${PROJECT_SOURCE_DIR}/include/beast/*.ipp
)

add_subdirectory (test)
add_subdirectory (examples)

#enable_testing()
