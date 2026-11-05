#include <benchmark/benchmark.h>
#include <benchmarks/libxrpl/wasm/BenchFixtures.h>
#include <benchmarks/libxrpl/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
floatDivideImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"float_div"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().host(); },
        [](auto& host) {
            return host.floatDivide(
                Fixtures::floatX(), Fixtures::floatY(), Fixtures::kRoundingMode);
        });
}
BENCHMARK(floatDivideImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
