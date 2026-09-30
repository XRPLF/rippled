
#include <xrpl/protocol/Indexes.h>

#include <benchmark/benchmark.h>
#include <benchmarks/libxrpl/wasm/BenchFixtures.h>
#include <benchmarks/libxrpl/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
mptokenKeyletImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"mptoken_id"};

    auto const mptid = makeMptID(1, Fixtures::instance().alice().id());

    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().host(); },
        [&mptid](auto& host) {
            return host.mptokenKeylet(mptid, Fixtures::instance().bob().id());
        });
}
BENCHMARK(mptokenKeyletImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
