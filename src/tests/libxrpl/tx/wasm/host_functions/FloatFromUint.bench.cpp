#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "float_from_uint";

// Declared 130 against `float_from_int`'s 100. The same conversion from an unsigned value, so
// the 30% surcharge is a claim about the wider range needing more normalization work.
void
floatFromUintImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.floatFromUint(3141592653589793u, kBenchMode); });
}
BENCHMARK(floatFromUintImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
