#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
floatToIntImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"float_to_int"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().host(); },
        [](auto& host) { return host.floatToInt(Fixtures::floatX(), Fixtures::kRoundingMode); });
}
BENCHMARK(floatToIntImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
