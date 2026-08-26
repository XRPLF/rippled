#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
mptokenIssuanceKeyletImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"mpt_issuance_id"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.mptokenIssuanceKeylet(benchAlice().id(), kBenchSeq); });
}
BENCHMARK(mptokenIssuanceKeyletImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
