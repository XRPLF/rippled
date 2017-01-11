# Part of Beast

GroupSources(extras/beast extras)
GroupSources(include/beast beast)

GroupSources(examples/ssl "/")

include_directories(${OPENSSL_INCLUDE_DIR})

add_executable (http-ssl-example
    ${BEAST_INCLUDES}
    ${EXTRAS_INCLUDES}
    http_ssl_example.cpp
)

target_link_libraries(http-ssl-example ${OPENSSL_LIBRARIES})

if (NOT WIN32)
    target_link_libraries(http-ssl-example ${Boost_LIBRARIES} Threads::Threads)
endif()

add_executable (websocket-ssl-example
    ${BEAST_INCLUDES}
    ${EXTRAS_INCLUDES}
    websocket_ssl_example.cpp
)

target_link_libraries(websocket-ssl-example ${OPENSSL_LIBRARIES})

if (NOT WIN32)
    target_link_libraries(websocket-ssl-example ${Boost_LIBRARIES} Threads::Threads)
endif()
