#include <xrpl/tx/wasm/WasmCommon.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/RealHostFixture.h>
#include <tx/wasm/WasmBench.h>

#include <cstdint>
#include <string_view>

namespace xrpl::test::bench {
namespace {

constexpr std::string_view kWasmName = "escrow_id";

constexpr std::string_view kImport =
    R"(  (import "host_lib" "escrow_id" (func $escrow_id (param i32 i32 i32 i32 i32 i32) (result i32)))
)";
constexpr std::string_view kBody =
    "(call $escrow_id (i32.const 0) (i32.const 20) (i32.const 32) (i32.const 4) "
    "(i32.const 64) (i32.const 32))";

void
escrowKeyletThroughVm(benchmark::State& state)
{
    static auto const kData = [] {
        auto seq = Bytes(4);
        for (auto i = 0U; i < 4; ++i)
        {
            seq[i] = static_cast<std::uint8_t>((Fixtures::kSeq >> (8 * i)) & 0xFF);
        }
        return dataSegment(0, RealHostFixture::toBytes(Fixtures::instance().alice().id())) +
            dataSegment(32, seq);
    }();

    benchmarkThroughVm(
        state, kWasmName, kImport, kData, kBody, [] { return Fixtures::instance().host(); });
}
BENCHMARK(escrowKeyletThroughVm)->UseManualTime()->Iterations(kBenchIterations);

void
escrowKeyletImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().host(); },
        [](auto& host) {
            return host.escrowKeylet(Fixtures::instance().alice().id(), Fixtures::kSeq);
        });
}
BENCHMARK(escrowKeyletImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
