#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
nftTaxonImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"nft_taxon"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.getNFTTaxon(benchNftId()); });
}
BENCHMARK(nftTaxonImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
