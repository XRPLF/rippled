#include <benchmark/benchmark.h>
#include <benchmarks/libxrpl/wasm/BenchFixtures.h>
#include <benchmarks/libxrpl/wasm/WasmBench.h>

#include <string_view>

namespace xrpl::test::bench {
namespace {

void
permissionedDomainKeyletImpl(benchmark::State& state)
{
    static constexpr auto kWasmName = std::string_view{"permissioned_domain_id"};

    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().host(); },
        [](auto& host) {
            return host.permissionedDomainKeylet(Fixtures::instance().alice().id(), Fixtures::kSeq);
        });
}
BENCHMARK(permissionedDomainKeyletImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
