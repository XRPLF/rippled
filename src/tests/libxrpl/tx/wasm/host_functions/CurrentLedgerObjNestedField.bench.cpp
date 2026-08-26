#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "home_le_inner";

// Declared 110 against a direct read's 70 — the table's claim that walking a locator costs
// about half a field read again. A one-step locator like this one is the cheapest such walk,
// so it is the best case for that claim.
void
currentLedgerObjNestedFieldImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) {
            return host.getCurrentLedgerObjNestedField(FieldLocator{{sfAccount.getCode()}});
        });
}
BENCHMARK(currentLedgerObjNestedFieldImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
