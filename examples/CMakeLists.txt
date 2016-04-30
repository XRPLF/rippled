# Part of Beast

GroupSources(include/beast)
GroupSources(examples)
GroupSources(test)

add_executable (http-crawl
    ${BEAST_INCLUDES}
    urls_large_data.hpp
    urls_large_data.cpp
    http_crawl.cpp
)

if (NOT WIN32)
    target_link_libraries(http-crawl ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
endif()

add_executable (http-server
    ${BEAST_INCLUDES}
    file_body.hpp
    http_async_server.hpp
    http_stream.hpp
    http_stream.ipp
    http_sync_server.hpp
    ../test/sig_wait.hpp
    http_server.cpp
)

if (NOT WIN32)
    target_link_libraries(http-server ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
endif()

add_executable (http-example
    ${BEAST_INCLUDES}
    http_example.cpp
)

if (NOT WIN32)
    target_link_libraries(http-example ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
endif()

add_executable (websocket-example
    ${BEAST_INCLUDES}
    websocket_example.cpp
)

if (NOT WIN32)
    target_link_libraries(websocket-example ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
endif()
