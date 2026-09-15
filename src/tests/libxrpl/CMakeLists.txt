include(GoogleTest)
include(isolate_headers)
include(verify_headers)

# Test requirements.
find_package(GTest REQUIRED)

add_library(
    xrpl.testkit.wasm
    STATIC
    helpers/Account.cpp
    helpers/TestSink.cpp
    helpers/TxTest.cpp
    tx/wasm/fixtures/NftSetup.cpp
    tx/wasm/fixtures/WasmLedger.cpp
    tx/wasm/fixtures/WasmRun.cpp
)
target_include_directories(xrpl.testkit.wasm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
    xrpl.testkit.wasm
    PUBLIC xrpl.libxrpl xrpl_wasm_testkit_cxxbridge
)
add_dependencies(xrpl.testkit.wasm xrpl_crates)

add_executable(xrpl_tests main.cpp)
patch_nix_binary(xrpl_tests)
set_target_properties(
    xrpl_tests
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
# Lets test sources include the shared helpers as <helpers/...>.
target_include_directories(xrpl_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
    xrpl_tests
    PRIVATE GTest::gtest GTest::gmock xrpl.libxrpl xrpl.testkit.wasm
)

# One source subdirectory per module. Network unit tests are currently not
# supported on Windows.
set(test_modules
    basics
    beast
    consensus
    crypto
    json
    nodestore
    peerfinder
    protocol
    resource
    shamap
    tx
    protocol_autogen
    server
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
    # The framework-free half of tx/wasm/fixtures/ is compiled into xrpl.testkit.wasm,
    # which this binary links; the rest of that folder belongs here.
    list(
        FILTER sources
        EXCLUDE
        REGEX "/fixtures/(NftSetup|WasmLedger|WasmRun)\\.cpp$"
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

gtest_discover_tests(xrpl_tests DISCOVERY_TIMEOUT 60)
