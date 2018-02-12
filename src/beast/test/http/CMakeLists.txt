# Part of Beast

GroupSources(example example)
GroupSources(extras/beast extras)
GroupSources(include/beast beast)

GroupSources(test/http "/")

add_executable (http-tests
    ${BEAST_INCLUDES}
    ${EXAMPLE_INCLUDES}
    ${EXTRAS_INCLUDES}
    message_fuzz.hpp
    test_parser.hpp
    ../../extras/beast/unit_test/main.cpp
    basic_parser.cpp
    buffer_body.cpp
    doc_examples.cpp
    doc_snippets.cpp
    dynamic_body.cpp
    empty_body.cpp
    error.cpp
    field.cpp
    fields.cpp
    file_body.cpp
    message.cpp
    parser.cpp
    read.cpp
    rfc7230.cpp
    serializer.cpp
    span_body.cpp
    status.cpp
    string_body.cpp
    type_traits.cpp
    vector_body.cpp
    verb.cpp
    write.cpp
)

target_link_libraries(http-tests
    Beast
    ${Boost_PROGRAM_OPTIONS_LIBRARY}
    ${Boost_FILESYSTEM_LIBRARY}
    ${Boost_COROUTINE_LIBRARY}
    ${Boost_THREAD_LIBRARY}
    ${Boost_CONTEXT_LIBRARY}
    )
