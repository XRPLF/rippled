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
target_link_libraries(xrpl_tests PRIVATE GTest::gtest GTest::gmock xrpl.libxrpl)

target_link_libraries(xrpl_tests PRIVATE xrpl_wasm_testkit_cxxbridge)
add_dependencies(xrpl_tests xrpl_crates)

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
    # `*.bench.cpp` files live beside the tests they measure, but belong to the
    # benchmark executables (src/benchmarks/libxrpl), not to this one: they define
    # Google Benchmark registrars, and running them under ctest would make a test
    # run take minutes. Keep them out of the test binary.
    list(FILTER sources EXCLUDE REGEX "\\.bench\\.cpp$")
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

# `tx/wasm/WasmBench.h` and `tx/wasm/BenchFixtures.h` sit beside the tests they
# measure but belong to `xrpl.bench.wasm`, so they include <benchmark/benchmark.h>
# while nothing in this binary does. `verify_target_headers` below compiles *every*
# header under this directory with this target's flags, so without the benchmark
# headers on the path those two fail to parse — which also makes clang-tidy's
# include-cleaner report nonsense for everything after the failed include.
#
# Carrying the include directory (not the library) is enough: the test binary
# compiles nothing that uses Google Benchmark, and links nothing from it.
if(benchmark AND TARGET benchmark::benchmark)
    target_include_directories(
        xrpl_tests
        PRIVATE
            $<TARGET_PROPERTY:benchmark::benchmark,INTERFACE_INCLUDE_DIRECTORIES>
    )
endif()

# The test helpers and per-module test headers are not built with add_module,
# so verify them against the test binary's own compile environment.
if(verify_headers)
    verify_target_headers(xrpl_tests "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

gtest_discover_tests(xrpl_tests DISCOVERY_TIMEOUT 60)
