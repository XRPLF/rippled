# Part of Beast

GroupSources(include/beast beast)

GroupSources(example/server-framework "/")
GroupSources(example/common "common")

file(GLOB_RECURSE SERVER_INCLUDES
    ${PROJECT_SOURCE_DIR}/example/server-framework/*.hpp
  )

add_executable (server-framework
    ${BEAST_INCLUDES}
    ${COMMON_INCLUDES}
    ${SERVER_INCLUDES}
    main.cpp
)

target_link_libraries(
    server-framework
    Beast
    ${Boost_PROGRAM_OPTIONS_LIBRARY}
    ${Boost_FILESYSTEM_LIBRARY})

if (OPENSSL_FOUND)
    target_link_libraries(server-framework ${OPENSSL_LIBRARIES})
endif()
