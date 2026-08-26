#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
offerKeyletImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"offer_id"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.offerKeylet(benchAlice().id(), kBenchSeq); });
}
BENCHMARK(offerKeyletImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
