# Part of Beast

GroupSources(include/beast beast)
GroupSources(example/common common)
GroupSources(example/http-server-fast "/")

add_executable (http-server-fast
    ${BEAST_INCLUDES}
    ${COMMON_INCLUDES}
    fields_alloc.hpp
    http_server_fast.cpp
)

target_link_libraries(http-server-fast
    Beast
    ${Boost_FILESYSTEM_LIBRARY}
    )

