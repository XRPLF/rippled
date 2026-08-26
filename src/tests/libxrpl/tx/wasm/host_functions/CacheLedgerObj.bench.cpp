#include <xrpl/protocol/Indexes.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "cache_le";

// Declared 5000 — seventy times a field read, and the second-most expensive price in the table.
//
// The number is presumably set for a *cold* lookup: find an object in the ledger and pin it to a
// slot. But it is charged per call, and after the first call the view has the object cached, so
// a contract can arrange never to pay the cold cost again. This case measures the warm path. If
// the gap is large, 5000 is wrong in the common case; if it is small, it is wrong in the cold
// one. One number cannot be right for both.
//
// Re-caching the same key into the same slot is idempotent, which is what makes repeating it a
// measurement rather than a slot-table exhaustion test.
void
cacheLedgerObjImpl(benchmark::State& state)
{
    auto const key = keylet::account(benchAlice().id()).key;

    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [&key](auto& host) { return host.cacheLedgerObj(key, 1); });
}
BENCHMARK(cacheLedgerObjImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
