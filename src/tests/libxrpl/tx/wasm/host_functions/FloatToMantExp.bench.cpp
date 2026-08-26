#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/FloatFixture.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "float_to_mant_exp";

// Declared 130. The one call in the whole ABI that writes *two* output regions, so the one place
// the crossing does two bounds checks and two writes — which is why it carries a `ThroughVm` case
// despite being unremarkable arithmetic. Its gap over `floatToMantExpImpl` is the only measurement
// of what a second output region costs, and nothing else in the suite can supply it.

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
    benchmarkThroughVm(state, kWasmName, kImport, kData, kBody, [] { return benchHost(); });
}
BENCHMARK(floatToMantExpThroughVm)->UseManualTime()->Iterations(kBenchIterations);

void
floatToMantExpImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.floatToMantExp(benchFloatX()); });
}
BENCHMARK(floatToMantExpImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
