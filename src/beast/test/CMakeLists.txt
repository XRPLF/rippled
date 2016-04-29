# Part of Beast

GroupSources(include/beast)
GroupSources(test)

set(CORE_TESTS_SRCS
    main.cpp
    async_completion.cpp
    basic_streambuf.cpp
    bind_handler.cpp
    buffer_cat.cpp
    buffers_adapter.cpp
    buffers_debug.cpp
    consuming_buffers.cpp
    handler_alloc.cpp
    placeholders.cpp
    prepare_buffers.cpp
    static_streambuf.cpp
    streambuf.cpp
    streambuf_readstream.cpp
    type_check.cpp
    detail/base64.cpp
    detail/empty_base_optimization.cpp
)

add_executable (core-tests
    ${BEAST_INCLUDES}
    ${CORE_TESTS_SRCS}
)

set(HTTP_TESTS_SRCS
    main.cpp
    http/basic_headers.cpp
    http/basic_parser.cpp
    http/chunk_encode.cpp
    http/empty_body.cpp
    http/error.cpp
    http/headers.cpp
    http/message.cpp
    http/method.cpp
    http/parse_error.cpp
    http/parser.cpp
    http/read.cpp
    http/reason.cpp
    http/resume_context.cpp
    http/rfc2616.cpp
    http/rfc7230.cpp
    http/streambuf_body.cpp
    http/string_body.cpp
    http/write.cpp
)

add_executable (http-tests
    ${BEAST_INCLUDES}
    ${HTTP_TESTS_SRCS}
)

set(WEBSOCKET_TESTS_SRCS
    main.cpp
    websocket/error.cpp
    websocket/option.cpp
    websocket/rfc6455.cpp
    websocket/static_string.cpp
    websocket/teardown.cpp
    websocket/utf8_checker.cpp
)

add_executable (websocket-tests
    ${BEAST_INCLUDES}
    ${WEBSOCKET_TESTS_SRCS}
)

set(PARSER_BENCH_SRCS
    main.cpp
    http/nodejs_parser.cpp
    http/parser_bench.cpp
)

add_executable (parser-bench
    ${BEAST_INCLUDES}
    ${PARSER_BENCH_SRCS}
)
