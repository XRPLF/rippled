#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/fixtures/BenchFixtures.h>
#include <tx/wasm/fixtures/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
ledgerObjNestedFieldImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"le_inner"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().cachedHost(); },
        [](auto& host) {
            return host.getLedgerObjNestedField(1, FieldLocator{{sfAccount.getCode()}});
        });
}
BENCHMARK(ledgerObjNestedFieldImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
