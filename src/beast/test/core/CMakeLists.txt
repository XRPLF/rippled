# Part of Beast

GroupSources(extras/beast beast)
GroupSources(include/beast beast)
GroupSources(test/core "/")

add_executable (core-tests
    ${BEAST_INCLUDES}
    ../../extras/beast/unit_test/main.cpp
    buffer_test.hpp
    async_completion.cpp
    basic_streambuf.cpp
    bind_handler.cpp
    buffer_cat.cpp
    buffer_concepts.cpp
    buffers_adapter.cpp
    consuming_buffers.cpp
    error.cpp
    handler_alloc.cpp
    handler_concepts.cpp
    placeholders.cpp
    prepare_buffers.cpp
    static_streambuf.cpp
    static_string.cpp
    stream_concepts.cpp
    streambuf.cpp
    streambuf_readstream.cpp
    to_string.cpp
    write_streambuf.cpp
    detail/base64.cpp
    detail/empty_base_optimization.cpp
    detail/sha1.cpp
)

if (NOT WIN32)
    target_link_libraries(core-tests ${Boost_LIBRARIES})
endif()
