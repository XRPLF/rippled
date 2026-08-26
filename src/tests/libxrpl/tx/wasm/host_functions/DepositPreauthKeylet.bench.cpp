#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
depositPreauthKeyletImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"deposit_preauth_id"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.depositPreauthKeylet(benchAlice().id(), benchBob().id()); });
}
BENCHMARK(depositPreauthKeyletImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
