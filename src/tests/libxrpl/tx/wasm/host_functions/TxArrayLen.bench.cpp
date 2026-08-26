#include <xrpl/protocol/SField.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "tx_arr_len";

// Declared 40, the cheapest non-`trace` price in the table. Counting an array's elements does
// not serialize them, which is what justifies pricing it below a field read's 70 — this case
// and `TxField` together are whether that 40/70 split is the right size.
void
txArrayLenImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.getTxArrayLen(sfMemos); });
}
BENCHMARK(txArrayLenImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
