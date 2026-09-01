#include <benchmark/benchmark.h>
#include <tx/wasm/fixtures/BenchFixtures.h>
#include <tx/wasm/fixtures/WasmBench.h>

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
        [] { return Fixtures::instance().host(); },
        [](auto& host) {
            return host.floatMultiply(
                Fixtures::floatX(), Fixtures::floatY(), Fixtures::kRoundingMode);
        });
}
BENCHMARK(floatMultiplyImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
