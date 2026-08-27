#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
paychannelKeyletImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"paychan_id"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().host(); },
        [](auto& host) {
            return host.paychannelKeylet(
                Fixtures::instance().alice().id(), Fixtures::instance().bob().id(), Fixtures::kSeq);
        });
}
BENCHMARK(paychannelKeyletImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
