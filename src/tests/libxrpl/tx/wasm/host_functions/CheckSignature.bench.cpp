#include <xrpl/basics/Slice.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace xrpl::test::bench {
namespace {

constexpr std::string_view kWasmName = "check_sig";

constexpr std::string_view kImport =
    R"(  (import "host_lib" "check_sig" (func $check_sig (param i32 i32 i32 i32 i32 i32) (result i32)))
)";

constexpr std::int32_t kMessageOffset = 0;
constexpr std::int32_t kSignatureOffset = 256;
constexpr std::int32_t kPubkeyOffset = 512;

void
checkSignatureThroughVm(benchmark::State& state)
{
    auto const& m = benchSignedMessage();
    static auto const kData = dataSegment(kMessageOffset, m.message) +
        dataSegment(kSignatureOffset, m.signature) + dataSegment(kPubkeyOffset, m.publicKey);
    static auto const kBody = std::string{"(call $check_sig (i32.const "} +
        std::to_string(kMessageOffset) + ") (i32.const " + std::to_string(m.message.size()) +
        ") (i32.const " + std::to_string(kSignatureOffset) + ") (i32.const " +
        std::to_string(m.signature.size()) + ") (i32.const " + std::to_string(kPubkeyOffset) +
        ") (i32.const " + std::to_string(m.publicKey.size()) + "))";

    benchmarkThroughVm(state, kWasmName, kImport, kData, kBody, [] { return benchHost(); });
}
BENCHMARK(checkSignatureThroughVm)->UseManualTime()->Iterations(kBenchIterations);

void
checkSignatureImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return benchHost(); },
        [](auto& host) {
            auto const& m = benchSignedMessage();
            return host.checkSignature(
                Slice{m.message.data(), m.message.size()},
                Slice{m.signature.data(), m.signature.size()},
                Slice{m.publicKey.data(), m.publicKey.size()});
        });
}
BENCHMARK(checkSignatureImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
