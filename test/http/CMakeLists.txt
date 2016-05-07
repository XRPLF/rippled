# Part of Beast

GroupSources(extras/beast beast)
GroupSources(include/beast beast)
GroupSources(test/http "/")

add_executable (http-tests
    ${BEAST_INCLUDES}
    ../../extras/beast/unit_test/main.cpp
    basic_headers.cpp
    basic_parser_v1.cpp
    body_type.cpp
    empty_body.cpp
    headers.cpp
    message.cpp
    message_v1.cpp
    parse_error.cpp
    parser_v1.cpp
    read.cpp
    reason.cpp
    resume_context.cpp
    rfc2616.cpp
    rfc7230.cpp
    status.cpp
    streambuf_body.cpp
    string_body.cpp
    type_check.cpp
    write.cpp
)

if (NOT WIN32)
    target_link_libraries(http-tests ${Boost_LIBRARIES})
endif()

add_executable (bench-tests
    ${BEAST_INCLUDES}
    ../../extras/beast/unit_test/main.cpp
    nodejs_parser.cpp
    parser_bench.cpp
)
