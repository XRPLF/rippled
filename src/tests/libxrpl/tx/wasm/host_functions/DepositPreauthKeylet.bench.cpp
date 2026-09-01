#include <benchmark/benchmark.h>
#include <tx/wasm/fixtures/BenchFixtures.h>
#include <tx/wasm/fixtures/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
depositPreauthKeyletImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"deposit_preauth_id"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().host(); },
        [](auto& host) {
            return host.depositPreauthKeylet(
                Fixtures::instance().alice().id(), Fixtures::instance().bob().id());
        });
}
BENCHMARK(depositPreauthKeyletImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
