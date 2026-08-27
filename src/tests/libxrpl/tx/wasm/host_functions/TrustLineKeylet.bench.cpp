#include <xrpl/protocol/UintTypes.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
trustLineKeyletImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"trustline_id"};

    auto const currency = toCurrency("USD");

    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().host(); },
        [&currency](auto& host) {
            return host.trustLineKeylet(
                Fixtures::instance().alice().id(), Fixtures::instance().bob().id(), currency);
        });
}
BENCHMARK(trustLineKeyletImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
