// Not built. These suites drive a C++ wasm engine interface -- WasmVM over the wasm.h C API,
// HostFuncWrapper, WasmImportsHelper -- that this tree does not provide; the VM lives in the
// Rust crates. Kept as the coverage target for the port. The body is one comment block, and
// the fixtures it reads (wasm_fixtures/fixtures.cpp) are disabled the same way; re-enabling
// the suites means uncommenting both.

/*
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
#include <xrpl/tx/wasm/HostFuncWrapper.h>  // IWYU pragma: keep
#include <xrpl/tx/wasm/WasmCommon.h>
#include <xrpl/tx/wasm/WasmImportsHelper.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <boost/algorithm/hex.hpp>

#include <wasm.h>

#include <cstdint>
#include <limits>
#include <source_location>
#include <string>
#include <vector>

namespace xrpl::test {

bool
testGetDataIncrement();

using Add_proto = int32_t(int32_t, int32_t);
static wasm_trap_t*
add(HostFunctions&, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    int32_t const val1 = params->data[0].of.i32;
    int32_t const val2 = params->data[1].of.i32;
    // printf("Host function \"Add\": %d + %d\n", Val1, Val2);
    results->data[0] = WASM_I32_VAL(val1 + val2);
    return nullptr;
}

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
    testGetDataHelperFunctions()
    {
        testcase("getData helper functions");
        BEAST_EXPECT(testGetDataIncrement());
    }

    void
    testWasmLib()
    {
        testcase("wasm lib test");
        // clang-format off
        // The WASM module buffer. //
        Bytes const wasm = {// WASM header //
                          0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00,
                          // Type section //
                          0x01, 0x07, 0x01,
                          // function type {i32, i32} -> {i32} //
                          0x60, 0x02, 0x7F, 0x7F, 0x01, 0x7F,
                          // Import section //
                          0x02, 0x13, 0x01,
                          // module name: "extern" //
                          0x06, 0x65, 0x78, 0x74, 0x65, 0x72, 0x6E,
                          // extern name: "func-add" //
                          0x08, 0x66, 0x75, 0x6E, 0x63, 0x2D, 0x61, 0x64, 0x64,
                          // import desc: func 0 //
                          0x00, 0x00,
                          // Function section //
                          0x03, 0x02, 0x01, 0x00,
                          // Export section //
                          0x07, 0x0A, 0x01,
                          // export name: "addTwo" //
                          0x06, 0x61, 0x64, 0x64, 0x54, 0x77, 0x6F,
                          // export desc: func 0 //
                          0x00, 0x01,
                          // Code section //
                          0x0A, 0x0A, 0x01,
                          // code body //
                          0x08, 0x00, 0x20, 0x00, 0x20, 0x01, 0x10, 0x00, 0x0B};
        // clang-format on
        auto& vm = WasmEngine::instance();

        HostFunctions hfs;
        ImportVec imports;
        WasmImpFunc<Add_proto>(imports, "func-add", add, hfs);

        auto re = vm.run(wasm, hfs, 10'000'000, "addTwo", wasmParams(1234, 5678), imports);

        // if (res) printf("invokeAdd get the result: %d\n", res.value());

        checkResult(re, 6'912, 59);
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

            auto re = runEscrowWasm(wasm, hfs, 15, funcName, {});
            BEAST_EXPECT(!re);
        }

        {
            auto wasm = hexToBytes("00112233445566778899AA");
            std::string const funcName("mock_escrow");

            auto const re = preflightEscrowWasm(wasm, hfs, funcName);
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

            auto const re = preflightEscrowWasm(badWasm, hfs, escrowFunctionName);
            BEAST_EXPECT(!isTesSuccess(re));
        }
    }

    void
    testWasmLedgerSqn()
    {
        testcase("Wasm get ledger sequence");

        auto ledgerSqnWasm = hexToBytes(kLedgerSqnWasmHex);

        using namespace test::jtx;

        Env env{*this};
        TestLedgerDataProvider hfs(env);
        ImportVec imports;
        WASM_IMPORT_FUNC2(imports, getLedgerSqn, "ldgr_index", hfs, 33);
        auto& engine = WasmEngine::instance();

        auto re =
            engine.run(ledgerSqnWasm, hfs, 1'000'000, escrowFunctionName, {}, imports, env.journal);

        checkResult(re, 0, 440);

        env.close();
        env.close();

        // empty module, throwing exception
        re = engine.run({}, hfs, 1'000'000, escrowFunctionName, {}, imports, env.journal);
        BEAST_EXPECT(!re);
        env.close();
    }

    void
    testHFCost()
    {
        testcase("wasm test host functions cost");

        using namespace test::jtx;

        Env env(*this);
        {
            auto const allHostFuncWasm = hexToBytes(kAllHostFunctionsWasmHex);

            auto& engine = WasmEngine::instance();

            TestHostFunctions hfs(env);
            auto imp = createWasmImport(hfs);
            for (auto& i : imp)
                i.second.second.gas = 0;

            auto re = engine.run(
                allHostFuncWasm, hfs, 1'000'000, escrowFunctionName, {}, imp, env.journal);

            checkResult(re, 1, 30'760);

            env.close();
        }

        env.close();
        env.close();
        env.close();
        env.close();
        env.close();

        {
            auto const allHostFuncWasm = hexToBytes(kAllHostFunctionsWasmHex);

            auto& engine = WasmEngine::instance();

            TestHostFunctions hfs(env);
            auto const imp = createWasmImport(hfs);

            auto re = engine.run(
                allHostFuncWasm, hfs, 1'000'000, escrowFunctionName, {}, imp, env.journal);

            checkResult(re, 1, 48'580);

            env.close();
        }

        // not enough gas
        {
            auto const allHostFuncWasm = hexToBytes(kAllHostFunctionsWasmHex);

            auto& engine = WasmEngine::instance();

            TestHostFunctions hfs(env);
            auto const imp = createWasmImport(hfs);

            auto re =
                engine.run(allHostFuncWasm, hfs, 200, escrowFunctionName, {}, imp, env.journal);

            if (BEAST_EXPECT(!re))
            {
                // Running out of gas now terminates with tecOUT_OF_GAS (was
                // previously collapsed into tecFAILED_PROCESSING).
                BEAST_EXPECTS(
                    re.error().ter == tecOUT_OF_GAS, std::to_string(TERtoInt(re.error().ter)));
            }

            env.close();
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
            auto re = runEscrowWasm(allHFWasm, hfs, 100'000, escrowFunctionName, {});
            checkResult(re, 1, 48'580);
        }

        {
            // Invalid gas limit (0) should be rejected (boundary condition)
            TestHostFunctions hfs(env);
            auto re = runEscrowWasm(allHFWasm, hfs, -1, escrowFunctionName, {});
            BEAST_EXPECT(!re.has_value());
            BEAST_EXPECT(re.error().ter == temBAD_AMOUNT);
        }

        {
            // Invalid gas limit (-1) should be rejected
            TestHostFunctions hfs(env);
            auto re = runEscrowWasm(allHFWasm, hfs, 0, escrowFunctionName, {});
            BEAST_EXPECT(!re.has_value());
            BEAST_EXPECT(re.error().ter == temBAD_AMOUNT);
        }

        {
            // max<int64_t>() gas
            TestHostFunctions hfs(env);
            auto re = runEscrowWasm(
                allHFWasm, hfs, std::numeric_limits<int64_t>::max(), escrowFunctionName, {});
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
            auto re = runEscrowWasm(allHFWasm, hfs, 100'000, escrowFunctionName, {});
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
            auto re = runEscrowWasm(allHFWasm, hfs, 100'000, escrowFunctionName, {});
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
        auto re = runEscrowWasm(codecovWasm, hfs, allowance, escrowFunctionName, {});

        checkResult(re, 1, allowance);
    }

    void
    testBadAlign()
    {
        testcase("Wasm Bad Align");

        // bad_align.c
        auto const badAlignWasm = hexToBytes(kBadAlignWasmHex);

        using namespace test::jtx;

        Env env{*this};
        TestHostFunctions hfs(env);
        auto imports = createWasmImport(hfs);

        {  // Calls float_from_uint with bad alignment.
           // Can be checked through codecov
            auto& engine = WasmEngine::instance();

            auto re = engine.run(badAlignWasm, hfs, 1'000'000, "test", {}, imports, env.journal);
            if (BEAST_EXPECTS(re, transToken(re.error().ter)))
            {
                BEAST_EXPECTS(re->result == 0x47308594, std::to_string(re->result));
            }
        }

        env.close();
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
        using namespace test::jtx;

        testGetDataHelperFunctions();
        testWasmLib();
        testBadWasm();
        testWasmLedgerSqn();

        testHFCost();
        testEscrowWasmDN();

        testCodecovWasm();

        testBadAlign();
        testSwapBytes();
    }
};

BEAST_DEFINE_TESTSUITE(Wasm, app, xrpl);

}  // namespace xrpl::test
*/
