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
    telemetry
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

# The telemetry test module exercises SpanGuard/Telemetry, which link against
# the OpenTelemetry SDK when the telemetry build option is enabled.
if(telemetry)
    target_link_libraries(
        xrpl_tests
        PRIVATE opentelemetry-cpp::opentelemetry-cpp
    )
    # ValidationTracker lives in src/xrpld/ (not libxrpl), so we compile its
    # implementation directly into the test binary and put src/ on the include
    # path so its tests can reach <xrpld/telemetry/...> headers.
    target_include_directories(xrpl_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_sources(
        xrpl_tests
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src/xrpld/telemetry/detail/ValidationTracker.cpp
    )
else()
    # MetricsRegistry lives in xrpld; compile its .cpp directly into the test
    # target so the no-op path can be tested without linking all of xrpld.
    # When telemetry=ON, XRPL_ENABLE_TELEMETRY is globally defined and the
    # .cpp pulls in xrpld symbols we cannot satisfy here.
    target_sources(
        xrpl_tests
        PRIVATE ${CMAKE_SOURCE_DIR}/src/xrpld/telemetry/MetricsRegistry.cpp
    )
endif()

gtest_discover_tests(xrpl_tests DISCOVERY_TIMEOUT 60)
