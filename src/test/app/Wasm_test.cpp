#ifdef _DEBUG
// #define DEBUG_OUTPUT 1
#endif

#include <test/app/TestHostFunctions.h>

#include <xrpld/app/wasm/HostFuncWrapper.h>

#include <source_location>

namespace xrpl {
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

std::vector<uint8_t> const
hexToBytes(std::string const& hex)
{
    auto const ws = boost::algorithm::unhex(hex);
    return Bytes(ws.begin(), ws.end());
}

std::optional<int32_t>
runFinishFunction(std::string const& code)
{
    auto& engine = WasmEngine::instance();
    auto const wasm = hexToBytes(code);
    auto const re = engine.run(wasm, "finish");
    if (re.has_value())
    {
        return std::optional<int32_t>(re->result);
    }
    else
    {
        return std::nullopt;
    }
}

struct Wasm_test : public beast::unit_test::suite
{
    void
    checkResult(
        Expected<WasmResult<int32_t>, TER> re,
        int32_t expectedResult,
        int64_t expectedCost,
        std::source_location const location = std::source_location::current())
    {
        auto const lineStr = " (" + std::to_string(location.line()) + ")";
        if (BEAST_EXPECTS(re.has_value(), transToken(re.error()) + lineStr))
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

        std::shared_ptr<ImportVec> imports(std::make_shared<ImportVec>());
        WasmImpFunc<Add_proto>(*imports, "func-add", reinterpret_cast<void*>(&Add));

        auto re = vm.run(wasm, "addTwo", wasmParams(1234, 5678), imports);

        // if (res) printf("invokeAdd get the result: %d\n", res.value());

        checkResult(re, 6'912, 59);
    }

    void
    testBadWasm()
    {
        testcase("bad wasm test");

        using namespace test::jtx;

        Env env{*this};
        std::shared_ptr<HostFunctions> hfs(new HostFunctions(env.journal));

        {
            auto wasm = hexToBytes("00000000");
            std::string funcName("mock_escrow");

            auto re = runEscrowWasm(wasm, hfs, funcName, {}, 15);
            BEAST_EXPECT(!re);
        }

        {
            auto wasm = hexToBytes("00112233445566778899AA");
            std::string funcName("mock_escrow");

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

            auto const re = preflightEscrowWasm(badWasm, hfs, ESCROW_FUNCTION_NAME);
            BEAST_EXPECT(!isTesSuccess(re));
        }
    }

    void
    testWasmLedgerSqn()
    {
        testcase("Wasm get ledger sequence");

        auto ledgerSqnWasm = hexToBytes(ledgerSqnWasmHex);

        using namespace test::jtx;

        Env env{*this};
        std::shared_ptr<HostFunctions> hfs(new TestLedgerDataProvider(env));
        auto imports = std::make_shared<ImportVec>();
        WASM_IMPORT_FUNC2(*imports, getLedgerSqn, "get_ledger_sqn", hfs.get(), 33);
        auto& engine = WasmEngine::instance();

        auto re = engine.run(ledgerSqnWasm, ESCROW_FUNCTION_NAME, {}, imports, hfs, 1'000'000, env.journal);

        checkResult(re, 0, 440);

        env.close();
        env.close();

        // empty module - run the same instance
        re = engine.run({}, ESCROW_FUNCTION_NAME, {}, imports, hfs, 1'000'000, env.journal);

        checkResult(re, 5, 488);
    }

    void
    testWasmFib()
    {
        testcase("Wasm fibo");

        auto const fibWasm = hexToBytes(fibWasmHex);
        auto& engine = WasmEngine::instance();

        auto const re = engine.run(fibWasm, "fib", wasmParams(10));

        checkResult(re, 55, 1'137);
    }

    void
    testWasmSha()
    {
        testcase("Wasm sha");

        auto const sha512Wasm = hexToBytes(sha512PureWasmHex);
        auto& engine = WasmEngine::instance();

        auto const re = engine.run(sha512Wasm, "sha512_process", wasmParams(sha512PureWasmHex));

        checkResult(re, 34'432, 151'155);
    }

    void
    testWasmB58()
    {
        testcase("Wasm base58");
        auto const b58Wasm = hexToBytes(b58WasmHex);
        auto& engine = WasmEngine::instance();

        Bytes outb;
        outb.resize(1024);

        auto const minsz = std::min(static_cast<std::uint32_t>(512), static_cast<std::uint32_t>(b58WasmHex.size()));
        auto const s = std::string_view(b58WasmHex.c_str(), minsz);

        auto const re = engine.run(b58Wasm, "b58enco", wasmParams(outb, s));

        checkResult(re, 700, 2'886'069);
    }

    void
    testHFCost()
    {
        testcase("wasm test host functions cost");

        using namespace test::jtx;

        Env env(*this);
        {
            auto const allHostFuncWasm = hexToBytes(allHostFunctionsWasmHex);

            auto& engine = WasmEngine::instance();

            std::shared_ptr<HostFunctions> hfs(new TestHostFunctions(env, 0));
            auto imp = createWasmImport(*hfs);
            for (auto& i : *imp)
                i.second.gas = 0;

            auto re = engine.run(allHostFuncWasm, ESCROW_FUNCTION_NAME, {}, imp, hfs, 1'000'000, env.journal);

            checkResult(re, 1, 27'080);

            env.close();
        }

        env.close();
        env.close();
        env.close();
        env.close();
        env.close();

        {
            auto const allHostFuncWasm = hexToBytes(allHostFunctionsWasmHex);

            auto& engine = WasmEngine::instance();

            std::shared_ptr<HostFunctions> hfs(new TestHostFunctions(env, 0));
            auto const imp = createWasmImport(*hfs);

            auto re = engine.run(allHostFuncWasm, ESCROW_FUNCTION_NAME, {}, imp, hfs, 1'000'000, env.journal);

            checkResult(re, 1, 65'840);

            env.close();
        }

        // not enough gas
        {
            auto const allHostFuncWasm = hexToBytes(allHostFunctionsWasmHex);

            auto& engine = WasmEngine::instance();

            std::shared_ptr<HostFunctions> hfs(new TestHostFunctions(env, 0));
            auto const imp = createWasmImport(*hfs);

            auto re = engine.run(allHostFuncWasm, ESCROW_FUNCTION_NAME, {}, imp, hfs, 200, env.journal);

            if (BEAST_EXPECT(!re))
            {
                BEAST_EXPECTS(re.error() == tecFAILED_PROCESSING, std::to_string(TERtoInt(re.error())));
            }

            env.close();
        }
    }

    void
    testEscrowWasmDN()
    {
        testcase("escrow wasm devnet test");

        auto const allHFWasm = hexToBytes(allHostFunctionsWasmHex);

        using namespace test::jtx;
        Env env{*this};
        {
            std::shared_ptr<HostFunctions> hfs(new TestHostFunctions(env, 0));
            auto re = runEscrowWasm(allHFWasm, hfs, ESCROW_FUNCTION_NAME, {}, 100'000);
            checkResult(re, 1, 65'840);
        }

        {
            // max<int64_t>() gas
            std::shared_ptr<HostFunctions> hfs(new TestHostFunctions(env, 0));
            auto re = runEscrowWasm(allHFWasm, hfs, ESCROW_FUNCTION_NAME, {}, -1);
            checkResult(re, 1, 65'840);
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

            std::shared_ptr<HostFunctions> hfs(new BadTestHostFunctions(env));
            auto re = runEscrowWasm(allHFWasm, hfs, ESCROW_FUNCTION_NAME, {}, 100'000);
            checkResult(re, -201, 28'965);
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
                    return Bytes((128 + 1) * 64 * 1024, 1);
                }
            };

            std::shared_ptr<HostFunctions> hfs(new BadTestHostFunctions(env));
            auto re = runEscrowWasm(allHFWasm, hfs, ESCROW_FUNCTION_NAME, {}, 100'000);
            checkResult(re, -201, 28'965);
        }

        {  // fail because recursion too deep

            auto const deepWasm = hexToBytes(deepRecursionHex);

            std::shared_ptr<TestHostFunctionsSink> hfs(new TestHostFunctionsSink(env));
            std::string funcName("finish");
            auto re = runEscrowWasm(deepWasm, hfs, funcName, {}, 1'000'000'000);
            BEAST_EXPECT(!re && re.error());
            // std::cout << "bad case (deep recursion) result " << re.error()
            //             << std::endl;

            auto const& sink = hfs->getSink();
            auto countSubstr = [](std::string const& str, std::string const& substr) {
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
            BEAST_EXPECT(countSubstr(s, "WASMI Error: failure to call func") == 1);
            BEAST_EXPECT(countSubstr(s, "exception: <finish> failure") > 0);
        }

        {  // infinite loop
            auto const infiniteLoopWasm = hexToBytes(infiniteLoopWasmHex);
            std::string const funcName("loop");
            std::shared_ptr<HostFunctions> hfs(new TestHostFunctions(env, 0));

            // infinite loop should be caught and fail
            auto const re = runEscrowWasm(infiniteLoopWasm, hfs, funcName, {}, 1'000'000);
            if (BEAST_EXPECT(!re.has_value()))
            {
                BEAST_EXPECT(re.error() == tecFAILED_PROCESSING);
            }
        }

        {
            // expected import not provided
            auto const lgrSqnWasm = hexToBytes(ledgerSqnWasmHex);
            std::shared_ptr<HostFunctions> hfs(new TestLedgerDataProvider(env));
            std::shared_ptr<ImportVec> imports(std::make_shared<ImportVec>());
            WASM_IMPORT_FUNC2(*imports, getLedgerSqn, "get_ledger_sqn2", hfs.get());

            auto& engine = WasmEngine::instance();

            auto re = engine.run(lgrSqnWasm, ESCROW_FUNCTION_NAME, {}, imports, hfs, 1'000'000, env.journal);

            BEAST_EXPECT(!re);
        }

        {
            // bad import format
            auto const lgrSqnWasm = hexToBytes(ledgerSqnWasmHex);
            std::shared_ptr<HostFunctions> hfs(new TestLedgerDataProvider(env));
            std::shared_ptr<ImportVec> imports(std::make_shared<ImportVec>());
            WASM_IMPORT_FUNC2(*imports, getLedgerSqn, "get_ledger_sqn", hfs.get());
            (*imports)[0].first = nullptr;

            auto& engine = WasmEngine::instance();

            auto re = engine.run(lgrSqnWasm, ESCROW_FUNCTION_NAME, {}, imports, hfs, 1'000'000, env.journal);

            BEAST_EXPECT(!re);
        }

        {
            // bad function name
            auto const lgrSqnWasm = hexToBytes(ledgerSqnWasmHex);
            std::shared_ptr<HostFunctions> hfs(new TestLedgerDataProvider(env));
            std::shared_ptr<ImportVec> imports(std::make_shared<ImportVec>());
            WASM_IMPORT_FUNC2(*imports, getLedgerSqn, "get_ledger_sqn", hfs.get());

            auto& engine = WasmEngine::instance();
            auto re = engine.run(lgrSqnWasm, "func1", {}, imports, hfs, 1'000'000, env.journal);

            BEAST_EXPECT(!re);
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
            auto const floatTestWasm = hexToBytes(floatTestsWasmHex);

            std::shared_ptr<HostFunctions> hfs(new TestHostFunctions(env, 0));
            auto re = runEscrowWasm(floatTestWasm, hfs, funcName, {}, 200'000);
            checkResult(re, 1, 110'699);
            env.close();
        }

        {
            auto const float0Wasm = hexToBytes(float0Hex);

            std::shared_ptr<HostFunctions> hfs(new TestHostFunctions(env, 0));
            auto re = runEscrowWasm(float0Wasm, hfs, funcName, {}, 100'000);
            checkResult(re, 1, 4'259);
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
        auto const perfWasm = hexToBytes(hfPerfTest);

        // std::string const credType = "abcde";
        // std::string const credType2 = "fghijk";
        // std::string const credType3 = "0123456";
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

            // create nft
            [[maybe_unused]] uint256 const nft0{token::getNextID(env, alan, 0u)};
            env(token::mint(alan, 0u));
            auto const k = keylet::nftoffer(alan, 0);
            [[maybe_unused]] uint256 const nft1{token::getNextID(env, alan, 0u)};

            env(token::mint(alan, 0u),
                token::uri("https://github.com/XRPLF/XRPL-Standards/discussions/"
                           "279?id=github.com/XRPLF/XRPL-Standards/discussions/"
                           "279&ut=github.com/XRPLF/XRPL-Standards/discussions/"
                           "279&sid=github.com/XRPLF/XRPL-Standards/discussions/"
                           "279&aot=github.com/XRPLF/XRPL-Standards/disc"));
            [[maybe_unused]] uint256 const nft2{token::getNextID(env, alan, 0u)};
            env(token::mint(alan, 0u));
            env.close();

            std::shared_ptr<HostFunctions> hfs(new PerfHostFunctions(env, k, env.tx()));

            auto re = runEscrowWasm(perfWasm, hfs, ESCROW_FUNCTION_NAME);
            if (BEAST_EXPECT(re.has_value()))
            {
                BEAST_EXPECT(re->result);
                std::cout << "Res: " << re->result << " cost: " << re->cost << std::endl;
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

        Env env{*this};

        auto const codecovWasm = hexToBytes(codecovTestsWasmHex);
        std::shared_ptr<HostFunctions> hfs(new TestHostFunctions(env, 0));

        auto const allowance = 339'303;
        auto re = runEscrowWasm(codecovWasm, hfs, ESCROW_FUNCTION_NAME, {}, allowance);

        checkResult(re, 1, allowance);
    }

    void
    testDisabledFloat()
    {
        testcase("disabled float");

        using namespace test::jtx;
        Env env{*this};

        auto disabledFloatWasm = hexToBytes(disabledFloatHex);
        std::string const funcName("finish");
        std::shared_ptr<HostFunctions> hfs(new TestHostFunctions(env, 0));

        {
            // f32 set constant, opcode disabled exception
            auto const re = runEscrowWasm(disabledFloatWasm, hfs, funcName, {}, 1'000'000);
            if (BEAST_EXPECT(!re.has_value()))
            {
                BEAST_EXPECT(re.error() == tecFAILED_PROCESSING);
            }
        }

        {
            // f32 add, can't create module exception
            disabledFloatWasm[0x117] = 0x92;
            auto const re = runEscrowWasm(disabledFloatWasm, hfs, funcName, {}, 1'000'000);
            if (BEAST_EXPECT(!re.has_value()))
            {
                BEAST_EXPECT(re.error() == tecFAILED_PROCESSING);
            }
        }
    }

    void
    testWasmMemory()
    {
        testcase("Wasm additional memory limit tests");
        BEAST_EXPECT(runFinishFunction(memoryPointerAtLimitHex).value() == 1);
        BEAST_EXPECT(runFinishFunction(memoryPointerOverLimitHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(memoryOffsetOverLimitHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(memoryEndOfWordOverLimitHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(memoryGrow0To1PageHex).value() == 1);
        BEAST_EXPECT(runFinishFunction(memoryGrow1To0PageHex).value() == -1);
        BEAST_EXPECT(runFinishFunction(memoryLastByteOf8MBHex).value() == 1);
        BEAST_EXPECT(runFinishFunction(memoryGrow1MoreThan8MBHex).value() == -1);
        BEAST_EXPECT(runFinishFunction(memoryGrow0MoreThan8MBHex).value() == 1);
        BEAST_EXPECT(runFinishFunction(memoryInit1MoreThan8MBHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(memoryNegativeAddressHex).has_value() == false);
    }

    void
    testWasmTable()
    {
        testcase("Wasm table limit tests");
        BEAST_EXPECT(runFinishFunction(table64ElementsHex).value() == 1);
        BEAST_EXPECT(runFinishFunction(table65ElementsHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(table2TablesHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(table0ElementsHex).value() == 1);
        BEAST_EXPECT(runFinishFunction(tableUintMaxHex).has_value() == false);
    }

    void
    testWasmProposal()
    {
        testcase("Wasm disabled proposal tests");
        BEAST_EXPECT(runFinishFunction(proposalMutableGlobalHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(proposalGcStructNewHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(proposalMultiValueHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(proposalSignExtHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(proposalFloatToIntHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(proposalBulkMemoryHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(proposalRefTypesHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(proposalTailCallHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(proposalExtendedConstHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(proposalMultiMemoryHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(proposalCustomPageSizesHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(proposalMemory64Hex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(proposalWideArithmeticHex).has_value() == false);
    }

    void
    testWasmTrap()
    {
        testcase("Wasm trap tests");
        BEAST_EXPECT(runFinishFunction(trapDivideBy0Hex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(trapIntOverflowHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(trapUnreachableHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(trapNullCallHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(trapFuncSigMismatchHex).has_value() == false);
    }

    void
    testWasmWasi()
    {
        testcase("Wasm Wasi tests");
        BEAST_EXPECT(runFinishFunction(wasiGetTimeHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(wasiPrintHex).has_value() == false);
    }

    void
    testWasmSectionCorruption()
    {
        testcase("Wasm Section Corruption tests");
        BEAST_EXPECT(runFinishFunction(badMagicNumberHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(badVersionNumberHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(lyingHeaderHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(neverEndingNumberHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(vectorLieHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(sectionOrderingHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(ghostPayloadHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(junkAfterSectionHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(invalidSectionIdHex).has_value() == false);
        BEAST_EXPECT(runFinishFunction(localVariableBombHex).has_value() == false);
    }

    void
    testStartFunctionLoop()
    {
        testcase("infinite loop in start function");

        using namespace test::jtx;
        Env env(*this);

        auto const startLoopWasm = hexToBytes(startLoopHex);
        std::shared_ptr<HostFunctions> hfs(new TestLedgerDataProvider(env));
        std::shared_ptr<ImportVec> imports(std::make_shared<ImportVec>());

        auto& engine = WasmEngine::instance();
        auto checkRes = engine.check(startLoopWasm, "finish", {}, imports, hfs, env.journal);
        BEAST_EXPECTS(checkRes == tesSUCCESS, std::to_string(TERtoInt(checkRes)));

        auto re = engine.run(startLoopWasm, ESCROW_FUNCTION_NAME, {}, imports, hfs, 1'000'000, env.journal);
        BEAST_EXPECTS(re.error() == tecFAILED_PROCESSING, std::to_string(TERtoInt(re.error())));
    }

    void
    testBadAlloc()
    {
        testcase("Wasm Bad Alloc");

        // bad_alloc.c
        auto const badAllocWasm = hexToBytes(badAllocHex);

        using namespace test::jtx;

        Env env{*this};
        std::shared_ptr<HostFunctions> hfs(new TestLedgerDataProvider(env));

        // std::shared_ptr<ImportVec> imports(std::make_shared<ImportVec>());
        uint8_t buf1[8] = {7, 8, 9, 10, 11, 12, 13, 14};
        {  // forged "allocate" return valid address
            std::vector<WasmParam> params = {{.type = WT_U8V, .of = {.u8v = {.d = buf1, .sz = sizeof(buf1)}}}};
            auto& engine = WasmEngine::instance();

            auto re = engine.run(badAllocWasm, "test", params, {}, hfs, 1'000'000, env.journal);
            if (BEAST_EXPECT(re))
            {
                BEAST_EXPECTS(re->result == 7, std::to_string(re->result));
                BEAST_EXPECTS(re->cost == 430, std::to_string(re->cost));
            }
        }

        {  // return 0 whithout calling wasm
            std::vector<WasmParam> params = {{.type = WT_U8V, .of = {.u8v = {.d = buf1, .sz = 0}}}};
            auto& engine = WasmEngine::instance();
            auto re = engine.run(badAllocWasm, "test", params, {}, hfs, 1'000'000, env.journal);
            BEAST_EXPECT(!re) && BEAST_EXPECT(re.error() == tecFAILED_PROCESSING);
        }

        {  // forged "allocate" return 8Mb (which is more than memory limit)
            std::vector<WasmParam> params = {{.type = WT_U8V, .of = {.u8v = {.d = buf1, .sz = 1}}}};
            auto& engine = WasmEngine::instance();
            auto re = engine.run(badAllocWasm, "test", params, {}, hfs, 1'000'000, env.journal);
            BEAST_EXPECT(!re) && BEAST_EXPECT(re.error() == tecFAILED_PROCESSING);
        }

        {  // forged "allocate" return 0
            std::vector<WasmParam> params = {{.type = WT_U8V, .of = {.u8v = {.d = buf1, .sz = 2}}}};
            auto& engine = WasmEngine::instance();
            auto re = engine.run(badAllocWasm, "test", params, {}, hfs, 1'000'000, env.journal);
            BEAST_EXPECT(!re) && BEAST_EXPECT(re.error() == tecFAILED_PROCESSING);
        }

        {  // forged "allocate" return -1
            std::vector<WasmParam> params = {{.type = WT_U8V, .of = {.u8v = {.d = buf1, .sz = 3}}}};
            auto& engine = WasmEngine::instance();
            auto re = engine.run(badAllocWasm, "test", params, {}, hfs, 1'000'000, env.journal);

            BEAST_EXPECT(!re) && BEAST_EXPECT(re.error() == tecFAILED_PROCESSING);
        }

        {
            std::string what;
            try
            {
                char const test[] = "test";
                std::size_t sz = std::numeric_limits<int32_t>::max() + 1ull;
                auto p = wasmParams(std::string_view(test, sz));
            }
            catch (std::exception const& e)
            {
                what = e.what();
            }
            BEAST_EXPECT(what.find("can't allocate memory, size: 2147483648") != std::string::npos);
        }

        env.close();
    }

    void
    testBadAlign()
    {
        testcase("Wasm Bad Align");

        // bad_align.c
        auto const badAlignWasm = hexToBytes(badAlignWasmHex);

        using namespace test::jtx;

        Env env{*this};
        std::shared_ptr<HostFunctions> hfs(new TestHostFunctions(env, 0));
        auto imports = createWasmImport(*hfs);

        {  // Calls float_from_uint with bad aligment.
           // Can be checked through codecov
            auto& engine = WasmEngine::instance();

            auto re = engine.run(badAlignWasm, "test", {}, imports, hfs, 1'000'000, env.journal);
            if (BEAST_EXPECTS(re, transToken(re.error())))
            {
                BEAST_EXPECTS(re->result == 0x684f7941, std::to_string(re->result));
            }
        }

        env.close();
    }

    void
    testReturnType()
    {
        using namespace test::jtx;
        Env env(*this);
        std::shared_ptr<HostFunctions> hfs(new TestHostFunctions(env, 0));

        // return int64.
        {  // (module
            //   (memory (export "memory") 1)
            //   (func (export "finish") (result i64)
            //     i64.const 0x100000000))
            auto const wasmHex =
                "0061736d010000000105016000017e030201000503010001"
                "071302066d656d6f727902000666696e69736800000a0a01"
                "08004280808080100b";
            auto const wasm = hexToBytes(wasmHex);
            auto const re = runEscrowWasm(wasm, hfs, ESCROW_FUNCTION_NAME, {}, 100'000);
            BEAST_EXPECT(!re);
        }

        // return void. wasmi return execution error
        {  //(module
           //  (type (;0;) (func))
           //  (func (;0;) (type 0)
           //   return)
           //  (memory (;0;) 1)
           //  (export "memory" (memory 0))
           //  (export "finish" (func 0)))
            auto const wasmHex =
                "0061736d01000000010401600000030201000503010001071302066d656d6f"
                "727902000666696e69736800000a050103000f0b";
            auto const wasm = hexToBytes(wasmHex);
            auto const re = runEscrowWasm(wasm, hfs, ESCROW_FUNCTION_NAME, {}, 100'000);
            BEAST_EXPECT(!re);
        }

        // return i32, i32. wasmi doesn't create module
        {  //(module
           //  (memory (export "memory") 1)
           //  (func (export "finish") (result i32 i32)
           //   i32.const 0x10000000
           //   i32.const 0x100000FF))
            auto const wasmHex =
                "0061736d010000000106016000027f7f030201000503010001071302066d65"
                "6d6f727902000666696e69736800000a10010e0041808080800141ff818080"
                "010b";
            auto const wasm = hexToBytes(wasmHex);
            auto const re = runEscrowWasm(wasm, hfs, ESCROW_FUNCTION_NAME, {}, 100'000);
            BEAST_EXPECT(!re);
        }
    }

    void
    testSwapBytes()
    {
        testcase("Wasm swap bytes");

        uint64_t const SWAP_DATAU64 = 0x123456789abcdeffull;
        uint64_t const REVERSE_SWAP_DATAU64 = 0xffdebc9a78563412ull;
        int64_t const SWAP_DATAI64 = 0x123456789abcdeffll;
        int64_t const REVERSE_SWAP_DATAI64 = 0xffdebc9a78563412ll;

        uint32_t const SWAP_DATAU32 = 0x12789aff;
        uint32_t const REVERSE_SWAP_DATAU32 = 0xff9a7812;
        int32_t const SWAP_DATAI32 = 0x12789aff;
        int32_t const REVERSE_SWAP_DATAI32 = 0xff9a7812;

        uint16_t const SWAP_DATAU16 = 0x12ff;
        uint16_t const REVERSE_SWAP_DATAU16 = 0xff12;
        int16_t const SWAP_DATAI16 = 0x12ff;
        int16_t const REVERSE_SWAP_DATAI16 = 0xff12;

        uint64_t b1 = SWAP_DATAU64;
        int64_t b2 = SWAP_DATAI64;
        b1 = adjustWasmEndianessHlp(b1);
        b2 = adjustWasmEndianessHlp(b2);
        BEAST_EXPECT(b1 == REVERSE_SWAP_DATAU64);
        BEAST_EXPECT(b2 == REVERSE_SWAP_DATAI64);
        b1 = adjustWasmEndianessHlp(b1);
        b2 = adjustWasmEndianessHlp(b2);
        BEAST_EXPECT(b1 == SWAP_DATAU64);
        BEAST_EXPECT(b2 == SWAP_DATAI64);

        uint32_t b3 = SWAP_DATAU32;
        int32_t b4 = SWAP_DATAI32;
        b3 = adjustWasmEndianessHlp(b3);
        b4 = adjustWasmEndianessHlp(b4);
        BEAST_EXPECT(b3 == REVERSE_SWAP_DATAU32);
        BEAST_EXPECT(b4 == REVERSE_SWAP_DATAI32);
        b3 = adjustWasmEndianessHlp(b3);
        b4 = adjustWasmEndianessHlp(b4);
        BEAST_EXPECT(b3 == SWAP_DATAU32);
        BEAST_EXPECT(b4 == SWAP_DATAI32);

        uint16_t b5 = SWAP_DATAU16;
        int16_t b6 = SWAP_DATAI16;
        b5 = adjustWasmEndianessHlp(b5);
        b6 = adjustWasmEndianessHlp(b6);
        BEAST_EXPECT(b5 == REVERSE_SWAP_DATAU16);
        BEAST_EXPECT(b6 == REVERSE_SWAP_DATAI16);
        b5 = adjustWasmEndianessHlp(b5);
        b6 = adjustWasmEndianessHlp(b6);
        BEAST_EXPECT(b5 == SWAP_DATAU16);
        BEAST_EXPECT(b6 == SWAP_DATAI16);
    }

    void
    run() override
    {
        using namespace test::jtx;

        testGetDataHelperFunctions();
        testWasmLib();
        testBadWasm();
        testWasmLedgerSqn();

        testWasmFib();
        testWasmSha();
        testWasmB58();

        testHFCost();
        testEscrowWasmDN();
        testFloat();

        testCodecovWasm();
        testDisabledFloat();

        testWasmMemory();
        testWasmTable();
        testWasmProposal();
        testWasmTrap();
        testWasmWasi();
        testWasmSectionCorruption();

        testStartFunctionLoop();
        testBadAlloc();
        testBadAlign();
        testReturnType();
        testSwapBytes();

        // perfTest();
    }
};

BEAST_DEFINE_TESTSUITE(Wasm, app, xrpl);

}  // namespace test
}  // namespace xrpl
