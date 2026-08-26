#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "parent_ldgr_hash";

// Declared 60, the same as the other header getters, but this one returns 32 bytes rather than
// 4 — and on some paths a parent hash is a lookup rather than a field read. If any of the four
// is secretly doing work, it is this one.
void
parentLedgerHashImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.getParentLedgerHash(); });
}
BENCHMARK(parentLedgerHashImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
