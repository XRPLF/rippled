#include <xrpl/protocol/SField.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
ledgerObjFieldImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"le_field"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().cachedHost(); },
        [](auto& host) { return host.getLedgerObjField(1, sfAccount); });
}
BENCHMARK(ledgerObjFieldImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
