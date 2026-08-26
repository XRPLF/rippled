#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "trace";

// Declared 30, the cheapest price in the table, and the only host function that answers nothing.
//
// 30 is right for the case that matters: a validator with tracing off, where the call renders
// nothing and returns. But the same 30 is charged when the sink *is* enabled and the host formats
// a message and writes it. A contract cannot choose which node it runs on, so the price has to be
// set for the cheap case and the expensive one has to be a node's own problem. These two cases
// measure how far apart they are, which is how you decide whether that reasoning still holds.
//
// No `ThroughVm` case: `trace` has no result for the harness's `result >= 0` guard to check, and
// its input shape (two regions in, nothing out) is already priced by `Sha512Half.bench.cpp`.

constexpr auto kMessage = std::string_view{"benchmark trace message"};
constexpr auto kData = std::string_view{"0123456789abcdef"};

// The path a validator actually runs: journal pointed at a null sink.
void
traceDisabledImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchLedger().makeHost(); },
        [](auto& host) { return host.trace(kMessage, kData); });
}
BENCHMARK(traceDisabledImpl)->UseManualTime()->Iterations(kBenchIterations);

// The same call against a host whose sink records what it is given. The gap over the case above
// is the cost the flat 30 does not cover.
void
traceEnabledImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchLedger().makeTracingHost(); },
        [](auto& host) { return host.trace(kMessage, kData); });
}
BENCHMARK(traceEnabledImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
