#include <xrpl/basics/Slice.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <cstddef>
#include <string>
#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "sha512_half";

// Declared 2000 — a single flat price, no matter how many bytes it is asked to hash. Hashing is
// linear in its input, so the declaration can only be exactly right at one length; the `Range`
// here shows where that length is, and how far the flat price misses at the ends.
//
// What bounds the damage is `MAX_FIELD_BYTES` (crates/xrpl-wasm-vm/src/region.rs): no single value
// may cross the boundary in either direction above 1 KiB, `DataFieldTooLarge` otherwise. So the
// flat price is wrong over a 128x span, not an unbounded one — the worst a contract can extract is
// the ratio between hashing 1 KiB and hashing 8 bytes, both for 2000 gas. That is what this sweep
// puts a figure on, and why the range stops at 1024: past that the `ThroughVm` case cannot run at
// all, and comparing it against an `Impl` case that can would be measuring two different things.

constexpr int kMaxBytes = 1024;

constexpr std::string_view kImport =
    R"(  (import "host_lib" "sha512_half" (func $sha512_half (param i32 i32 i32 i32) (result i32)))
)";

void
sha512HalfThroughVm(benchmark::State& state)
{
    // Zeroed guest memory is a perfectly good hash input: `sha512_half` validates nothing about
    // its bytes, so there is no data segment to seed.
    auto const body = std::string{"(call $sha512_half (i32.const 0) (i32.const "} +
        std::to_string(state.range(0)) + ") (i32.const 8192) (i32.const 32))";

    // The call count shrinks as the input grows: at 1 KiB a thousand calls would approach the
    // engine's per-run transfer budget and the tail of the loop would be measuring refusals.
    benchmarkThroughVm(
        state,
        kWasmName,
        kImport,
        "",
        body,
        [] { return benchHost(); },
        callsWithinTransferBudget(state.range(0) + 32));
    state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(sha512HalfThroughVm)
    ->RangeMultiplier(8)
    ->Range(8, kMaxBytes)
    ->UseManualTime()
    ->Iterations(kBenchIterations);

void
sha512HalfImpl(benchmark::State& state)
{
    auto const data = Bytes(static_cast<std::size_t>(state.range(0)), 0x42);
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [&data](auto& host) {
            return host.computeSha512HalfHash(Slice{data.data(), data.size()});
        });
    state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(sha512HalfImpl)
    ->RangeMultiplier(8)
    ->Range(8, kMaxBytes)
    ->UseManualTime()
    ->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
