#include <benchmark/benchmark.h>
#include <tx/wasm/fixtures/BenchFixtures.h>
#include <tx/wasm/fixtures/WasmBench.h>

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
        [] { return Fixtures::instance().host(); },
        [](auto& host) {
            return host.floatSubtract(
                Fixtures::floatX(), Fixtures::floatY(), Fixtures::kRoundingMode);
        });
}
BENCHMARK(floatSubtractImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
