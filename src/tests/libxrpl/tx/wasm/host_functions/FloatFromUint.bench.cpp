#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
floatFromUintImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"float_from_uint"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.floatFromUint(3141592653589793u, kBenchMode); });
}
BENCHMARK(floatFromUintImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
