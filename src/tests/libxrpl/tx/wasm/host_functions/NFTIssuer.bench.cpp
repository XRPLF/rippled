#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

// The guest import name, used to look up what `lib.rs` declares this call costs. Reading the
// declaration rather than copying the number keeps the two from drifting apart.
constexpr std::string_view kWasmName = "nft_issuer";

// Declared 70. An NFToken id encodes its issuer in its own 32 bytes, so this touches no ledger
// state: it is shifts and masks over a value the guest already handed over. Compare against
// `GetNFT`, declared 5000, which actually goes and finds the token.
void
nftIssuerImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) { return host.getNFTIssuer(benchNftId()); });
}
BENCHMARK(nftIssuerImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
