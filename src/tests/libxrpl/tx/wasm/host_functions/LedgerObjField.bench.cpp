#include <xrpl/protocol/SField.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "le_field";

// Declared 70. The same read as `home_le_field`, but through a cache slot rather than the
// current object — one extra scalar and a slot-table lookup. There is deliberately no
// `ThroughVm` pair: `runEscrowWasm` asserts a clean host, so a benchmark cannot pre-cache a
// slot. `e2e/CacheLedgerObj.cpp` covers the cross-call behaviour instead.
void
ledgerObjFieldImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchCachedHost(); },
        [](auto& host) { return host.getLedgerObjField(1, sfAccount); });
}
BENCHMARK(ledgerObjFieldImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
