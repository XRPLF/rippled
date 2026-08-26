#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/UintTypes.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
ammKeyletImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"amm_id"};

    auto const usd = Asset{Issue{toCurrency("USD"), benchAlice().id()}};
    auto const xrp = Asset{xrpIssue()};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [&usd, &xrp](auto& host) { return host.ammKeylet(usd, xrp); });
}
BENCHMARK(ammKeyletImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
