#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/Protocol.h>
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
constexpr std::string_view kWasmName = "set_data";

// Declared 1000, and the suite's purest measurement of what *moving bytes* costs.
//
// The impl does almost nothing but copy the guest's region into host-owned storage, so unlike
// `sha512_half` there is no computation competing with the transfer. Swept over the input length
// up to `kMaxWasmDataLength`, the `ThroughVm` case is close to a direct readout of the per-byte
// term in the crossing — the number that, added to the fixed floor in `Crossing.bench.cpp`, should
// predict every other function's `ThroughVm` minus `Impl` gap.
//
// It is also a flat price over a range spanning two orders of magnitude, the same
// flat-price-for-linear-work question `Sha512Half.bench.cpp` asks.

constexpr std::string_view kImport =
    R"(  (import "host_lib" "set_data" (func $set_data (param i32 i32) (result i32)))
)";

void
updateDataThroughVm(benchmark::State& state)
{
    auto const body = std::string{"(call $set_data (i32.const 0) (i32.const "} +
        std::to_string(state.range(0)) + "))";

    benchmarkThroughVm(
        state,
        kWasmName,
        kImport,
        "",
        body,
        [] { return benchHost(); },
        callsWithinTransferBudget(state.range(0)));
    state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(updateDataThroughVm)
    ->RangeMultiplier(4)
    ->Range(8, kMaxWasmDataLength)
    ->UseManualTime()
    ->Iterations(kBenchIterations);

void
updateDataImpl(benchmark::State& state)
{
    auto const data = Bytes(static_cast<std::size_t>(state.range(0)), 0x42);
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [&data](auto& host) { return host.updateData(Slice{data.data(), data.size()}); });
    state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(updateDataImpl)
    ->RangeMultiplier(4)
    ->Range(8, kMaxWasmDataLength)
    ->UseManualTime()
    ->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
