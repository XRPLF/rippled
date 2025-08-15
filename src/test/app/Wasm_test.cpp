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

bool
testGetDataIncrement();

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
    testGetDataHelperFunctions()
    {
        testcase("getData helper functions");
        BEAST_EXPECT(testGetDataIncrement());
    }

    void
    testWasmFib()
    {
        testcase("Wasm fibo");

        auto const ws = boost::algorithm::unhex(fib32Hex);
        Bytes const wasm(ws.begin(), ws.end());
        auto& engine = WasmEngine::instance();

        auto const re = engine.run(wasm, "fib", wasmParams(10));

        BEAST_EXPECT(re.has_value() && (re->result == 55) && (re->cost == 755));
    }

    void
    testWasmSha()
    {
        testcase("Wasm sha");

        auto const ws = boost::algorithm::unhex(sha512PureHex);
        Bytes const wasm(ws.begin(), ws.end());
        auto& engine = WasmEngine::instance();

        auto const re =
            engine.run(wasm, "sha512_process", wasmParams(sha512PureHex));

        BEAST_EXPECT(
            re.has_value() && (re->result == 34432) && (re->cost == 157'452));
    }

    void
    testWasmB58()
    {
        testcase("Wasm base58");
        auto const ws = boost::algorithm::unhex(b58Hex);
        Bytes const wasm(ws.begin(), ws.end());
        auto& engine = WasmEngine::instance();

        Bytes outb;
        outb.resize(1024);

        auto const minsz = std::min(
            static_cast<std::uint32_t>(512),
            static_cast<std::uint32_t>(b58Hex.size()));
        auto const s = std::string_view(b58Hex.c_str(), minsz);
        auto const re = engine.run(wasm, "b58enco", wasmParams(outb, s));

        BEAST_EXPECT(re.has_value() && re->result && (re->cost == 3'066'129));
    }

    void
    testWasmSP1Verifier()
    {
        testcase("Wasm sp1 zkproof verifier");
        auto const ws = boost::algorithm::unhex(sp1_wasm);
        Bytes const wasm(ws.begin(), ws.end());
        auto& engine = WasmEngine::instance();

        auto const re = engine.run(wasm, "sp1_groth16_verifier");

        BEAST_EXPECT(
            re.has_value() && re->result && (re->cost == 4'191'711'969ll));
    }

    void
    testWasmBG16Verifier()
    {
        testcase("Wasm BG16 zkproof verifier");
        auto const ws = boost::algorithm::unhex(zkProofHex);
        Bytes const wasm(ws.begin(), ws.end());
        auto& engine = WasmEngine::instance();

        auto const re = engine.run(wasm, "bellman_groth16_test");

        BEAST_EXPECT(re.has_value() && re->result && (re->cost == 332'205'984));
    }

    void
    testWasmLedgerSqn()
    {
        testcase("Wasm get ledger sequence");

        auto wasmStr = boost::algorithm::unhex(ledgerSqnHex);
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
            BEAST_EXPECT(!re->result && (re->cost == 44));

        env.close();
        env.close();
        env.close();
        env.close();

        // empty module - run the same instance
        re = engine.run(
            {}, ESCROW_FUNCTION_NAME, {}, imports, &hf, 1'000'000, env.journal);

        // code takes 22 gas + 2 getLedgerSqn calls
        if (BEAST_EXPECT(re.has_value()))
            BEAST_EXPECT(re->result && (re->cost == 88));
    }

    void
    testWasmCheckJson()
    {
        testcase("Wasm check json");

        using namespace test::jtx;
        Env env{*this};

        auto const wasmStr = boost::algorithm::unhex(checkJsonHex);
        Bytes const wasm(wasmStr.begin(), wasmStr.end());
        std::string const funcName("check_accountID");
        {
            std::string str = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            Bytes data(str.begin(), str.end());
            auto re = runEscrowWasm(
                wasm, funcName, wasmParams(data), nullptr, -1, env.journal);
            if (BEAST_EXPECT(re.has_value()))
                BEAST_EXPECT(re.value().result && (re->cost == 838));
        }
        {
            std::string str = "rHb9CJAWyB4rj91VRWn96DkukG4bwdty00";
            Bytes data(str.begin(), str.end());
            auto re = runEscrowWasm(
                wasm, funcName, wasmParams(data), nullptr, -1, env.journal);
            if (BEAST_EXPECT(re.has_value()))
                BEAST_EXPECT(!re.value().result && (re->cost == 822));
        }
    }

    void
    testWasmCompareJson()
    {
        testcase("Wasm compare json");

        using namespace test::jtx;
        Env env{*this};

        auto wasmStr = boost::algorithm::unhex(compareJsonHex);
        std::vector<uint8_t> wasm(wasmStr.begin(), wasmStr.end());
        std::string funcName("compare_accountID");

        std::vector<uint8_t> const tx_data(tx_js.begin(), tx_js.end());
        std::vector<uint8_t> const lo_data(lo_js.begin(), lo_js.end());
        auto re = runEscrowWasm(
            wasm,
            funcName,
            wasmParams(tx_data, lo_data),
            nullptr,
            -1,
            env.journal);
        if (BEAST_EXPECT(re.has_value()))
            BEAST_EXPECT(re.value().result && (re->cost == 42'212));

        std::vector<uint8_t> const lo_data2(lo_js2.begin(), lo_js2.end());
        re = runEscrowWasm(
            wasm,
            funcName,
            wasmParams(tx_data, lo_data2),
            nullptr,
            -1,
            env.journal);
        if (BEAST_EXPECT(re.has_value()))
            BEAST_EXPECT(!re.value().result && (re->cost == 41'496));
    }

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

        BEAST_EXPECT(re.has_value() && re->result == 6912 && (re->cost == 2));
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
    testEscrowWasmDN()
    {
        testcase("escrow wasm devnet test");

        std::string const wasmStr =
            boost::algorithm::unhex(allHostFunctionsHex);
        std::vector<uint8_t> wasm(wasmStr.begin(), wasmStr.end());

        using namespace test::jtx;
        Env env{*this};
        {
            TestHostFunctions nfs(env, 0);
            auto re =
                runEscrowWasm(wasm, ESCROW_FUNCTION_NAME, {}, &nfs, 100'000);
            if (BEAST_EXPECT(re.has_value()))
            {
                BEAST_EXPECT(re->result && (re->cost == 41'132));
            }
        }

        {  // fail because trying to access nonexistent field
            struct BadTestHostFunctions : public TestHostFunctions
            {
                explicit BadTestHostFunctions(Env& env) : TestHostFunctions(env)
                {
                }
                Expected<Bytes, HostFunctionError>
                getTxField(SField const& fname) override
                {
                    return Unexpected(HostFunctionError::FIELD_NOT_FOUND);
                }
            };
            BadTestHostFunctions nfs(env);
            auto re =
                runEscrowWasm(wasm, ESCROW_FUNCTION_NAME, {}, &nfs, 100000);
            if (BEAST_EXPECT(re.has_value()))
            {
                BEAST_EXPECT(re->result == -201);
                BEAST_EXPECT(re->cost == 5831);
            }
        }

        {  // fail because trying to allocate more than MAX_PAGES memory
            struct BadTestHostFunctions : public TestHostFunctions
            {
                explicit BadTestHostFunctions(Env& env) : TestHostFunctions(env)
                {
                }
                Expected<Bytes, HostFunctionError>
                getTxField(SField const& fname) override
                {
                    return Bytes((MAX_PAGES + 1) * 64 * 1024, 1);
                }
            };
            BadTestHostFunctions nfs(env);
            auto re =
                runEscrowWasm(wasm, ESCROW_FUNCTION_NAME, {}, &nfs, 100'000);
            if (BEAST_EXPECT(re.has_value()))
            {
                BEAST_EXPECT(re->result == -201);
                BEAST_EXPECT(re->cost == 5831);
            }
        }

        {  // fail because recursion too deep

            auto const wasmStr = boost::algorithm::unhex(deepRecursionHex);
            std::vector<uint8_t> wasm(wasmStr.begin(), wasmStr.end());

            TestHostFunctionsSink nfs(env);
            std::string funcName("recursive");
            auto re = runEscrowWasm(wasm, funcName, {}, &nfs, 1000'000'000);
            BEAST_EXPECT(!re && re.error());
            // std::cout << "bad case (deep recursion) result " << re.error()
            //             << std::endl;

            auto const& sink = nfs.getSink();
            auto countSubstr = [](std::string const& str,
                                  std::string const& substr) {
                std::size_t pos = 0;
                int occurrences = 0;
                while ((pos = str.find(substr, pos)) != std::string::npos)
                {
                    occurrences++;
                    pos += substr.length();
                }
                return occurrences;
            };

            auto const s = sink.messages().str();
            BEAST_EXPECT(
                countSubstr(s, "WAMR Error: failure to call func") == 1);
            BEAST_EXPECT(
                countSubstr(s, "Exception: wasm operand stack overflow") > 0);
        }

        {
            auto wasmStr = boost::algorithm::unhex(ledgerSqnHex);
            Bytes wasm(wasmStr.begin(), wasmStr.end());
            TestLedgerDataProvider ledgerDataProvider(&env);

            std::vector<WasmImportFunc> imports;
            WASM_IMPORT_FUNC2(
                imports, getLedgerSqn, "get_ledger_sqn2", &ledgerDataProvider);

            auto& engine = WasmEngine::instance();

            auto re = engine.run(
                wasm,
                ESCROW_FUNCTION_NAME,
                {},
                imports,
                nullptr,
                1'000'000,
                env.journal);

            // expected import not provided
            BEAST_EXPECT(!re);
        }
    }

    void
    testHFCost()
    {
        testcase("wasm test host functions cost");

        using namespace test::jtx;

        Env env(*this);
        {
            std::string const wasmHex = allHostFunctionsHex;
            std::string const wasmStr = boost::algorithm::unhex(wasmHex);
            std::vector<uint8_t> const wasm(wasmStr.begin(), wasmStr.end());

            auto& engine = WasmEngine::instance();

            TestHostFunctions hfs(env, 0);
            std::vector<WasmImportFunc> imp = createWasmImport(&hfs);
            for (auto& i : imp)
                i.gas = 0;

            auto re = engine.run(
                wasm,
                ESCROW_FUNCTION_NAME,
                {},
                imp,
                &hfs,
                1'000'000,
                env.journal);

            if (BEAST_EXPECT(re.has_value()))
            {
                BEAST_EXPECT(re->result && (re->cost == 872));
            }

            env.close();
        }

        env.close();
        env.close();
        env.close();
        env.close();
        env.close();

        {
            std::string const wasmHex = allHostFunctionsHex;
            std::string const wasmStr = boost::algorithm::unhex(wasmHex);
            std::vector<uint8_t> const wasm(wasmStr.begin(), wasmStr.end());

            auto& engine = WasmEngine::instance();

            TestHostFunctions hfs(env, 0);
            std::vector<WasmImportFunc> const imp = createWasmImport(&hfs);

            auto re = engine.run(
                wasm,
                ESCROW_FUNCTION_NAME,
                {},
                imp,
                &hfs,
                1'000'000,
                env.journal);

            if (BEAST_EXPECT(re.has_value()))
            {
                BEAST_EXPECT(re->result && (re->cost == 41'132));
            }

            env.close();
        }
    }

    void
    testFloat()
    {
        testcase("float point");

        std::string const funcName("finish");

        using namespace test::jtx;

        Env env(*this);
        {
            std::string const wasmHex = floatHex;
            std::string const wasmStr = boost::algorithm::unhex(wasmHex);
            std::vector<uint8_t> const wasm(wasmStr.begin(), wasmStr.end());

            TestHostFunctions hf(env, 0);
            auto re = runEscrowWasm(wasm, funcName, {}, &hf, 100'000);
            BEAST_EXPECT(re && re->result && (re->cost == 91'412));
            env.close();
        }

        {
            std::string const wasmHex = float0Hex;
            std::string const wasmStr = boost::algorithm::unhex(wasmHex);
            std::vector<uint8_t> const wasm(wasmStr.begin(), wasmStr.end());

            TestHostFunctions hf(env, 0);
            auto re = runEscrowWasm(wasm, funcName, {}, &hf, 100'000);
            BEAST_EXPECT(re && re->result && (re->cost == 6'533));
            env.close();
        }
    }

    void
    perfTest()
    {
        testcase("Perf test host functions");

        using namespace jtx;
        using namespace std::chrono;

        // std::string const funcName("test");
        auto const& wasmHex = hfPerfTest;
        // auto const& wasmHex = opcCallPerfTest;
        std::string const wasmStr = boost::algorithm::unhex(wasmHex);
        std::vector<uint8_t> const wasm(wasmStr.begin(), wasmStr.end());

        std::string const credType = "abcde";
        std::string const credType2 = "fghijk";
        std::string const credType3 = "0123456";
        // char const uri[] = "uri";

        Account const alan{"alan"};
        Account const bob{"bob"};
        Account const issuer{"issuer"};

        {
            Env env(*this);
            // Env env(*this, envconfig(), {}, nullptr,
            // beast::severities::kTrace);
            env.fund(XRP(5000), alan, bob, issuer);
            env.close();

            // // create escrow
            // auto const seq = env.seq(alan);
            // auto const k = keylet::escrow(alan, seq);
            // // auto const allowance = 3'600;
            // auto escrowCreate = escrow::create(alan, bob, XRP(1000));
            // XRPAmount txnFees = env.current()->fees().base + 1000;
            // env(escrowCreate,
            //     escrow::finish_function(wasmHex),
            //     escrow::finish_time(env.now() + 11s),
            //     escrow::cancel_time(env.now() + 100s),
            //     escrow::data("1000000000"),  // 1000 XRP in drops
            //     memodata("memo1234567"),
            //     memodata("2memo1234567"),
            //     fee(txnFees));

            // // create depositPreauth
            // auto const k = keylet::depositPreauth(
            //     bob,
            //     {{issuer.id(), makeSlice(credType)},
            //      {issuer.id(), makeSlice(credType2)},
            //      {issuer.id(), makeSlice(credType3)}});
            // env(deposit::authCredentials(
            //     bob,
            //     {{issuer, credType},
            //      {issuer, credType2},
            //      {issuer, credType3}}));

            // cREATE nft
            [[maybe_unused]] uint256 const nft0{
                token::getNextID(env, alan, 0u)};
            env(token::mint(alan, 0u));
            auto const k = keylet::nftoffer(alan, 0);
            [[maybe_unused]] uint256 const nft1{
                token::getNextID(env, alan, 0u)};

            env(token::mint(alan, 0u),
                token::uri(
                    "https://github.com/XRPLF/XRPL-Standards/discussions/"
                    "279?id=github.com/XRPLF/XRPL-Standards/discussions/"
                    "279&ut=github.com/XRPLF/XRPL-Standards/discussions/"
                    "279&sid=github.com/XRPLF/XRPL-Standards/discussions/"
                    "279&aot=github.com/XRPLF/XRPL-Standards/disc"));
            [[maybe_unused]] uint256 const nft2{
                token::getNextID(env, alan, 0u)};
            env(token::mint(alan, 0u));
            env.close();

            PerfHostFunctions nfs(env, k, env.tx());

            auto re = runEscrowWasm(wasm, ESCROW_FUNCTION_NAME, {}, &nfs);
            if (BEAST_EXPECT(re.has_value()))
            {
                BEAST_EXPECT(re->result);
                std::cout << "Res: " << re->result << " cost: " << re->cost
                          << std::endl;
            }

            // env(escrow::finish(alan, alan, seq),
            //     escrow::comp_allowance(allowance),
            //     fee(txnFees),
            //     ter(tesSUCCESS));

            env.close();
        }
    }

    void
    testCodecovWasm()
    {
        testcase("Codecov wasm test");

        using namespace test::jtx;

        // Env env{
        //     *this,
        //     envconfig(),
        //     testable_amendments(),
        //     nullptr,
        //     beast::severities::kTrace};
        Env env{*this};

        auto const wasmStr = boost::algorithm::unhex(codecovWasm);
        Bytes const wasm(wasmStr.begin(), wasmStr.end());
        std::string const funcName("finish");
        TestHostFunctions hfs(env, 0);

        auto re =
            runEscrowWasm(wasm, funcName, {}, &hfs, 1'000'000, env.journal);

        if (BEAST_EXPECT(re.has_value()))
            BEAST_EXPECT(re->result);
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
        TestHostFunctions hfs(env, 0);

        {
            // f32 set constant, opcode disabled exception
            auto const re =
                runEscrowWasm(wasm, funcName, {}, &hfs, 1'000'000, env.journal);
            BEAST_EXPECT(!re && re.error() == tecFAILED_PROCESSING);
        }

        {
            // f32 add, can't create module exception
            wasm[0x117] = 0x92;
            auto const re =
                runEscrowWasm(wasm, funcName, {}, &hfs, 1'000'000, env.journal);
            BEAST_EXPECT(!re && re.error() == tecFAILED_PROCESSING);
        }
    }

    void
    run() override
    {
        using namespace test::jtx;

        testGetDataHelperFunctions();
        testWasmLib();
        testBadWasm();
        testWasmCheckJson();
        testWasmCompareJson();
        testWasmLedgerSqn();

        testWasmFib();
        testWasmSha();
        testWasmB58();

        // runing too long
        // testWasmSP1Verifier();
        testWasmBG16Verifier();

        testHFCost();

        testEscrowWasmDN();
        testFloat();

        testCodecovWasm();
        testDisabledFloat();

        // perfTest();
    }
};

BEAST_DEFINE_TESTSUITE(Wasm, app, ripple);

}  // namespace test
}  // namespace ripple
