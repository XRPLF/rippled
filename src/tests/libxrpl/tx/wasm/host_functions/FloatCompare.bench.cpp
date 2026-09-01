#include <benchmark/benchmark.h>
#include <tx/wasm/fixtures/BenchFixtures.h>
#include <tx/wasm/fixtures/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
floatCompareImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"float_cmp"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().host(); },
        [](auto& host) { return host.floatCompare(Fixtures::floatX(), Fixtures::floatY()); });
}
BENCHMARK(floatCompareImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
