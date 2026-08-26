#include <xrpl/basics/Number.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "float_from_stnumber";

// Declared 150, the same as `float_from_stamount`. An `STNumber` is already a mantissa and an
// exponent, so this conversion has strictly less to do than one from an `STAmount` — pricing
// them identically is the claim under test.
void
floatFromStNumberImpl(benchmark::State& state)
{
    auto const number = STNumber{sfNumber, Number(3141592653589793, -15)};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [&number](auto& host) { return host.floatFromSTNumber(number, kBenchMode); });
}
BENCHMARK(floatFromStNumberImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
