# Part of Beast

GroupSources(extras/beast extras)
GroupSources(include/beast beast)
GroupSources(test "/")

add_executable (lib-tests
    ${BEAST_INCLUDES}
    ${EXTRAS_INCLUDES}
    ../extras/beast/unit_test/main.cpp
    core.cpp
    http.cpp
    version.cpp
    websocket.cpp
    zlib.cpp
)

if (NOT WIN32)
    target_link_libraries(lib-tests ${Boost_LIBRARIES})
endif()

if (MINGW)
    set_target_properties(lib-tests PROPERTIES COMPILE_FLAGS "-Wa,-mbig-obj")
endif()
