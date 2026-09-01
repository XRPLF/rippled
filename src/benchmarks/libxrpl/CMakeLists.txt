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
# `src/tests/libxrpl/tx/wasm/`, so the two move together. A separate executable keeps
# benchmark runtime off the `ctest` path (the test binary filters `*.bench.cpp` out).
#
# The ledger and host come from `xrpl.testkit.wasm`, which links no test framework — so
# this target links **no GTest and no GMock**, and compiles no test sources of its own
# beyond the benchmark harness.
if(TARGET xrpl.testkit.wasm)
    file(
        GLOB_RECURSE wasm_bench_sources
        CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/tests/libxrpl/tx/wasm/*.bench.cpp"
    )

    add_executable(
        xrpl.bench.wasm
        ${wasm_bench_sources}
        # The benchmark harness. Named without the `.bench.cpp` suffix because it is
        # infrastructure rather than a benchmark, so the glob above does not find it and
        # `xrpl_tests` excludes it by name.
        "${CMAKE_SOURCE_DIR}/src/tests/libxrpl/tx/wasm/fixtures/BenchFixtures.cpp"
        "${CMAKE_SOURCE_DIR}/src/tests/libxrpl/tx/wasm/fixtures/WasmBench.cpp"
    )

    # Google Benchmark registers cases through static registrars declared in anonymous
    # namespaces; merging several such files into one unity translation unit collides
    # them. Same reason as `xrpl_add_benchmark`.
    #
    # The output directory matches too: benchmarks land in the build root beside
    # `xrpl_tests`, because they are run by hand and comparing two of them should not
    # mean typing two long paths.
    set_target_properties(
        xrpl.bench.wasm
        PROPERTIES
            UNITY_BUILD OFF
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )

    target_link_libraries(
        xrpl.bench.wasm
        PRIVATE xrpl.imports.bench xrpl.testkit.wasm
    )

    add_dependencies(xrpl.benchmarks xrpl.bench.wasm)
else()
    message(
        STATUS
        "xrpl.testkit.wasm not built (tests disabled); skipping xrpl.bench.wasm."
    )
endif()
