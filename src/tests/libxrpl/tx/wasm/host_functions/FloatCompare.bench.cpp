#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "float_cmp";

// Declared 80 — cheaper than any arithmetic, and the only float call answering a scalar rather
// than 12 bytes. It still decodes two operands, which is what makes it a useful floor for what
// operand decoding alone costs.
void
floatCompareImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.floatCompare(benchFloatX(), benchFloatY()); });
}
BENCHMARK(floatCompareImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
