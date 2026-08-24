#include <expected>
#ifdef _DEBUG
// #define DEBUG_OUTPUT 1
#endif

#include <test/app/TestHostFunctions.h>
#include <test/app/wasm_fixtures/fixtures.h>
#include <test/jtx/Env.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/WasmCommon.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <boost/algorithm/hex.hpp>

#include <cstdint>
#include <limits>
#include <source_location>
#include <string>
#include <vector>

namespace xrpl::test {

std::vector<uint8_t>
hexToBytes(std::string const& hex)
{
    auto const ws = boost::algorithm::unhex(hex);
    return Bytes(ws.begin(), ws.end());
}

struct Wasm_test : public beast::unit_test::Suite
{
    void
    checkResult(
        std::expected<WasmResult<int32_t>, WasmTER> re,
        int32_t expectedResult,
        int64_t expectedCost,
        std::source_location const location = std::source_location::current())
    {
        auto const lineStr = " (" + std::to_string(location.line()) + ")";
        if (BEAST_EXPECTS(re.has_value(), transToken(re.error().ter) + lineStr))
        {
            BEAST_EXPECTS(re->result == expectedResult, std::to_string(re->result) + lineStr);
            BEAST_EXPECTS(re->cost == expectedCost, std::to_string(re->cost) + lineStr);
        }
    }

    void
    testBadWasm()
    {
        testcase("bad wasm test");

        using namespace test::jtx;

        Env const env{*this};
        HostFunctions hfs(env.journal);

        {
            auto wasm = hexToBytes("00000000");
            std::string const funcName("mock_escrow");

            auto re = runEscrowWasm(wasm, hfs, 15, funcName);
            BEAST_EXPECT(!re);
        }

        {
            auto wasm = hexToBytes("00112233445566778899AA");
            std::string const funcName("mock_escrow");

            auto const re = preflightEscrowWasm(wasm, env.journal, funcName);
            BEAST_EXPECT(!isTesSuccess(re));
        }

        {
            // FinishFunction wrong function name
            // pub fn bad() -> bool {
            //     unsafe { host_lib::getLedgerSqn() >= 5 }
            // }
            auto const badWasm = hexToBytes(
                "0061736d010000000105016000017f02190108686f73745f6c69620c6765"
                "744c656467657253716e00000302010005030100100611027f00418080c0"
                "000b7f00418080c0000b072b04066d656d6f727902000362616400010a5f"
                "5f646174615f656e6403000b5f5f686561705f6261736503010a09010700"
                "100041044a0b004d0970726f64756365727302086c616e67756167650104"
                "52757374000c70726f6365737365642d6279010572757374631d312e3835"
                "2e31202834656231363132353020323032352d30332d31352900490f7461"
                "726765745f6665617475726573042b0f6d757461626c652d676c6f62616c"
                "732b087369676e2d6578742b0f7265666572656e63652d74797065732b0a"
                "6d756c746976616c7565");

            auto const re = preflightEscrowWasm(badWasm, env.journal, escrowFunctionName);
            BEAST_EXPECT(!isTesSuccess(re));
        }
    }

    void
    testEscrowWasmDN()
    {
        testcase("escrow wasm devnet test");

        auto const allHFWasm = hexToBytes(kAllHostFunctionsWasmHex);

        using namespace test::jtx;
        Env env{*this};
        {
            TestHostFunctions hfs(env);
            auto re = runEscrowWasm(allHFWasm, hfs, 100'000, escrowFunctionName);
            checkResult(re, 1, 48'580);
        }

        {
            // max<int64_t>() gas
            TestHostFunctions hfs(env);
            auto re = runEscrowWasm(
                allHFWasm, hfs, std::numeric_limits<int64_t>::max(), escrowFunctionName);
            checkResult(re, 1, 48'580);
        }

        {  // fail because trying to access nonexistent field
            struct FieldNotFoundHostFunctions : public TestHostFunctions
            {
                explicit FieldNotFoundHostFunctions(Env& env) : TestHostFunctions(env)
                {
                }
                [[nodiscard]] std::expected<Bytes, HostFunctionError>
                getTxField(SField const& fname) const override
                {
                    return std::unexpected(HostFunctionError::FieldNotFound);
                }
            };

            FieldNotFoundHostFunctions hfs(env);
            auto re = runEscrowWasm(allHFWasm, hfs, 100'000, escrowFunctionName);
            checkResult(re, -201, 28'329);
        }

        {  // fail because trying to allocate more than MAX_PAGES memory
            struct OversizedFieldHostFunctions : public TestHostFunctions
            {
                explicit OversizedFieldHostFunctions(Env& env) : TestHostFunctions(env)
                {
                }
                [[nodiscard]] std::expected<Bytes, HostFunctionError>
                getTxField(SField const& fname) const override
                {
                    return Bytes((128 + 1) * 64 * 1024, 1);
                }
            };

            OversizedFieldHostFunctions hfs(env);
            auto re = runEscrowWasm(allHFWasm, hfs, 100'000, escrowFunctionName);
            checkResult(re, -201, 28'329);
        }
    }

    void
    testCodecovWasm()
    {
        testcase("Codecov wasm test");

        using namespace test::jtx;

        Env env{*this};

        auto const codecovWasm = hexToBytes(kCodecovTestsWasmHex);
        TestHostFunctions hfs(env);

        auto const allowance = 125'667;
        auto re = runEscrowWasm(codecovWasm, hfs, allowance, escrowFunctionName);

        checkResult(re, 1, allowance);
    }

    void
    testSwapBytes()
    {
        testcase("Wasm swap bytes");

        uint64_t const swapDataU64 = 0x123456789abcdeffull;
        uint64_t const reverseSwapDataU64 = 0xffdebc9a78563412ull;
        int64_t const swapDataI64 = 0x123456789abcdeffll;
        int64_t const reverseSwapDataI64 = 0xffdebc9a78563412ll;

        uint32_t const swapDataU32 = 0x12789aff;
        uint32_t const reverseSwapDataU32 = 0xff9a7812;
        int32_t const swapDataI32 = 0x12789aff;
        int32_t const reverseSwapDataI32 = 0xff9a7812;

        uint16_t const swapDataU16 = 0x12ff;
        uint16_t const reverseSwapDataU16 = 0xff12;
        int16_t const swapDataI16 = 0x12ff;
        int16_t const reverseSwapDataI16 = 0xff12;

        uint64_t b1 = swapDataU64;
        int64_t b2 = swapDataI64;
        b1 = adjustWasmEndianessHlp(b1);
        b2 = adjustWasmEndianessHlp(b2);
        BEAST_EXPECT(b1 == reverseSwapDataU64);
        BEAST_EXPECT(b2 == reverseSwapDataI64);
        b1 = adjustWasmEndianessHlp(b1);
        b2 = adjustWasmEndianessHlp(b2);
        BEAST_EXPECT(b1 == swapDataU64);
        BEAST_EXPECT(b2 == swapDataI64);

        uint32_t b3 = swapDataU32;
        int32_t b4 = swapDataI32;
        b3 = adjustWasmEndianessHlp(b3);
        b4 = adjustWasmEndianessHlp(b4);
        BEAST_EXPECT(b3 == reverseSwapDataU32);
        BEAST_EXPECT(b4 == reverseSwapDataI32);
        b3 = adjustWasmEndianessHlp(b3);
        b4 = adjustWasmEndianessHlp(b4);
        BEAST_EXPECT(b3 == swapDataU32);
        BEAST_EXPECT(b4 == swapDataI32);

        uint16_t b5 = swapDataU16;
        int16_t b6 = swapDataI16;
        b5 = adjustWasmEndianessHlp(b5);
        b6 = adjustWasmEndianessHlp(b6);
        BEAST_EXPECT(b5 == reverseSwapDataU16);
        BEAST_EXPECT(b6 == reverseSwapDataI16);
        b5 = adjustWasmEndianessHlp(b5);
        b6 = adjustWasmEndianessHlp(b6);
        BEAST_EXPECT(b5 == swapDataU16);
        BEAST_EXPECT(b6 == swapDataI16);
    }

    void
    run() override
    {
        testBadWasm();
        testEscrowWasmDN();
        testCodecovWasm();
        testSwapBytes();
    }
};

BEAST_DEFINE_TESTSUITE(Wasm, app, xrpl);

}  // namespace xrpl::test
