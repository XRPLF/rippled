#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "parent_ldgr_time";

// Declared 60. No input, and the answer is a field of a header already in hand, so this should
// measure at essentially nothing and the crossing should dominate it entirely. Read against
// `Crossing.bench.cpp`, which uses `ldgr_index` — the same shape — as the crossing floor.
void
parentLedgerTimeImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.getParentLedgerTime(); });
}
BENCHMARK(parentLedgerTimeImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
