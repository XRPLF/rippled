# Part of Beast

GroupSources(extras/beast extras)
GroupSources(include/beast beast)
GroupSources(test/core "/")

add_executable (core-tests
    ${BEAST_INCLUDES}
    ${EXTRAS_INCLUDES}
    ../../extras/beast/unit_test/main.cpp
    buffer_test.hpp
    async_completion.cpp
    basic_streambuf.cpp
    bind_handler.cpp
    buffer_cat.cpp
    buffer_concepts.cpp
    buffers_adapter.cpp
    clamp.cpp
    consuming_buffers.cpp
    dynabuf_readstream.cpp
    error.cpp
    handler_alloc.cpp
    handler_concepts.cpp
    handler_ptr.cpp
    placeholders.cpp
    prepare_buffers.cpp
    static_streambuf.cpp
    static_string.cpp
    stream_concepts.cpp
    streambuf.cpp
    to_string.cpp
    write_dynabuf.cpp
    base64.cpp
    empty_base_optimization.cpp
    get_lowest_layer.cpp
    sha1.cpp
)

if (NOT WIN32)
    target_link_libraries(core-tests ${Boost_LIBRARIES} Threads::Threads)
endif()
