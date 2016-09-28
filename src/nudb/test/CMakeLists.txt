# Part of nudb

GroupSources(test "/")
GroupSources(include/nudb nudb)
GroupSources(extras/nudb extras)
GroupSources(extras/beast/include/beast beast)
GroupSources(extras/beast/extras/beast beast)

add_executable(test-all
    ${EXTRAS_INCLUDES}
    ${NUDB_INCLUDES}
    ${BEAST_INCLUDES}
    ../extras/beast/extras/beast/unit_test/main.cpp
    basic_store.cpp
    buffer.cpp
    callgrind_test.cpp
    concepts.cpp
    create.cpp
    error.cpp
    file.cpp
    native_file.cpp
    posix_file.cpp
    recover.cpp
    rekey.cpp
    store.cpp
    type_traits.cpp
    verify.cpp
    version.cpp
    visit.cpp
    win32_file.cpp
    xxhasher.cpp
)

if (WIN32)
    target_link_libraries(test-all ${Boost_LIBRARIES})
else ()
    target_link_libraries(test-all ${Boost_LIBRARIES} rt Threads::Threads)
endif ()
