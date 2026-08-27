#include <xrpl/basics/Slice.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <cstddef>
#include <cstdint>
#include <format>
#include <string_view>

namespace xrpl::test::bench {
namespace {

constexpr std::string_view kWasmName = "sha512_half";

constexpr std::int16_t kMaxBytes = 1024;

constexpr std::string_view kImport =
    R"(  (import "host_lib" "sha512_half" (func $sha512_half (param i32 i32 i32 i32) (result i32)))
)";

void
sha512HalfThroughVm(benchmark::State& state)
{
    // Zeroed guest memory is a perfectly good hash input: `sha512_half` validates nothing about
    // its bytes, so there is no data segment to seed.
    auto const body = std::format(
        "(call $sha512_half (i32.const 0) (i32.const {}) (i32.const 8192) (i32.const 32))",
        state.range(0));

    // The call count shrinks as the input grows: at 1 KiB a thousand calls would approach the
    // engine's per-run transfer budget and the tail of the loop would be measuring refusals.
    benchmarkThroughVm(
        state,
        kWasmName,
        kImport,
        "",
        body,
        [] { return Fixtures::instance().host(); },
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
        [] { return Fixtures::instance().host(); },
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
