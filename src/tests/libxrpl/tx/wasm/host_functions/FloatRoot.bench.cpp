#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "float_root";

// Declared 5500, tied with `float_pow` for the most expensive host function that is not a
// signature check. Tied is the thing to question: a root and a power are different algorithms,
// and one price for both is only right if they happen to cost the same.
void
floatRootImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.floatRoot(benchFloatX(), 2, kBenchMode); });
}
BENCHMARK(floatRootImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
