#include <benchmark/benchmark.h>
#include <tx/wasm/fixtures/BenchFixtures.h>
#include <tx/wasm/fixtures/WasmBench.h>

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
        [] { return Fixtures::instance().host(); },
        [](auto& host) { return host.floatFromInt(3141592653589793, Fixtures::kRoundingMode); });
}
BENCHMARK(floatFromIntImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
