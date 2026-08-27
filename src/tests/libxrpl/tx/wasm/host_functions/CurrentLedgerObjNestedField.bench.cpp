#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
currentLedgerObjNestedFieldImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"home_le_inner"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().host(); },
        [](auto& host) {
            return host.getCurrentLedgerObjNestedField(FieldLocator{{sfAccount.getCode()}});
        });
}
BENCHMARK(currentLedgerObjNestedFieldImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
