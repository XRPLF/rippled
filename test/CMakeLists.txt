# Part of Beast

GroupSources(include/beast)
GroupSources(extras)
GroupSources(test)

add_executable (core-tests
    ${BEAST_INCLUDES}
    main.cpp
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
    http.cpp
    placeholders.cpp
    prepare_buffers.cpp
    static_streambuf.cpp
    static_string.cpp
    stream_concepts.cpp
    streambuf.cpp
    streambuf_readstream.cpp
    to_string.cpp
    version.cpp
    websocket.cpp
    write_streambuf.cpp
    detail/base64.cpp
    detail/empty_base_optimization.cpp
    detail/sha1.cpp
)

if (NOT WIN32)
    target_link_libraries(core-tests ${Boost_LIBRARIES})
endif()

add_executable (http-tests
    ${BEAST_INCLUDES}
    fail_stream.hpp
    string_stream.hpp
    yield_to.hpp
    main.cpp
    http/basic_headers.cpp
    http/basic_parser_v1.cpp
    http/body_type.cpp
    http/empty_body.cpp
    http/headers.cpp
    http/message.cpp
    http/message_v1.cpp
    http/parse_error.cpp
    http/parser_v1.cpp
    http/read.cpp
    http/reason.cpp
    http/resume_context.cpp
    http/rfc2616.cpp
    http/rfc7230.cpp
    http/status.cpp
    http/streambuf_body.cpp
    http/string_body.cpp
    http/type_check.cpp
    http/write.cpp
)

if (NOT WIN32)
    target_link_libraries(http-tests ${Boost_LIBRARIES})
endif()

add_executable (bench-tests
    ${BEAST_INCLUDES}
    main.cpp
    http/nodejs_parser.cpp
    http/parser_bench.cpp
)

if (NOT WIN32)
    target_link_libraries(parser-bench ${Boost_LIBRARIES})
endif()

add_executable (websocket-tests
    ${BEAST_INCLUDES}
    fail_stream.hpp
    string_stream.hpp
    yield_to.hpp
    websocket/websocket_async_echo_peer.hpp
    websocket/websocket_sync_echo_peer.hpp
    main.cpp
    websocket/error.cpp
    websocket/option.cpp
    websocket/rfc6455.cpp
    websocket/stream.cpp
    websocket/teardown.cpp
    websocket/utf8_checker.cpp
)

if (NOT WIN32)
    target_link_libraries(websocket-tests ${Boost_LIBRARIES})
endif()

add_executable (websocket-echo
    ${BEAST_INCLUDES}
    sig_wait.hpp
    websocket/websocket_async_echo_peer.hpp
    websocket/websocket_sync_echo_peer.hpp
    websocket/websocket_echo.cpp
)

if (NOT WIN32)
    target_link_libraries(websocket-echo ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
endif()
