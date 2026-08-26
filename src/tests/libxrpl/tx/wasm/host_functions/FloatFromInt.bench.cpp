#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
floatFromIntImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"float_from_int"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.floatFromInt(3141592653589793, kBenchMode); });
}
BENCHMARK(floatFromIntImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
