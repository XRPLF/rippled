#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
delegateKeyletImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"delegate_id"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.delegateKeylet(benchAlice().id(), benchBob().id()); });
}
BENCHMARK(delegateKeyletImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
