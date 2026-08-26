#include <xrpl/tx/wasm/WasmCommon.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/RealHostFixture.h>
#include <tx/wasm/WasmBench.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "escrow_id";

// Declared 350, and the one keylet carrying a `ThroughVm` pair on behalf of all nineteen.
//
// It is the right representative because it is the shape with the extra wrinkle: its sequence
// number arrives as a four-byte little-endian *region*, not a scalar, so the crossing decodes two
// inputs rather than one (`read_u32_arg` in crates/xrpl-wasm-vm/src/register.rs). The gap between
// this pair and the floor in `Crossing.bench.cpp` therefore prices region decoding as well as the
// hash. The other eighteen are `Impl`-only — crossing cost follows a call's shape, not which
// keylet it computes.

constexpr std::string_view kImport =
    R"(  (import "host_lib" "escrow_id" (func $escrow_id (param i32 i32 i32 i32 i32 i32) (result i32)))
)";
constexpr std::string_view kBody =
    "(call $escrow_id (i32.const 0) (i32.const 20) (i32.const 32) (i32.const 4) "
    "(i32.const 64) (i32.const 32))";

void
escrowKeyletThroughVm(benchmark::State& state)
{
    // Account at 0, sequence at 32, answer at 64. An all-zero account would be `InvalidAccount`
    // and the benchmark would measure the rejection instead of the keylet.
    static auto const kData = [] {
        auto seq = Bytes(4);
        for (auto i = 0; i < 4; ++i)
            seq[i] = static_cast<std::uint8_t>((kBenchSeq >> (8 * i)) & 0xFF);
        return dataSegment(0, RealHostFixture::toBytes(benchAlice().id())) + dataSegment(32, seq);
    }();

    benchmarkThroughVm(state, kWasmName, kImport, kData, kBody, [] { return benchHost(); });
}
BENCHMARK(escrowKeyletThroughVm)->UseManualTime()->Iterations(kBenchIterations);

void
escrowKeyletImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.escrowKeylet(benchAlice().id(), kBenchSeq); });
}
BENCHMARK(escrowKeyletImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
