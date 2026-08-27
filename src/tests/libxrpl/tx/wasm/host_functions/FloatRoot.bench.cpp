#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
floatRootImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"float_root"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().host(); },
        [](auto& host) { return host.floatRoot(Fixtures::floatX(), 2, Fixtures::kRoundingMode); });
}
BENCHMARK(floatRootImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
