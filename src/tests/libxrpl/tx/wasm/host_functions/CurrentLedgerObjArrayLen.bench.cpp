#include <xrpl/protocol/SField.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "home_le_arr_len";

// Declared 40. Runs against a signer list rather than an account root, which has no arrays at
// all — a `FieldNotFound` answer would time the rejection instead of the count.
void
currentLedgerObjArrayLenImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchSignerListHost(); },
        [](auto& host) { return host.getCurrentLedgerObjArrayLen(sfSignerEntries); });
}
BENCHMARK(currentLedgerObjArrayLenImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
