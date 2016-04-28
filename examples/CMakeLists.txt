# Part of Beast

GroupSources(include/beast)
GroupSources(examples)

add_executable (http-crawl
    ${BEAST_INCLUDES}
    http_crawl.cpp
    urls_large_data.cpp
)

add_executable (http-server
    ${BEAST_INCLUDES}
    http_server.cpp
)

add_executable (http-example
    ${BEAST_INCLUDES}
    http_example.cpp
)

add_executable (websocket-echo
    ${BEAST_INCLUDES}
    websocket_echo.cpp
)

add_executable (websocket-example
    ${BEAST_INCLUDES}
    websocket_example.cpp
)
