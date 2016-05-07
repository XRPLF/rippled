# Part of Beast

GroupSources(extras/beast beast)
GroupSources(include/beast beast)
GroupSources(test "/")

add_executable (lib-tests
    ${BEAST_INCLUDES}
    ../extras/beast/unit_test/main.cpp
    core.cpp
    http.cpp
    version.cpp
    websocket.cpp
)

if (NOT WIN32)
    target_link_libraries(core-tests ${Boost_LIBRARIES})
endif()
