include(XrplAddBenchmark)

# Benchmark requirements.
find_package(benchmark REQUIRED)

# Custom target for all benchmarks defined in this file.
add_custom_target(xrpl.benchmarks)

# Common library dependencies for every benchmark module. `benchmark_main`
# supplies a `main()` that parses the standard Google Benchmark CLI flags
# (`--benchmark_filter`, `--benchmark_format`, ...), so no per-module main.cpp
# is needed.
add_library(xrpl.imports.bench INTERFACE)
target_link_libraries(
    xrpl.imports.bench
    INTERFACE benchmark::benchmark_main xrpl.libxrpl
)

# One benchmark executable for each module.
xrpl_add_benchmark(nodestore)
target_link_libraries(xrpl.bench.nodestore PRIVATE xrpl.imports.bench)
add_dependencies(xrpl.benchmarks xrpl.bench.nodestore)

# xrpl.bench.wasm — gas calibration for the wasm host functions.
#
# Each `*.bench.cpp` sits beside the test that covers the same host function, under
# `src/tests/libxrpl/tx/wasm/`. Keeping the benchmark next to the test means the
# two move together and share one fixture; a separate executable means benchmark
# runtime never lands on the `ctest` path (the test binary filters `*.bench.cpp`
# back out).
#
# Reusing those fixtures means compiling a few test sources and linking GTest:
# `RealHostFixture` derives from `testing::Test`. Nothing here registers a test.
find_package(GTest QUIET)
if(TARGET GTest::gtest)
    file(
        GLOB_RECURSE wasm_bench_sources
        CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/tests/libxrpl/tx/wasm/*.bench.cpp"
    )

    add_executable(
        xrpl.bench.wasm
        ${wasm_bench_sources}
        "${CMAKE_SOURCE_DIR}/src/tests/libxrpl/helpers/Account.cpp"
        "${CMAKE_SOURCE_DIR}/src/tests/libxrpl/helpers/TestSink.cpp"
        "${CMAKE_SOURCE_DIR}/src/tests/libxrpl/helpers/TxTest.cpp"
        "${CMAKE_SOURCE_DIR}/src/tests/libxrpl/tx/wasm/RealHostFixture.cpp"
        "${CMAKE_SOURCE_DIR}/src/tests/libxrpl/tx/wasm/NFTFixture.cpp"
        # The benchmark harness and its shared ledger setup. Named without the
        # `.bench.cpp` suffix because they are infrastructure rather than a benchmark,
        # so the glob above does not find them and `xrpl_tests` excludes them by name.
        "${CMAKE_SOURCE_DIR}/src/tests/libxrpl/tx/wasm/BenchFixtures.cpp"
        "${CMAKE_SOURCE_DIR}/src/tests/libxrpl/tx/wasm/WasmBench.cpp"
        # The WAT assembler, shared with the test binary rather than benchmark-only:
        # both binaries compile their own copy.
        "${CMAKE_SOURCE_DIR}/src/tests/libxrpl/tx/wasm/WasmRun.cpp"
    )

    # Google Benchmark registers cases through static registrars declared in
    # anonymous namespaces; merging several such files into one unity translation
    # unit collides them. Same reason as `xrpl_add_benchmark`.
    #
    # The output directory matches too: benchmarks land in the build root beside
    # `xrpl_tests`, because they are run by hand and comparing two of them should
    # not mean typing two long paths.
    set_target_properties(
        xrpl.bench.wasm
        PROPERTIES
            UNITY_BUILD OFF
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )

    # The fixtures include their siblings as <helpers/...> and <tx/wasm/...>, the
    # same spelling the test binary gives them.
    target_include_directories(
        xrpl.bench.wasm
        PRIVATE "${CMAKE_SOURCE_DIR}/src/tests/libxrpl"
    )
    target_link_libraries(
        xrpl.bench.wasm
        PRIVATE
            xrpl.imports.bench
            GTest::gtest
            GTest::gmock
            xrpl_wasm_testkit_cxxbridge
    )
    add_dependencies(xrpl.bench.wasm xrpl_crates)

    # Hardware performance counters (INSTRUCTIONS, CYCLES, ...) come from libpfm,
    # which Google Benchmark only builds on Linux (see the `enable_libpfm` option
    # set in conanfile.py). Where it is available the benchmarks advertise the
    # `--benchmark_perf_counters=...` flag; elsewhere they report wall time only.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(
            xrpl.bench.wasm
            PRIVATE XRPL_BENCH_PERF_COUNTERS=1
        )
    endif()

    add_dependencies(xrpl.benchmarks xrpl.bench.wasm)
else()
    message(
        STATUS
        "GTest not found; skipping xrpl.bench.wasm (it reuses the test fixtures)."
    )
endif()
