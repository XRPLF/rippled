#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
signerListKeyletImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"signers_id"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.signerListKeylet(benchAlice().id()); });
}
BENCHMARK(signerListKeyletImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
