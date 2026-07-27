#include <test/app/wasm_fixtures/fixtures.h>
#include <test/jtx/Env.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/tx/wasm/HostFuncWrapper.h>  // IWYU pragma: keep
#include <xrpl/tx/wasm/ParamsHelper.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <boost/algorithm/hex.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#ifdef _DEBUG
// #define DEBUG_OUTPUT 1
#endif

#include <test/app/TestHostFunctions.h>

namespace xrpl::test {

namespace {

std::vector<uint8_t>
hexToBytes(std::string const& hex)
{
    auto const ws = boost::algorithm::unhex(hex);
    return Bytes(ws.begin(), ws.end());
}
}  // namespace

struct WasmPerf_test : public beast::unit_test::Suite
{
    template <typename Functor>
    void
    perf(TestHostFunctions& hfs, size_t runs, Functor&& f)
    {
        for (auto i = size_t{}; i < runs; ++i)
        {
            auto result = f();
            BEAST_EXPECT(result);
        }
        BEAST_EXPECT(hfs.execTimeEvent->count == runs);
    }

    void
    perfEscrowFinish()
    {
        testcase("perf escrow finish");
        using namespace test::jtx;

        static constexpr auto kRuns = 1000;

        auto const wasm = hexToBytes(kAllHostFunctionsWasmHex);

        Env env{*this};
        auto hfns = TestHostFunctions{env, 0};
        hfns.execTimeEvent->printOnDestruction = true;

        perf(hfns, kRuns, [&] {
            return runEscrowWasm(wasm, hfns, 1'000'000, escrowFunctionName, {}).has_value();
        });
    }

    void
    perfEscrowCreate()
    {
        testcase("perf escrow create");
        using namespace test::jtx;

        static constexpr auto kRuns = 1000;

        auto const wasm = hexToBytes(kAllHostFunctionsWasmHex);

        Env env{*this};
        auto hfns = TestHostFunctions{env, 0};
        hfns.execTimeEvent->printOnDestruction = true;

        perf(hfns, kRuns, [&] { return !preflightEscrowWasm(wasm, hfns, escrowFunctionName); });
    }

    void
    run() override
    {
        using namespace test::jtx;

        perfEscrowFinish();
        perfEscrowCreate();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(WasmPerf, app, xrpl);

}  // namespace xrpl::test
