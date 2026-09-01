#include <benchmark/benchmark.h>
#include <tx/wasm/fixtures/BenchFixtures.h>
#include <tx/wasm/fixtures/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
floatFromMantExpImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"float_from_mant_exp"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().host(); },
        [](auto& host) {
            return host.floatFromMantExp(3141592653589793, -15, Fixtures::kRoundingMode);
        });
}
BENCHMARK(floatFromMantExpImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
