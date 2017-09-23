# Part of Beast

GroupSources(extras/beast extras)
GroupSources(include/beast beast)

GroupSources(test/benchmarks "/")
GroupSources(test/http "/")

add_executable (benchmarks
    ${BEAST_INCLUDES}
    ${EXTRAS_INCLUDES}
    ../../extras/beast/unit_test/main.cpp
    ../http/message_fuzz.hpp
    nodejs_parser.hpp
    buffers.cpp
    nodejs_parser.cpp
    parser.cpp
    utf8_checker.cpp
)

target_link_libraries(benchmarks
    Beast
    ${Boost_PROGRAM_OPTIONS_LIBRARY}
    ${Boost_FILESYSTEM_LIBRARY}
    )
