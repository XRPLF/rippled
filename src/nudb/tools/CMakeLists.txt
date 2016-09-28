# Part of nudb

GroupSources (include/nudb nudb)
GroupSources (extras/nudb extras)
GroupSources (tools "/")

add_executable (nudb
    ${NUDB_INCLUDES}
    ${EXTRAS_INCLUDES}
    nudb.cpp
)

if (WIN32)
    target_link_libraries (nudb ${Boost_LIBRARIES})
else ()
    target_link_libraries (nudb ${Boost_LIBRARIES} rt Threads::Threads)
endif ()
