#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
oracleKeyletImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"oracle_id"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.oracleKeylet(benchAlice().id(), kBenchSeq); });
}
BENCHMARK(oracleKeyletImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
