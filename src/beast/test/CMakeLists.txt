# Part of Beast

add_subdirectory (core)
add_subdirectory (http)
add_subdirectory (websocket)
add_subdirectory (zlib)

if ((NOT "${VARIANT}" STREQUAL "coverage") AND
    (NOT "${VARIANT}" STREQUAL "ubasan"))

    add_subdirectory (benchmarks)
    add_subdirectory (common)
    add_subdirectory (server)

    GroupSources(extras/beast extras)
    GroupSources(include/beast beast)
    
    GroupSources(test "/")

    add_executable (lib-tests
        ${BEAST_INCLUDES}
        ${EXTRAS_INCLUDES}
        ../extras/beast/unit_test/main.cpp
        config.cpp
        core.cpp
        exemplars.cpp
        http.cpp
        version.cpp
        websocket.cpp
        zlib.cpp
    )

    target_link_libraries(lib-tests Beast ${Boost_PROGRAM_OPTIONS_LIBRARY})

    if (MINGW)
        set_target_properties(lib-tests PROPERTIES COMPILE_FLAGS "-Wa,-mbig-obj")
    endif()
endif()
