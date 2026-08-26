#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "float_mult";

// Declared 300, 1.875x `float_add`'s 160. A multiplication really is more work than an
// addition on a mantissa/exponent pair, so this ratio is one of the table's more defensible
// claims — and one of the easier ones to confirm.
void
floatMultiplyImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.floatMultiply(benchFloatX(), benchFloatY(), kBenchMode); });
}
BENCHMARK(floatMultiplyImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
