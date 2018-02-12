# Part of Beast

GroupSources(include/beast beast)

GroupSources(example/http-crawl "/")

add_executable (http-crawl
    ${BEAST_INCLUDES}
    ${EXTRAS_INCLUDES}
    urls_large_data.hpp
    urls_large_data.cpp
    http_crawl.cpp
)

target_link_libraries(http-crawl Beast)
