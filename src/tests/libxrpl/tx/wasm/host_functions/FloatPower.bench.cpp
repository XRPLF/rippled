#include <benchmark/benchmark.h>
#include <tx/wasm/fixtures/BenchFixtures.h>
#include <tx/wasm/fixtures/FloatConstants.h>
#include <tx/wasm/fixtures/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

constexpr std::string_view kWasmName = "float_pow";

constexpr std::string_view kImport =
    R"(  (import "host_lib" "float_pow" (func $float_pow (param i32 i32 i32 i32 i32 i32) (result i32)))
)";

constexpr std::string_view kBody =
    "(call $float_pow (i32.const 0) (i32.const 12) (i32.const 7) "
    "(i32.const 64) (i32.const 12) (i32.const 0))";

void
floatPowerThroughVm(benchmark::State& state)
{
    static auto const kData = dataSegment(0, FloatConstants::kPi);
    benchmarkThroughVm(
        state, kWasmName, kImport, kData, kBody, [] { return Fixtures::instance().host(); });
}
BENCHMARK(floatPowerThroughVm)->UseManualTime()->Iterations(kBenchIterations);

void
floatPowerImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().host(); },
        [](auto& host) { return host.floatPower(Fixtures::floatX(), 7, Fixtures::kRoundingMode); });
}
BENCHMARK(floatPowerImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
