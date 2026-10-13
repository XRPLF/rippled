#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <benchmark/benchmark.h>
#include <benchmarks/libxrpl/wasm/BenchFixtures.h>
#include <benchmarks/libxrpl/wasm/WasmBench.h>

#include <cstddef>
#include <format>
#include <string_view>

namespace xrpl::test::bench {
namespace {

constexpr std::string_view kWasmName = "sha512_half";

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

    // A hash is 32 bytes back to the guest whatever the input length, and the transfer budget
    // counts only what the host writes — so the input sweep does not shrink the call count.
    benchmarkThroughVm(
        state,
        kWasmName,
        kImport,
        "",
        body,
        [] { return Fixtures::instance().host(); },
        callsWithinTransferBudget(32));
    state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(sha512HalfThroughVm)
    ->RangeMultiplier(8)
    ->Range(8, xrpl::kMaxWasmDataLength)
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
    ->Range(8, xrpl::kMaxWasmDataLength)
    ->UseManualTime()
    ->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
