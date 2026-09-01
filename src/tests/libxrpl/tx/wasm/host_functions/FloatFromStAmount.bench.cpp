#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/UintTypes.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/fixtures/BenchFixtures.h>
#include <tx/wasm/fixtures/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
floatFromStAmountImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"float_from_stamount"};

    auto const amount =
        STAmount{Issue{toCurrency("USD"), Fixtures::instance().alice().id()}, 1234567, -3};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().host(); },
        [&amount](auto& host) { return host.floatFromSTAmount(amount, Fixtures::kRoundingMode); });
}
BENCHMARK(floatFromStAmountImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
