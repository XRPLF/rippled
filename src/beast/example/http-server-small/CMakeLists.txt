# Part of Beast

GroupSources(include/beast beast)

GroupSources(example/http-server-small "/")

add_executable (http-server-small
    ${BEAST_INCLUDES}
    http_server_small.cpp
)

target_link_libraries(http-server-small
    Beast
    )

