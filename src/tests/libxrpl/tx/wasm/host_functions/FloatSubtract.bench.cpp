#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
floatSubtractImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"float_sub"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.floatSubtract(benchFloatX(), benchFloatY(), kBenchMode); });
}
BENCHMARK(floatSubtractImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
