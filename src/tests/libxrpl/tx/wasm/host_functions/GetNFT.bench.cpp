
#include <benchmark/benchmark.h>
#include <tx/wasm/NFTFixture.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "nft_uri";

// Declared 5000 against the five id-extractor getters' 60-70 — a 14x ratio that is at least the
// right sign, since this is the only NFT call that touches the ledger. It has to find the token's
// `NFTokenPage`, walk it, and copy out a variable-length URI, where its siblings just mask bits
// out of an id the guest already supplied. Whether 14x is the right *size* is what the gap between
// this case and `NFTIssuer.bench.cpp` answers.

void
getNFTImpl(benchmark::State& state)
{
    // A really minted token, so the lookup walks a real page rather than failing fast — a
    // not-found answer would measure the rejection instead of the work.
    static constexpr auto kUri = std::string_view{"ipfs://benchmark"};
    static Bench<NFTTest> nft;
    static auto const kOwner = nft.fund("benchNftOwner");
    static auto const kMinted = nft.mintNFT(kOwner, kUri);

    benchmarkImpl(
        state,
        kWasmName,
        [] { return nft.makeHost(); },
        [](auto& host) { return host.getNFT(kOwner.id(), kMinted); });
}
BENCHMARK(getNFTImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
