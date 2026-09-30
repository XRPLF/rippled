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

# Gas calibration for the wasm host functions. The ledger and real host come from
# `xrpl.testkit.wasm` (built with the tests, but framework-free), so this target links
# no GTest and no GMock.
if(TARGET xrpl.testkit.wasm)
    xrpl_add_benchmark(wasm)
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
