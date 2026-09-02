#include <xrpl/basics/Number.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>

#include <benchmark/benchmark.h>
#include <benchmarks/libxrpl/wasm/BenchFixtures.h>
#include <benchmarks/libxrpl/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
floatFromStNumberImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"float_from_stnumber"};

    auto const number = STNumber{sfNumber, Number(3141592653589793, -15)};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().host(); },
        [&number](auto& host) { return host.floatFromSTNumber(number, Fixtures::kRoundingMode); });
}
BENCHMARK(floatFromStNumberImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
