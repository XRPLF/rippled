#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/UintTypes.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "amm_id";

// Declared 450 — a hundred above the keylet family's flat 350, and the only keylet whose input
// *length* selects between interpretations: 20 bytes is XRP, 24 an MPT, 40 an issue. That
// dispatch is the work the surcharge is paying for, so this case is whether it costs 100.
void
ammKeyletImpl(benchmark::State& state)
{
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
