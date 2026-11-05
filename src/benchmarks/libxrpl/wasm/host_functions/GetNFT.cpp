
#include <benchmark/benchmark.h>
#include <benchmarks/libxrpl/wasm/WasmBench.h>
#include <tx/wasm/fixtures/NftSetup.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
getNFTImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"nft_uri"};

    // A really minted token, so the lookup walks a real page rather than failing fast — a
    // not-found answer would measure the rejection instead of the work.
    static constexpr auto kUri = std::string_view{"ipfs://benchmark"};
    // Its own ledger rather than the shared `Fixtures`: minting mutates state, and the shared
    // one is deliberately read-only after construction.
    static auto nft = WasmLedger{};
    static auto const kOwner = nft.fund("benchNftOwner");
    static auto const kMinted = mintNft(nft, kOwner, kUri);

    benchmarkImpl(
        state,
        kWasmName,
        [] { return nft.makeHost(); },
        [](auto& host) { return host.getNFT(kOwner.id(), kMinted); });
}
BENCHMARK(getNFTImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
