#include <xrpl/basics/Slice.h>

#include <benchmark/benchmark.h>
#include <tx/wasm/BenchFixtures.h>
#include <tx/wasm/WasmBench.h>

#include <cstdint>
#include <format>
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
    auto const& m = Fixtures::instance().signedMessage();
    static auto const kData = dataSegment(kMessageOffset, m.message) +
        dataSegment(kSignatureOffset, m.signature) + dataSegment(kPubkeyOffset, m.publicKey);
    static auto const kBody = std::format(
        "(call $check_sig (i32.const {}) (i32.const {}) (i32.const {}) (i32.const {}) "
        "(i32.const {}) (i32.const {}))",
        kMessageOffset,
        m.message.size(),
        kSignatureOffset,
        m.signature.size(),
        kPubkeyOffset,
        m.publicKey.size());

    benchmarkThroughVm(
        state, kWasmName, kImport, kData, kBody, [] { return Fixtures::instance().host(); });
}
BENCHMARK(checkSignatureThroughVm)->UseManualTime()->Iterations(kBenchIterations);

void
checkSignatureImpl(benchmark::State& state)
{
    benchmarkImpl(
        state,
        kWasmName,
        [] { return Fixtures::instance().host(); },
        [](auto& host) {
            auto const& m = Fixtures::instance().signedMessage();
            return host.checkSignature(
                Slice{m.message.data(), m.message.size()},
                Slice{m.signature.data(), m.signature.size()},
                Slice{m.publicKey.data(), m.publicKey.size()});
        });
}
BENCHMARK(checkSignatureImpl)->UseManualTime()->Iterations(kBenchIterations);

}  // namespace
}  // namespace xrpl::test::bench
