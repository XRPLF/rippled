#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "base_fee";

// Declared 60. A field of the current fee schedule; like the other header getters it should be
// far cheaper than its price, with the crossing accounting for nearly all of what a guest pays.
void
baseFeeImpl(benchmark::State& state)
{
    benchmarkImpl(
        state, kWasmName, [] { return benchHost(); }, [](auto& host) { return host.getBaseFee(); });
}
BENCHMARK(baseFeeImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
