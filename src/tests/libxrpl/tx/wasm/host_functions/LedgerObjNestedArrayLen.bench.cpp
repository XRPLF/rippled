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
constexpr std::string_view kWasmName = "le_inner_arr_len";

// Declared 70. The deepest read in the family: slot lookup, locator walk, then a count.
void
ledgerObjNestedArrayLenImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchCachedSignerListHost(); },
        [](auto& host) {
            return host.getLedgerObjNestedArrayLen(1, FieldLocator{{sfSignerEntries.getCode()}});
        });
}
BENCHMARK(ledgerObjNestedArrayLenImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
