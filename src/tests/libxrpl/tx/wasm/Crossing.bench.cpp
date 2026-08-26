#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The harness checking itself.
//
// This file holds no host function — it belongs to the wasm directory rather than
// `host_functions/` because it measures the two reference points every per-function number is read
// against, and neither is a host call.
//
// `GuestInstruction` runs a contract whose "host call" is a couple of guest instructions. Its
// `implied_gas` and `charged_gas` are then two independent measurements of the same quantity — one
// from wall time via `secondsPerGas`, one from the engine's own fuel meter — and they should agree
// closely. When they diverge, `secondsPerGas` has measured something other than a guest
// instruction and no other number in the run is trustworthy. Read this first.
//
// The crossing floor is the other reference point, and it lives in `host_functions/LedgerSqn`:
// `ldgr_index` takes no input and answers from a header already in hand, so its impl is as close
// to nothing as a host function gets, and whatever its `ThroughVm` case costs above its `Impl`
// case is the price of leaving the guest — paid by every one of the 61 functions before any of
// them does any work.
//
// So the reading order across the suite is: this file, then `LedgerSqn`'s pair for the floor, then
// a function's own `Impl` number. Those three should account for its `ThroughVm` number; where
// they do not, the gap is size-dependent copying, which the swept cases (`Sha512Half`,
// `UpdateData`) expose.

void
guestInstruction(benchmark::State& state)
{
    static constexpr std::string_view kBody = "(i32.add (local.get $r) (i32.const 1))";
    // Empty import name: this case prices no host function, so there is nothing to look a
    // declaration up for and it reports no `suggested_gas`.
    benchmarkThroughVm(state, "", "", "", kBody, [] { return benchHost(); });
}
BENCHMARK(guestInstruction)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
