#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
floatMultiplyImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"float_mult"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.floatMultiply(benchFloatX(), benchFloatY(), kBenchMode); });
}
BENCHMARK(floatMultiplyImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
