# Part of Beast

GroupSources(example/common common)
GroupSources(extras/beast extras)
GroupSources(include/beast beast)
GroupSources(test/common "/")

add_executable (common-test
    ${BEAST_INCLUDES}
    ${COMMON_INCLUDES}
    detect_ssl.cpp
    mime_types.cpp
    rfc7231.cpp
    ssl_stream.cpp
    write_msg.cpp
    main.cpp
)

target_link_libraries(common-test
    Beast
    ${Boost_PROGRAM_OPTIONS_LIBRARY}
    ${Boost_FILESYSTEM_LIBRARY}
    )

if (OPENSSL_FOUND)
    target_link_libraries (common-test ${OPENSSL_LIBRARIES})
endif()
