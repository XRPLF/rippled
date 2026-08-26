#include <xrpl/protocol/SField.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "tx_field";

// Declared 70. One `getFieldByCode` on the transaction being executed, then serialize the
// result. Its pair with `CurrentLedgerObjField` (same price, an object instead of a tx) shows
// whether the source matters to the cost; the declared table says it does not.
void
txFieldImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.getTxField(sfAccount); });
}
BENCHMARK(txFieldImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
