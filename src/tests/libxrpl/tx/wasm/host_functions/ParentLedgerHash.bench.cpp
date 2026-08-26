#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
parentLedgerHashImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"parent_ldgr_hash"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.getParentLedgerHash(); });
}
BENCHMARK(parentLedgerHashImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
