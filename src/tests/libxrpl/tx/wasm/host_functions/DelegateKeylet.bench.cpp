#include <benchmark/benchmark.h>
#include <tx/wasm/fixtures/BenchFixtures.h>
#include <tx/wasm/fixtures/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
delegateKeyletImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"delegate_id"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().host(); },
        [](auto& host) {
            return host.delegateKeylet(
                Fixtures::instance().alice().id(), Fixtures::instance().bob().id());
        });
}
BENCHMARK(delegateKeyletImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
