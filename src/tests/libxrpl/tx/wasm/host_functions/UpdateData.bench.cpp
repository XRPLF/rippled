#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/fixtures/BenchFixtures.h>
#include <tx/wasm/fixtures/WasmBench.h>

#include <cstddef>
#include <format>
#include <string_view>

namespace xrpl::test::bench {
namespace {

constexpr std::string_view kWasmName = "set_data";
constexpr std::string_view kImport =
    R"(  (import "host_lib" "set_data" (func $set_data (param i32 i32) (result i32)))
)";

void
updateDataThroughVm(benchmark::State& state)
{
    auto const body = std::format("(call $set_data (i32.const 0) (i32.const {}))", state.range(0));

    benchmarkThroughVm(
        state,
        kWasmName,
        kImport,
        "",
        body,
        [] { return Fixtures::instance().host(); },
        // `set_data` answers a scalar and writes nothing into guest memory, so the
        // transfer budget does not constrain it however large the input gets.
        callsWithinTransferBudget(0));
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
        [] { return Fixtures::instance().host(); },
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
