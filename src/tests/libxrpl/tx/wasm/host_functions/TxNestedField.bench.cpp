#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <cstdint>
#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "tx_inner";

// Declared 110 against a direct read's 70 — the table's claim that walking a locator costs about
// half a field read again.
//
// This file carries the locator `ThroughVm` case for the whole nested family. A locator is a
// variable-length path of little-endian i32 steps that the *guest* lays out and the host walks;
// every other input shape in the ABI is a fixed-size value, so this is the only place the crossing
// reads a length the guest chose. The other five nested getters are `Impl`-only.

constexpr std::string_view kImport =
    R"(  (import "host_lib" "tx_inner" (func $tx_inner (param i32 i32 i32 i32) (result i32)))
)";
constexpr std::string_view kBody =
    "(call $tx_inner (i32.const 0) (i32.const 12) (i32.const 64) (i32.const 64))";

void
txNestedFieldThroughVm(benchmark::State& state)
{
    // The locator bytes are seeded rather than stored by the guest, so the loop measures the host
    // call and not three `i32.store`s.
    static auto const kData = dataSegment(0, [] {
        auto bytes = Bytes{};
        for (auto const step : {sfMemos.getCode(), 0, sfMemoData.getCode()})
        {
            for (auto i = 0; i < 4; ++i)
            {
                bytes.push_back(static_cast<std::uint8_t>((step >> (8 * i)) & 0xFF));
            }
        }
        return bytes;
    }());

    benchmarkThroughVm(state, kWasmName, kImport, kData, kBody, [] { return benchHost(); });
}
BENCHMARK(txNestedFieldThroughVm)->UseManualTime()->Iterations(kBenchIterations);

void
txNestedFieldImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.getTxNestedField(benchMemoLocator()); });
}
BENCHMARK(txNestedFieldImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
