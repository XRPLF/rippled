# Part of Beast

GroupSources(include/beast beast)
GroupSources(example/common common)
GroupSources(example/http-server-threaded "/")

add_executable (http-server-threaded
    ${BEAST_INCLUDES}
    ${COMMON_INCLUDES}
    http_server_threaded.cpp
)

target_link_libraries(http-server-threaded
    Beast
    ${Boost_FILESYSTEM_LIBRARY}
    )

