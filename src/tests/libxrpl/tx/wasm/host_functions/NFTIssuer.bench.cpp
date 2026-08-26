#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
nftIssuerImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"nft_issuer"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.getNFTIssuer(benchNftId()); });
}
BENCHMARK(nftIssuerImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
