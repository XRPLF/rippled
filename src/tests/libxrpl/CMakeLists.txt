include(GoogleTest)
include(isolate_headers)

# Test requirements.
find_package(GTest REQUIRED)

# Single combined gtest binary built from the shared test helpers and all test
# modules below.
add_executable(
    xrpl_tests
    main.cpp
    helpers/Account.cpp
    helpers/TestSink.cpp
    helpers/TxTest.cpp
)
set_target_properties(
    xrpl_tests
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
# Lets test sources include the shared helpers as <helpers/...>.
target_include_directories(xrpl_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xrpl_tests PRIVATE GTest::gtest xrpl.libxrpl)

# One source subdirectory per module. Network unit tests are currently not
# supported on Windows.
set(test_modules
    basics
    crypto
    json
    tx
    protocol_autogen
)
if(NOT WIN32)
    list(APPEND test_modules net)
endif()

foreach(module IN LISTS test_modules)
    # Append the module's sources (${module}/*.cpp and ${module}.cpp, if any).
    file(
        GLOB_RECURSE sources
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${module}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/${module}.cpp"
    )
    target_sources(xrpl_tests PRIVATE ${sources})

    # Expose the module's private headers under their canonical include path.
    isolate_headers(
        xrpl_tests
        "${CMAKE_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/tests/${module}"
        PRIVATE
    )
endforeach()

gtest_discover_tests(xrpl_tests)
