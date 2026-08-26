#include <xrpl/protocol/SField.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "le_arr_len";

// Declared 40. The signer list's entries counted through a cache slot.
void
ledgerObjArrayLenImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchCachedSignerListHost(); },
        [](auto& host) { return host.getLedgerObjArrayLen(1, sfSignerEntries); });
}
BENCHMARK(ledgerObjArrayLenImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
