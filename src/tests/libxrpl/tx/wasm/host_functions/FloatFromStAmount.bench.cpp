#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/UintTypes.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "float_from_stamount";

// Declared 150, the joint-highest of the float conversions. Unlike `float_from_int` it starts
// from a serialized ledger type, so it pays a parse before the conversion — which is what the
// 50% premium over `float_from_int`'s 100 is for.
void
floatFromStAmountImpl(benchmark::State& state)
{
    auto const amount = STAmount{Issue{toCurrency("USD"), benchAlice().id()}, 1234567, -3};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [&amount](auto& host) { return host.floatFromSTAmount(amount, kBenchMode); });
}
BENCHMARK(floatFromStAmountImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
