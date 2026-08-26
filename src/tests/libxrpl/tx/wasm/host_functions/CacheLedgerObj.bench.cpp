#include <xrpl/protocol/Indexes.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
cacheLedgerObjImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"cache_le"};

    auto const key = keylet::account(benchAlice().id()).key;

    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [&key](auto& host) { return host.cacheLedgerObj(key, 1); });
}
BENCHMARK(cacheLedgerObjImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
