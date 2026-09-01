#include <benchmark/benchmark.h>
#include <tx/wasm/fixtures/BenchFixtures.h>
#include <tx/wasm/fixtures/FloatConstants.h>
#include <tx/wasm/fixtures/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

constexpr std::string_view kWasmName = "float_add";

constexpr std::string_view kImport =
    R"(  (import "host_lib" "float_add" (func $float_add (param i32 i32 i32 i32 i32 i32 i32) (result i32)))
)";

constexpr std::string_view kBody =
    "(call $float_add (i32.const 0) (i32.const 12) (i32.const 16) (i32.const 12) "
    "(i32.const 64) (i32.const 12) (i32.const 0))";

void
floatAddThroughVm(benchmark::State& state)
{
    static auto const kData =
        dataSegment(0, FloatConstants::kPi) + dataSegment(16, FloatConstants::kTwo);
    benchmarkThroughVm(
        state, kWasmName, kImport, kData, kBody, [] { return Fixtures::instance().host(); });
}
BENCHMARK(floatAddThroughVm)->UseManualTime()->Iterations(kBenchIterations);

void
floatAddImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().host(); },
        [](auto& host) {
            return host.floatAdd(Fixtures::floatX(), Fixtures::floatY(), Fixtures::kRoundingMode);
        });
}
BENCHMARK(floatAddImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
