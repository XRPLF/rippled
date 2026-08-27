#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
parentLedgerTimeImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"parent_ldgr_time"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().host(); },
        [](auto& host) { return host.getParentLedgerTime(); });
}
BENCHMARK(parentLedgerTimeImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
