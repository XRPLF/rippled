# Part of Beast

GroupSources(extras/beast extras)
GroupSources(include/beast beast)

GroupSources(test/zlib "/")

set(ZLIB_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/zlib-1.2.8/crc32.h
    ${CMAKE_CURRENT_LIST_DIR}/zlib-1.2.8/deflate.h
    ${CMAKE_CURRENT_LIST_DIR}/zlib-1.2.8/inffast.h
    ${CMAKE_CURRENT_LIST_DIR}/zlib-1.2.8/inffixed.h
    ${CMAKE_CURRENT_LIST_DIR}/zlib-1.2.8/inflate.h
    ${CMAKE_CURRENT_LIST_DIR}/zlib-1.2.8/inftrees.h
    ${CMAKE_CURRENT_LIST_DIR}/zlib-1.2.8/trees.h
    ${CMAKE_CURRENT_LIST_DIR}/zlib-1.2.8/zlib.h
    ${CMAKE_CURRENT_LIST_DIR}/zlib-1.2.8/zutil.h
    ${CMAKE_CURRENT_LIST_DIR}/zlib-1.2.8/adler32.c
    ${CMAKE_CURRENT_LIST_DIR}/zlib-1.2.8/compress.c
    ${CMAKE_CURRENT_LIST_DIR}/zlib-1.2.8/crc32.c
    ${CMAKE_CURRENT_LIST_DIR}/zlib-1.2.8/deflate.c
    ${CMAKE_CURRENT_LIST_DIR}/zlib-1.2.8/infback.c
    ${CMAKE_CURRENT_LIST_DIR}/zlib-1.2.8/inffast.c
    ${CMAKE_CURRENT_LIST_DIR}/zlib-1.2.8/inflate.c
    ${CMAKE_CURRENT_LIST_DIR}/zlib-1.2.8/inftrees.c
    ${CMAKE_CURRENT_LIST_DIR}/zlib-1.2.8/trees.c
    ${CMAKE_CURRENT_LIST_DIR}/zlib-1.2.8/uncompr.c
    ${CMAKE_CURRENT_LIST_DIR}/zlib-1.2.8/zutil.c
)

if (MSVC)
    set_source_files_properties (${ZLIB_SOURCES} PROPERTIES COMPILE_FLAGS "/wd4127 /wd4131 /wd4244")
endif()

add_executable (zlib-tests
    ${BEAST_INCLUDES}
    ${EXTRAS_INCLUDES}
    ${ZLIB_SOURCES}
    ../../extras/beast/unit_test/main.cpp
    ztest.hpp
    deflate_stream.cpp
    error.cpp
    inflate_stream.cpp
)

target_link_libraries(zlib-tests
    Beast
    ${Boost_PROGRAM_OPTIONS_LIBRARY}
    ${Boost_FILESYSTEM_LIBRARY}
    )
