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

# Number vs. decimal floating-point libraries. See docs/NumberDecimalBenchmark.md.
# Boost.Decimal is header-only and ships with Boost >= 1.91.
xrpl_add_benchmark(number)
target_link_libraries(xrpl.bench.number PRIVATE xrpl.imports.bench)
add_dependencies(xrpl.benchmarks xrpl.bench.number)

# Benchmark a 128-bit Number prototype if one exists (see number/Number128.h).
# CONFIGURE_DEPENDS re-checks the glob on every build, so adding or removing
# the header is enough to reconfigure and rebuild.
file(
    GLOB number128_header
    CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/include/xrpl/basics/Number128.h"
)
if(number128_header)
    set(XRPL_BENCH_NUMBER128 ON)
    target_compile_definitions(xrpl.bench.number PRIVATE XRPL_BENCH_NUMBER128)
endif()

if(bench_intel_dfp)
    # Intel Decimal Floating-Point Math Library, fetched and built with its own
    # makefile for local benchmarking only. If it is ever used beyond that, it
    # should become a Conan recipe instead.
    #
    # CALL_BY_REF=0 GLOBAL_RND=0 GLOBAL_FLAGS=0: arguments by value, rounding
    # mode and status flags passed to every call. number/IntelBid.h defines the
    # matching DECIMAL_* macros. The upstream makefile adds no optimization
    # flags of its own, so pass -O3 to match the Release build.
    include(ExternalProject)
    find_program(XRPL_MAKE_EXECUTABLE NAMES gmake make REQUIRED)
    set(intel_dfp_dir "${CMAKE_CURRENT_BINARY_DIR}/intel_dfp")
    ExternalProject_Add(
        intel_dfp
        URL https://netlib.org/misc/intel/IntelRDFPMathLib20U4.tar.gz
        URL_HASH
            SHA256=1df86132e7a31fd74d784fee1c679b21a088f73a8ec979cfaf784c200392e125
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_DIR "${intel_dfp_dir}"
        BUILD_IN_SOURCE TRUE
        CONFIGURE_COMMAND ""
        BUILD_COMMAND
            ${XRPL_MAKE_EXECUTABLE} -C LIBRARY CC=${CMAKE_C_COMPILER}
            CALL_BY_REF=0 GLOBAL_RND=0 GLOBAL_FLAGS=0 UNCHANGED_BINARY_FLAGS=0
            "CFLAGS_OPT=-O3 -fPIC"
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS "${intel_dfp_dir}/LIBRARY/libbid.a"
    )
    # Imported targets require their include directory to exist at generate time.
    file(MAKE_DIRECTORY "${intel_dfp_dir}/LIBRARY/src")
    add_library(xrpl.imports.intel_dfp STATIC IMPORTED)
    set_target_properties(
        xrpl.imports.intel_dfp
        PROPERTIES
            IMPORTED_LOCATION "${intel_dfp_dir}/LIBRARY/libbid.a"
            INTERFACE_INCLUDE_DIRECTORIES "${intel_dfp_dir}/LIBRARY/src"
    )
    add_dependencies(xrpl.imports.intel_dfp intel_dfp)

    target_link_libraries(xrpl.bench.number PRIVATE xrpl.imports.intel_dfp)
    target_compile_definitions(xrpl.bench.number PRIVATE XRPL_BENCH_INTEL_DFP)
endif()

if(bench_mpdecimal)
    find_package(mpdecimal REQUIRED)
    target_link_libraries(xrpl.bench.number PRIVATE mpdecimal::libmpdecimal)
    target_compile_definitions(xrpl.bench.number PRIVATE XRPL_BENCH_MPDECIMAL)

    # The divergence harness is a plain executable, not a Google Benchmark, so
    # it is defined here rather than with xrpl_add_benchmark. It shares the
    # adapters in number/.
    add_executable(xrpl.bench.number_diff number_diff/main.cpp)
    isolate_headers(
        xrpl.bench.number_diff
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/number"
        PRIVATE
    )
    target_link_libraries(
        xrpl.bench.number_diff
        PRIVATE xrpl.libxrpl mpdecimal::libmpdecimal
    )
    if(bench_intel_dfp)
        target_link_libraries(
            xrpl.bench.number_diff
            PRIVATE xrpl.imports.intel_dfp
        )
        target_compile_definitions(
            xrpl.bench.number_diff
            PRIVATE XRPL_BENCH_INTEL_DFP
        )
    endif()
    if(XRPL_BENCH_NUMBER128)
        target_compile_definitions(
            xrpl.bench.number_diff
            PRIVATE XRPL_BENCH_NUMBER128
        )
    endif()
    add_dependencies(xrpl.benchmarks xrpl.bench.number_diff)
endif()
