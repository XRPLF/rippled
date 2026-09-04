#include <benchmark/benchmark.h>
#include <benchmarks/libxrpl/wasm/BenchFixtures.h>
#include <benchmarks/libxrpl/wasm/WasmBench.h>
#include <tx/wasm/fixtures/WasmRun.h>

#include <cstddef>
#include <format>
#include <string>

namespace xrpl::test::bench {
namespace {

// What a run costs *around* the contract, rather than what its host calls cost. Only the guest's
// own execution is metered, so every stage measured here is wall time no transaction pays for.
// ../README.md has what each case measures and how to read `gas_equivalent`.
//
// **Every case must pin `->Iterations(...)`.** Compiling a module allocates against the
// process-global engine and is never reclaimed — roughly 800 bytes per compile — so Google
// Benchmark's automatic sizing, which targets a wall-clock budget rather than a compile count,
// reaches gigabytes resident and the whole binary stops being able to compile anything.

// The smallest module the engine accepts. Everything a run does to it is overhead by construction.
std::string
minimalWat()
{
    return R"wat((module
  (memory (export "memory") 1)
  (func (export "escrow_finish") (result i32)
    (i32.const 1)))
)wat";
}

// `count` unreachable functions on top of the minimal module: bigger without doing more.
//
// Each body is seeded with its own index so no two are identical and none folds to a constant the
// translator can drop — either would break the link between function count and byte count.
// Unreachable is fine: validation and translation visit every function a module declares, and that
// visit is exactly the cost being swept.
std::string
fillerWat(size_t count)
{
    auto out = std::string{"(module\n  (memory (export \"memory\") 1)\n"};
    for (auto i = 0uz; i < count; ++i)
    {
        out += std::format(
            "  (func $f{0} (param i32) (result i32)\n"
            "    (i32.add (i32.mul (local.get 0) (i32.const {0})) (i32.const {0})))\n",
            i);
    }
    out += "  (func (export \"escrow_finish\") (result i32)\n    (i32.const 1)))\n";
    return out;
}

// The minimal module asking for `pages` of initial memory. `MAX_MEMORY_PAGES` is 128 (8 MiB), and a
// contract declares this for free — the host allocates and zeroes it before the first instruction.
std::string
pagesWat(size_t pages)
{
    return std::format(
        "(module\n"
        "  (memory (export \"memory\") {})\n"
        "  (func (export \"escrow_finish\") (result i32)\n"
        "    (i32.const 1)))\n",
        pages);
}

// Compiles cleanly and is then refused: `env::malloc` is not a namespace the engine serves, so
// `check_imports` stops at it.
std::string
rejectedWat()
{
    return R"wat((module
  (import "env" "malloc" (func $malloc (param i32) (result i32)))
  (memory (export "memory") 1)
  (func (export "escrow_finish") (result i32)
    (i32.const 1)))
)wat";
}

void
preflightMinimal(benchmark::State& state)
{
    static auto const kWasm = assembleWat(minimalWat());
    benchmarkPreflight(state, kWasm);
}
BENCHMARK(preflightMinimal)->UseManualTime()->Iterations(kBenchIterations);

void
preflightRejects(benchmark::State& state)
{
    static auto const kWasm = assembleWat(rejectedWat());
    benchmarkPreflight(state, kWasm, /*expectAccepted*/ false);
}
BENCHMARK(preflightRejects)->UseManualTime()->Iterations(kBenchIterations);

void
runMinimal(benchmark::State& state)
{
    static auto const kWasm = assembleWat(minimalWat());
    benchmarkRun(state, kWasm, [] { return Fixtures::instance().host(); });
}
BENCHMARK(runMinimal)->UseManualTime()->Iterations(kBenchIterations);

void
compileScaling(benchmark::State& state)
{
    auto const wasm = assembleWat(fillerWat(static_cast<size_t>(state.range(0))));
    benchmarkPreflight(state, wasm, /*expectAccepted*/ true, /*sizeSweep*/ true);
}
BENCHMARK(compileScaling)
    ->UseManualTime()
    ->Iterations(kBenchIterations)
    ->Arg(1)
    ->Arg(8)
    ->Arg(64)
    ->Arg(512)
    ->Arg(4096);

void
runScaling(benchmark::State& state)
{
    auto const wasm = assembleWat(fillerWat(static_cast<size_t>(state.range(0))));
    benchmarkRun(state, wasm, [] { return Fixtures::instance().host(); }, /*sizeSweep*/ true);
}
BENCHMARK(runScaling)
    ->UseManualTime()
    ->Iterations(kBenchIterations)
    ->Arg(1)
    ->Arg(8)
    ->Arg(64)
    ->Arg(512)
    ->Arg(4096);

void
instantiateScaling(benchmark::State& state)
{
    auto const wasm = assembleWat(pagesWat(static_cast<size_t>(state.range(0))));
    benchmarkRun(state, wasm, [] { return Fixtures::instance().host(); });
}
BENCHMARK(instantiateScaling)
    ->UseManualTime()
    ->Iterations(kBenchIterations)
    ->Arg(1)
    ->Arg(8)
    ->Arg(32)
    ->Arg(128);

}  // namespace
}  // namespace xrpl::test::bench
