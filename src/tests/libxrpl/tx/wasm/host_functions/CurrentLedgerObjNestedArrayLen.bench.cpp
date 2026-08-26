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
constexpr std::string_view kWasmName = "home_le_inner_arr_len";

// Declared 70. A locator walk to the signer list's entries, then a count.
void
currentLedgerObjNestedArrayLenImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchSignerListHost(); },
        [](auto& host) {
            return host.getCurrentLedgerObjNestedArrayLen(
                FieldLocator{{sfSignerEntries.getCode()}});
        });
}
BENCHMARK(currentLedgerObjNestedArrayLenImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
