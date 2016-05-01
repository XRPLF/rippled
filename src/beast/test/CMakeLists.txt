# Part of Beast

GroupSources(include/beast)
GroupSources(test)

add_executable (core-tests
    ${BEAST_INCLUDES}
    main.cpp
    async_completion.cpp
    basic_streambuf.cpp
    bind_handler.cpp
    buffer_cat.cpp
    buffers_adapter.cpp
    consuming_buffers.cpp
    handler_alloc.cpp
    placeholders.cpp
    prepare_buffers.cpp
    static_streambuf.cpp
    static_string.cpp
    streambuf.cpp
    streambuf_readstream.cpp
    to_string.cpp
    type_check.cpp
    detail/base64.cpp
    detail/empty_base_optimization.cpp
)

if (NOT WIN32)
    target_link_libraries(core-tests ${Boost_LIBRARIES})
endif()

add_executable (http-tests
    ${BEAST_INCLUDES}
    main.cpp
    http/basic_headers.cpp
    http/basic_parser.cpp
    http/empty_body.cpp
    http/error.cpp
    http/headers.cpp
    http/message.cpp
    http/parse_error.cpp
    http/parser.cpp
    http/read.cpp
    http/reason.cpp
    http/resume_context.cpp
    http/rfc2616.cpp
    http/rfc7230.cpp
    http/status.cpp
    http/streambuf_body.cpp
    http/string_body.cpp
    http/write.cpp
)

if (NOT WIN32)
    target_link_libraries(http-tests ${Boost_LIBRARIES})
endif()

add_executable (parser-bench
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
