//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifdef _DEBUG
// #define DEBUG_OUTPUT 1
#endif

#include <test/app/TestHostFunctions.h>

#include <xrpld/app/wasm/HostFuncWrapper.h>
#include <xrpld/app/wasm/WamrVM.h>

namespace ripple {
namespace test {

using Add_proto = int32_t(int32_t, int32_t);
static wasm_trap_t*
Add(void* env, wasm_val_vec_t const* params, wasm_val_vec_t* results)
{
    int32_t Val1 = params->data[0].of.i32;
    int32_t Val2 = params->data[1].of.i32;
    // printf("Host function \"Add\": %d + %d\n", Val1, Val2);
    results->data[0] = WASM_I32_VAL(Val1 + Val2);
    return nullptr;
}

struct Wasm_test : public beast::unit_test::suite
{
    void
    testWasmLib()
    {
        testcase("wasmtime lib test");
        // clang-format off
        /* The WASM module buffer. */
        Bytes const wasm = {/* WASM header */
                          0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00,
                          /* Type section */
                          0x01, 0x07, 0x01,
                          /* function type {i32, i32} -> {i32} */
                          0x60, 0x02, 0x7F, 0x7F, 0x01, 0x7F,
                          /* Import section */
                          0x02, 0x13, 0x01,
                          /* module name: "extern" */
                          0x06, 0x65, 0x78, 0x74, 0x65, 0x72, 0x6E,
                          /* extern name: "func-add" */
                          0x08, 0x66, 0x75, 0x6E, 0x63, 0x2D, 0x61, 0x64, 0x64,
                          /* import desc: func 0 */
                          0x00, 0x00,
                          /* Function section */
                          0x03, 0x02, 0x01, 0x00,
                          /* Export section */
                          0x07, 0x0A, 0x01,
                          /* export name: "addTwo" */
                          0x06, 0x61, 0x64, 0x64, 0x54, 0x77, 0x6F,
                          /* export desc: func 0 */
                          0x00, 0x01,
                          /* Code section */
                          0x0A, 0x0A, 0x01,
                          /* code body */
                          0x08, 0x00, 0x20, 0x00, 0x20, 0x01, 0x10, 0x00, 0x0B};
        // clang-format on
        auto& vm = WasmEngine::instance();

        std::vector<WasmImportFunc> imports;
        WasmImpFunc<Add_proto>(
            imports, "func-add", reinterpret_cast<void*>(&Add));

        auto re = vm.run(wasm, "addTwo", wasmParams(1234, 5678), imports);

        // if (res) printf("invokeAdd get the result: %d\n", res.value());

        if (BEAST_EXPECT(re.has_value()))
        {
            BEAST_EXPECTS(re->result == 6'912, std::to_string(re->result));
            BEAST_EXPECTS(re->cost == 2, std::to_string(re->cost));
        }
    }

    void
    testBadWasm()
    {
        testcase("bad wasm test");

        using namespace test::jtx;

        Env env{*this};
        HostFunctions hfs;

        {
            auto wasmHex = "00000000";
            auto wasmStr = boost::algorithm::unhex(std::string(wasmHex));
            std::vector<uint8_t> wasm(wasmStr.begin(), wasmStr.end());
            std::string funcName("mock_escrow");

            auto re = runEscrowWasm(wasm, funcName, {}, &hfs, 15, env.journal);
            BEAST_EXPECT(!re);
        }

        {
            auto wasmHex = "00112233445566778899AA";
            auto wasmStr = boost::algorithm::unhex(std::string(wasmHex));
            std::vector<uint8_t> wasm(wasmStr.begin(), wasmStr.end());
            std::string funcName("mock_escrow");

            auto const re =
                preflightEscrowWasm(wasm, funcName, {}, &hfs, env.journal);
            BEAST_EXPECT(!isTesSuccess(re));
        }

        {
            // FinishFunction wrong function name
            // pub fn bad() -> bool {
            //     unsafe { host_lib::getLedgerSqn() >= 5 }
            // }
            auto const badWasmHex =
                "0061736d010000000105016000017f02190108686f73745f6c69620c6765"
                "744c656467657253716e00000302010005030100100611027f00418080c0"
                "000b7f00418080c0000b072b04066d656d6f727902000362616400010a5f"
                "5f646174615f656e6403000b5f5f686561705f6261736503010a09010700"
                "100041044a0b004d0970726f64756365727302086c616e67756167650104"
                "52757374000c70726f6365737365642d6279010572757374631d312e3835"
                "2e31202834656231363132353020323032352d30332d31352900490f7461"
                "726765745f6665617475726573042b0f6d757461626c652d676c6f62616c"
                "732b087369676e2d6578742b0f7265666572656e63652d74797065732b0a"
                "6d756c746976616c7565";
            auto wasmStr = boost::algorithm::unhex(std::string(badWasmHex));
            std::vector<uint8_t> wasm(wasmStr.begin(), wasmStr.end());

            auto const re = preflightEscrowWasm(
                wasm, ESCROW_FUNCTION_NAME, {}, &hfs, env.journal);
            BEAST_EXPECT(!isTesSuccess(re));
        }
    }

    void
    testWasmLedgerSqn()
    {
        testcase("Wasm get ledger sequence");

        auto wasmStr = boost::algorithm::unhex(ledgerSqnWasmHex);
        Bytes wasm(wasmStr.begin(), wasmStr.end());

        using namespace test::jtx;

        Env env{*this};
        TestLedgerDataProvider hf(&env);

        std::vector<WasmImportFunc> imports;
        WASM_IMPORT_FUNC2(imports, getLedgerSqn, "get_ledger_sqn", &hf, 33);
        auto& engine = WasmEngine::instance();

        auto re = engine.run(
            wasm,
            ESCROW_FUNCTION_NAME,
            {},
            imports,
            &hf,
            1'000'000,
            env.journal);

        // code takes 11 gas + 1 getLedgerSqn call
        if (BEAST_EXPECT(re.has_value()))
        {
            BEAST_EXPECTS(re->result == 0, std::to_string(re->result));
            BEAST_EXPECTS(re->cost == 39, std::to_string(re->cost));
        }

        env.close();
        env.close();

        // empty module => run the same instance
        re = engine.run(
            {}, ESCROW_FUNCTION_NAME, {}, imports, &hf, 1'000'000, env.journal);

        // code takes 22 gas + 2 getLedgerSqn calls
        if (BEAST_EXPECT(re.has_value()))
        {
            BEAST_EXPECTS(re->result == 5, std::to_string(re->result));
            BEAST_EXPECTS(re->cost == 78, std::to_string(re->cost));
        }
    }

    void
    testWasmFib()
    {
        testcase("Wasm fibo");

        auto const ws = boost::algorithm::unhex(fibWasmHex);
        Bytes const wasm(ws.begin(), ws.end());
        auto& engine = WasmEngine::instance();

        auto const re = engine.run(wasm, "fib", wasmParams(10));

        if (BEAST_EXPECT(re.has_value()))
        {
            BEAST_EXPECTS(re->result == 55, std::to_string(re->result));
            BEAST_EXPECTS(re->cost == 755, std::to_string(re->cost));
        }
    }

    void
    testWasmSha()
    {
        testcase("Wasm sha");

        auto const ws = boost::algorithm::unhex(sha512PureWasmHex);
        Bytes const wasm(ws.begin(), ws.end());
        auto& engine = WasmEngine::instance();

        auto const re =
            engine.run(wasm, "sha512_process", wasmParams(sha512PureWasmHex));

        if (BEAST_EXPECT(re.has_value()))
        {
            BEAST_EXPECTS(re->result == 34'432, std::to_string(re->result));
            BEAST_EXPECTS(re->cost == 157'452, std::to_string(re->cost));
        }
    }

    void
    testWasmB58()
    {
        testcase("Wasm base58");
        auto const ws = boost::algorithm::unhex(b58WasmHex);
        Bytes const wasm(ws.begin(), ws.end());
        auto& engine = WasmEngine::instance();

        Bytes outb;
        outb.resize(1024);

        auto const minsz = std::min(
            static_cast<std::uint32_t>(512),
            static_cast<std::uint32_t>(b58WasmHex.size()));
        auto const s = std::string_view(b58WasmHex.c_str(), minsz);

        auto const re = engine.run(wasm, "b58enco", wasmParams(outb, s));

        if (BEAST_EXPECT(re.has_value()))
        {
            BEAST_EXPECTS(re->result == 700, std::to_string(re->result));
            BEAST_EXPECTS(re->cost == 3'066'129, std::to_string(re->cost));
        }
    }

    void
    testWasmSP1Verifier()
    {
        testcase("Wasm sp1 zkproof verifier");
        auto const ws = boost::algorithm::unhex(sp1WasmHex);
        Bytes const wasm(ws.begin(), ws.end());
        auto& engine = WasmEngine::instance();

        auto const re = engine.run(wasm, "sp1_groth16_verifier");

        if (BEAST_EXPECT(re.has_value()))
        {
            BEAST_EXPECTS(re->result == 1, std::to_string(re->result));
            BEAST_EXPECTS(
                re->cost == 4'191'711'969ll, std::to_string(re->cost));
        }
    }

    void
    testWasmBG16Verifier()
    {
        testcase("Wasm BG16 zkproof verifier");
        auto const ws = boost::algorithm::unhex(zkProofWasmHex);
        Bytes const wasm(ws.begin(), ws.end());
        auto& engine = WasmEngine::instance();

        auto const re = engine.run(wasm, "bellman_groth16_test");

        if (BEAST_EXPECT(re.has_value()))
        {
            BEAST_EXPECTS(re->result == 1, std::to_string(re->result));
            BEAST_EXPECTS(re->cost == 332'205'984, std::to_string(re->cost));
        }
    }

    void
    testDisabledFloat()
    {
        testcase("disabled float");

        using namespace test::jtx;
        Env env{*this};

        auto const wasmStr = boost::algorithm::unhex(disabledFloatHex);
        Bytes wasm(wasmStr.begin(), wasmStr.end());
        std::string const funcName("finish");
        TestHostFunctions hfs(env);

        {
            // f32 set constant, opcode disabled exception
            auto const re =
                runEscrowWasm(wasm, funcName, {}, &hfs, 1'000'000, env.journal);
            if (BEAST_EXPECT(!re.has_value()))
            {
                BEAST_EXPECT(re.error() == tecFAILED_PROCESSING);
            }
        }

        {
            // f32 add, can't create module exception
            wasm[0x117] = 0x92;
            auto const re =
                runEscrowWasm(wasm, funcName, {}, &hfs, 1'000'000, env.journal);
            if (BEAST_EXPECT(!re.has_value()))
            {
                BEAST_EXPECT(re.error() == tecFAILED_PROCESSING);
            }
        }
    }
    void
    run() override
    {
        using namespace test::jtx;

        testWasmLib();
        testBadWasm();
        testWasmLedgerSqn();

        testWasmFib();
        testWasmSha();
        testWasmB58();

        // running too long
        // testWasmSP1Verifier();
        testWasmBG16Verifier();

        testDisabledFloat();

        // perfTest();
    }
};

BEAST_DEFINE_TESTSUITE(Wasm, app, ripple);

}  // namespace test
}  // namespace ripple
