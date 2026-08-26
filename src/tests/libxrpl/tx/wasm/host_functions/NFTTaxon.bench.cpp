#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "nft_taxon";

// Declared 60. Pure extraction from the id — no ledger access. See `NFTIssuer.bench.cpp`.
void
nftTaxonImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.getNFTTaxon(benchNftId()); });
}
BENCHMARK(nftTaxonImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
