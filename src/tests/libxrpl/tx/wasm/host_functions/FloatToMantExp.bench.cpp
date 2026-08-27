#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/FloatFixture.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

constexpr std::string_view kWasmName = "float_to_mant_exp";

constexpr std::string_view kImport =
    R"(  (import "host_lib" "float_to_mant_exp" (func $split (param i32 i32 i32 i32 i32 i32) (result i32)))
)";
constexpr std::string_view kBody =
    "(call $split (i32.const 0) (i32.const 12) (i32.const 64) (i32.const 8) "
    "(i32.const 128) (i32.const 4))";

void
floatToMantExpThroughVm(benchmark::State& state)
{
    static auto const kData = dataSegment(0, FloatTest::kPi);
    benchmarkThroughVm(
        state, kWasmName, kImport, kData, kBody, [] { return Fixtures::instance().host(); });
}
BENCHMARK(floatToMantExpThroughVm)->UseManualTime()->Iterations(kBenchIterations);

void
floatToMantExpImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().host(); },
        [](auto& host) { return host.floatToMantExp(Fixtures::floatX()); });
}
BENCHMARK(floatToMantExpImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
