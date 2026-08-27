#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
nftTransferFeeImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"nft_xfer_fee"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().host(); },
        [](auto& host) { return host.getNFTTransferFee(Fixtures::instance().nftId()); });
}
BENCHMARK(nftTransferFeeImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
