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
constexpr std::string_view kWasmName = "le_inner";

// Declared 110. A locator walk over a cached object: the nested read plus the slot lookup that
// `LedgerObjField` measures on its own.
void
ledgerObjNestedFieldImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchCachedHost(); },
        [](auto& host) {
            return host.getLedgerObjNestedField(1, FieldLocator{{sfAccount.getCode()}});
        });
}
BENCHMARK(ledgerObjNestedFieldImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
