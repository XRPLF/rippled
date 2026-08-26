#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
floatRootImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"float_root"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.floatRoot(benchFloatX(), 2, kBenchMode); });
}
BENCHMARK(floatRootImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
