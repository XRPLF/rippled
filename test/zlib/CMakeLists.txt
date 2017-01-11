# Part of Beast

GroupSources(extras/beast extras)
GroupSources(include/beast beast)
GroupSources(test/zlib "/")

set(ZLIB_SOURCES
    zlib-1.2.8/crc32.h
    zlib-1.2.8/deflate.h
    zlib-1.2.8/inffast.h
    zlib-1.2.8/inffixed.h
    zlib-1.2.8/inflate.h
    zlib-1.2.8/inftrees.h
    zlib-1.2.8/trees.h
    zlib-1.2.8/zlib.h
    zlib-1.2.8/zutil.h
    zlib-1.2.8/adler32.c
    zlib-1.2.8/compress.c
    zlib-1.2.8/crc32.c
    zlib-1.2.8/deflate.c
    zlib-1.2.8/infback.c
    zlib-1.2.8/inffast.c
    zlib-1.2.8/inflate.c
    zlib-1.2.8/inftrees.c
    zlib-1.2.8/trees.c
    zlib-1.2.8/uncompr.c
    zlib-1.2.8/zutil.c
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

if (NOT WIN32)
    target_link_libraries(zlib-tests ${Boost_LIBRARIES} Threads::Threads)
endif()
