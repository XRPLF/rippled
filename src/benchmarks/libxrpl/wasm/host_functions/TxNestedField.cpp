#include <xrpl/protocol/SField.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <benchmark/benchmark.h>
#include <benchmarks/libxrpl/wasm/BenchFixtures.h>
#include <benchmarks/libxrpl/wasm/WasmBench.h>

#include <cstdint>
#include <string_view>

namespace xrpl::test::bench {
namespace {

constexpr std::string_view kWasmName = "tx_inner";
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
            for (auto i = 0U; i < 4; ++i)
            {
                bytes.push_back(static_cast<std::uint8_t>((step >> (8 * i)) & 0xFF));
            }
        }
        return bytes;
    }());

    benchmarkThroughVm(
        state, kWasmName, kImport, kData, kBody, [] { return Fixtures::instance().host(); });
}
BENCHMARK(txNestedFieldThroughVm)->UseManualTime()->Iterations(kBenchIterations);

void
txNestedFieldImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().host(); },
        [](auto& host) { return host.getTxNestedField(Fixtures::memoLocator()); });
}
BENCHMARK(txNestedFieldImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
