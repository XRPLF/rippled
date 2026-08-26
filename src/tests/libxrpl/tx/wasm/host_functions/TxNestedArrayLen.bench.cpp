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
constexpr std::string_view kWasmName = "tx_inner_arr_len";

// Declared 70 against a direct count's 40 — the same locator surcharge the nested field getters
// pay, expressed as a different ratio (1.75x here, 1.57x there). Both cannot be right unless a
// locator walk costs a fixed amount, which is what these two pairs check.
void
txNestedArrayLenImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.getTxNestedArrayLen(FieldLocator{{sfMemos.getCode()}}); });
}
BENCHMARK(txNestedArrayLenImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
