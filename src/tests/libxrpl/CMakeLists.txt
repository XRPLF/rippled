include(GoogleTest)
include(isolate_headers)
include(verify_headers)

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
patch_nix_binary(xrpl_tests)
set_target_properties(
    xrpl_tests
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
# Lets test sources include the shared helpers as <helpers/...>.
target_include_directories(xrpl_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# Some headers under test live in src/xrpld/ rather than in libxrpl (for
# example app/ledger/AcquireStats.h and telemetry/ValidationTracker.h), so put
# src/ on the include path to reach them as <xrpld/...>. This is unconditional
# because header-only ones are testable in every build, telemetry or not.
target_include_directories(xrpl_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(xrpl_tests PRIVATE GTest::gtest GTest::gmock xrpl.libxrpl)

# One source subdirectory per module. Network unit tests are currently not
# supported on Windows.
set(test_modules
    basics
    beast
    consensus
    crypto
    json
    ledger
    nodestore
    peerfinder
    protocol
    resource
    shamap
    tx
    protocol_autogen
    server
    telemetry
)
if(NOT WIN32)
    list(APPEND test_modules net)
endif()
if(rust)
    target_link_libraries(xrpl_tests PRIVATE rs_hello_world_cxxbridge)
endif()

foreach(module IN LISTS test_modules)
    # Append the module's sources (${module}/*.cpp and ${module}.cpp, if any).
    file(
        GLOB_RECURSE sources
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${module}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/${module}.cpp"
    )
    if(NOT rust)
        # Tests of the Rust interop include generated cxxbridge headers, which
        # do not exist without the crates, so keep them out of the build tree
        # entirely. They are named `Rust<something>.cpp`.
        list(FILTER sources EXCLUDE REGEX "/Rust[^/]*\\.cpp$")
    endif()
    target_sources(xrpl_tests PRIVATE ${sources})

    # Expose the module's private headers under their canonical include path.
    isolate_headers(
        xrpl_tests
        "${CMAKE_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/tests/${module}"
        PRIVATE
    )
endforeach()

# The consensus tests use the CSF (Consensus Simulation Framework) helpers, so
# compile the CSF sources into the test binary. The consensus engine itself now
# lives in libxrpl, so no xrpld sources or include paths are needed here.
file(
    GLOB_RECURSE csf_sources
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/csf/*.cpp"
)
target_sources(xrpl_tests PRIVATE ${csf_sources})

# The test helpers and per-module test headers are not built with add_module,
# so verify them against the test binary's own compile environment.
if(verify_headers)
    verify_target_headers(xrpl_tests "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# The telemetry test module exercises SpanGuard/Telemetry, which link against
# the OpenTelemetry SDK when the telemetry build option is enabled.
if(telemetry)
    # The SpanGuardScope tests drive an InMemorySpanExporter to read exported
    # spans (test-only; production never uses it). The Conan package ships
    # libopentelemetry_exporter_in_memory.a but declares the component with no
    # libs, so neither the umbrella target nor opentelemetry-cpp::exporter_in_memory
    # puts the archive on the link line. Locate it by path and link it BEFORE
    # the umbrella so its undefined SDK references resolve against the umbrella.
    find_library(
        OTEL_IN_MEMORY_EXPORTER_LIB
        NAMES opentelemetry_exporter_in_memory
        HINTS "${opentelemetry-cpp_PACKAGE_FOLDER_RELEASE}/lib"
        REQUIRED
    )
    target_link_libraries(
        xrpl_tests
        PRIVATE
            "${OTEL_IN_MEMORY_EXPORTER_LIB}"
            opentelemetry-cpp::opentelemetry-cpp
    )
    # ValidationTracker lives in src/xrpld/ (not libxrpl), so we compile its
    # implementation directly into the test binary. The src/ include path its
    # tests need is added unconditionally above.
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
