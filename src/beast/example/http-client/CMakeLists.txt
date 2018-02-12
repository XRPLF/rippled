# Part of Beast

GroupSources(include/beast beast)

GroupSources(example/http-client "/")

add_executable (http-client
    ${BEAST_INCLUDES}
    ${EXTRAS_INCLUDES}
    http_client.cpp
)

target_link_libraries(http-client Beast)
