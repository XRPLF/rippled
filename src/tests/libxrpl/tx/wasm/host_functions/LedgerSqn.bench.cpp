#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "ldgr_index";

// Declared 60, and the cheapest host call there is: no input, and the answer is a field of a
// header already in hand. That makes it the crossing's reference point — whatever the `ThroughVm`
// case costs above the `Impl` case is the floor price of leaving the guest, paid by all 61
// functions before any of them does any work. `Crossing.bench.cpp` reads against these two.

constexpr std::string_view kImport =
    R"(  (import "host_lib" "ldgr_index" (func $ldgr_index (param i32 i32) (result i32)))
)";

void
ledgerSqnThroughVm(benchmark::State& state)
{
    benchmarkThroughVm(
        state, kWasmName, kImport, "", "(call $ldgr_index (i32.const 0) (i32.const 4))", [] {
            return benchHost();
        });
}
BENCHMARK(ledgerSqnThroughVm)->UseManualTime()->Iterations(kBenchIterations);

void
ledgerSqnImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.getLedgerSqn(); });
}
BENCHMARK(ledgerSqnImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
