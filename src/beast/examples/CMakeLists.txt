# Part of Beast

GroupSources(extras/beast extras)
GroupSources(include/beast beast)

GroupSources(examples "/")

add_executable (http-crawl
    ${BEAST_INCLUDES}
    ${EXTRAS_INCLUDES}
    urls_large_data.hpp
    urls_large_data.cpp
    http_crawl.cpp
)

if (NOT WIN32)
    target_link_libraries(http-crawl ${Boost_LIBRARIES} Threads::Threads)
else()
    target_link_libraries(http-crawl ${Boost_LIBRARIES})
endif()

add_executable (http-server
    ${BEAST_INCLUDES}
    ${EXTRAS_INCLUDES}
    file_body.hpp
    mime_type.hpp
    http_async_server.hpp
    http_sync_server.hpp
    http_server.cpp
)

if (NOT WIN32)
    target_link_libraries(http-server ${Boost_LIBRARIES} Threads::Threads)
else()
    target_link_libraries(http-server ${Boost_LIBRARIES})
endif()


add_executable (http-example
    ${BEAST_INCLUDES}
    ${EXTRAS_INCLUDES}
    http_example.cpp
)

if (NOT WIN32)
    target_link_libraries(http-example ${Boost_LIBRARIES} Threads::Threads)
else()
    target_link_libraries(http-example ${Boost_LIBRARIES})
endif()


add_executable (websocket-echo
    ${BEAST_INCLUDES}
    websocket_async_echo_server.hpp
    websocket_sync_echo_server.hpp
    websocket_echo.cpp
)

if (NOT WIN32)
    target_link_libraries(websocket-echo ${Boost_LIBRARIES} Threads::Threads)
else()
    target_link_libraries(websocket-echo ${Boost_LIBRARIES})
endif()


add_executable (websocket-example
    ${BEAST_INCLUDES}
    ${EXTRAS_INCLUDES}
    websocket_example.cpp
)

if (NOT WIN32)
    target_link_libraries(websocket-example ${Boost_LIBRARIES} Threads::Threads)
else()
    target_link_libraries(websocket-example ${Boost_LIBRARIES})
endif()
