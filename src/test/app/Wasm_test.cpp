//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <test/app/wasm_fixtures/fixtures.h>
#include <test/jtx.h>

#include <xrpld/app/misc/WasmHostFunc.h>
#include <xrpld/app/misc/WasmVM.h>

#include <wasm_c_api.h>

#ifdef _DEBUG
// #define DEBUG_OUTPUT 1
#endif

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

struct TestLedgerDataProvider
{
    jtx::Env* env;

public:
    TestLedgerDataProvider(jtx::Env* env_) : env(env_)
    {
    }

    int32_t
    get_ledger_sqn()
    {
        return (int32_t)env->current()->seq();
    }
};

using getLedgerSqn_proto = std::int32_t();
static wasm_trap_t*
getLedgerSqn_wrap(void* env, wasm_val_vec_t const*, wasm_val_vec_t* results)
{
    auto sqn = reinterpret_cast<TestLedgerDataProvider*>(env)->get_ledger_sqn();

    results->data[0] = WASM_I32_VAL(sqn);
    results->num_elems = 1;

    return nullptr;
}

struct TestHostFunctionsOld : public HostFunctions
{
    test::jtx::Env* env_;
    Bytes accountID_;
    Bytes data_;
    int clock_drift_ = 0;
    test::StreamSink sink_;
    beast::Journal jlog_;
    void const* rt_ = nullptr;

public:
    explicit TestHostFunctionsOld(test::jtx::Env* env, int cd = 0)
        : env_(env)
        , clock_drift_(cd)
        , sink_(beast::severities::kDebug)
        , jlog_(sink_)
    {
        std::string s = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
        accountID_ = Bytes{s.begin(), s.end()};
        std::string t = "10000";
        data_ = Bytes{t.begin(), t.end()};
    }

    virtual void
    setRT(void const* rt) override
    {
        rt_ = rt;
    }

    virtual void const*
    getRT() const override
    {
        return rt_;
    }

    test::StreamSink&
    getSink()
    {
        return sink_;
    }

    beast::Journal
    getJournal() override
    {
        return jlog_;
    }

    int32_t
    getLedgerSqn() override
    {
        return (int32_t)env_->current()->seq();
    }

    int32_t
    getParentLedgerTime() override
    {
        return env_->current()->parentCloseTime().time_since_epoch().count() +
            clock_drift_;
    }

    Expected<Bytes, int32_t>
    getTxField(SField const& fname) override
    {
        return accountID_;
    }

    Expected<Bytes, int32_t>
    getLedgerObjField(int32_t cacheIdx, SField const& fname) override
    {
        return data_;
    }

    Expected<Bytes, int32_t>
    getCurrentLedgerObjField(SField const& fname) override
    {
        if (fname.getName() == "Destination" || fname.getName() == "Account")
            return accountID_;
        else if (fname.getName() == "Data")
            return data_;
        else if (fname.getName() == "FinishAfter")
        {
            auto t =
                env_->current()->parentCloseTime().time_since_epoch().count();
            std::string s = std::to_string(t);
            return Bytes{s.begin(), s.end()};
        }

        return Unexpected(-1);
    }
};

struct TestHostFunctions : public HostFunctions
{
    test::jtx::Env& env_;
    AccountID accountID_;
    Bytes data_;
    int clock_drift_ = 0;
    void const* rt_ = nullptr;

public:
    explicit TestHostFunctions(test::jtx::Env& env, int cd = 0)
        : env_(env), clock_drift_(cd)
    {
        auto opt = parseBase58<AccountID>("rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh");
        if (opt)
            accountID_ = *opt;
        std::string t = "10000";
        data_ = Bytes{t.begin(), t.end()};
    }

    virtual void
    setRT(void const* rt) override
    {
        rt_ = rt;
    }

    virtual void const*
    getRT() const override
    {
        return rt_;
    }

    beast::Journal
    getJournal() override
    {
        return env_.journal;
    }

    int32_t
    getLedgerSqn() override
    {
        return static_cast<int32_t>(env_.current()->seq());
    }

    int32_t
    getParentLedgerTime() override
    {
        return env_.current()->parentCloseTime().time_since_epoch().count() +
            clock_drift_;
    }

    virtual int32_t
    cacheLedgerObj(Keylet const& keylet, int32_t cacheIdx) override
    {
        return 1;
    }

    Expected<Bytes, int32_t>
    getTxField(SField const& fname) override
    {
        if (fname == sfAccount)
            return Bytes(accountID_.begin(), accountID_.end());
        else if (fname == sfFee)
        {
            int64_t x = 235;
            uint8_t const* p = reinterpret_cast<uint8_t const*>(&x);
            return Bytes{p, p + sizeof(x)};
        }
        else if (fname == sfSequence)
        {
            int32_t x = getLedgerSqn();
            uint8_t const* p = reinterpret_cast<uint8_t const*>(&x);
            return Bytes{p, p + sizeof(x)};
        }
        return Bytes();
    }

    Expected<Bytes, int32_t>
    getTxNestedField(Slice const& locator) override
    {
        uint8_t const a[] = {0x2b, 0x6a, 0x23, 0x2a, 0xa4, 0xc4, 0xbe, 0x41,
                             0xbf, 0x49, 0xd2, 0x45, 0x9f, 0xa4, 0xa0, 0x34,
                             0x7e, 0x1b, 0x54, 0x3a, 0x4c, 0x92, 0xfc, 0xee,
                             0x08, 0x21, 0xc0, 0x20, 0x1e, 0x2e, 0x9a, 0x00};
        return Bytes(&a[0], &a[sizeof(a)]);
    }

    Expected<Bytes, int32_t>
    getLedgerObjField(int32_t cacheIdx, SField const& fname) override
    {
        // auto const& sn = fname.getName();
        if (fname == sfBalance)
        {
            int64_t x = 10'000;
            uint8_t const* p = reinterpret_cast<uint8_t const*>(&x);
            return Bytes{p, p + sizeof(x)};
        }
        return data_;
    }

    Expected<Bytes, int32_t>
    getCurrentLedgerObjField(SField const& fname) override
    {
        auto const& sn = fname.getName();
        if (sn == "Destination" || sn == "Account")
            return Bytes(accountID_.begin(), accountID_.end());
        else if (sn == "Data")
            return data_;
        else if (sn == "FinishAfter")
        {
            auto t =
                env_.current()->parentCloseTime().time_since_epoch().count();
            std::string s = std::to_string(t);
            return Bytes{s.begin(), s.end()};
        }

        return Unexpected(-1);
    }

    int32_t
    getTxArrayLen(SField const& fname) override
    {
        return 32;
    }

    int32_t
    getTxNestedArrayLen(Slice const& locator) override
    {
        return 32;
    }

    int32_t
    updateData(Bytes const& data) override
    {
        return 0;
    }

    Expected<Bytes, int32_t>
    accountKeylet(AccountID const& account) override
    {
        if (!account)
            return Unexpected(HF_ERR_INVALID_ACCOUNT);
        auto const keylet = keylet::account(account);
        return Bytes{keylet.key.begin(), keylet.key.end()};
    }

    int32_t
    trace(std::string const& msg, Bytes const& data, bool asHex) override
    {
#ifdef DEBUG_OUTPUT
        auto& j = std::cerr;
#else
        auto j = getJournal().trace();
#endif
        j << msg;
        if (!asHex)
            j << std::string_view(
                reinterpret_cast<char const*>(data.data()), data.size());
        else
        {
            auto const hex =
                boost::algorithm::hex(std::string(data.begin(), data.end()));
            j << hex;
        }

#ifdef DEBUG_OUTPUT
        j << std::endl;
#endif

        return msg.size() + data.size() * (asHex ? 2 : 1);
    }

    int32_t
    traceNum(std::string const& msg, int64_t data) override
    {
#ifdef DEBUG_OUTPUT
        auto& j = std::cerr;
#else
        auto j = getJournal().trace();
#endif
        j << msg << data;

#ifdef DEBUG_OUTPUT
        j << std::endl;
#endif
        return msg.size() + sizeof(data);
    }
};

struct Wasm_test : public beast::unit_test::suite
{
    void
    testWasmFib()
    {
        testcase("Wasm fibo");

        auto const ws = boost::algorithm::unhex(fib32Hex);
        Bytes const wasm(ws.begin(), ws.end());
        auto& engine = WasmEngine::instance();

        auto const r = engine.run(wasm, "fib", wasmParams(10));

        BEAST_EXPECT(r.has_value() && (r->result == 55));
    }

    void
    testWasmSha()
    {
        testcase("Wasm sha");

        auto const ws = boost::algorithm::unhex(sha512PureHex);
        Bytes const wasm(ws.begin(), ws.end());
        auto& engine = WasmEngine::instance();

        auto const r =
            engine.run(wasm, "sha512_process", wasmParams(sha512PureHex));

        BEAST_EXPECT(r.has_value() && (r->result == 34432));
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
        auto const r = engine.run(wasm, "b58enco", wasmParams(outb, s));

        BEAST_EXPECT(r.has_value() && r->result);
    }

    void
    testWasmSP1Verifier()
    {
        testcase("Wasm sp1 zkproof verifier");
        auto const ws = boost::algorithm::unhex(sp1_wasm);
        Bytes const wasm(ws.begin(), ws.end());
        auto& engine = WasmEngine::instance();

        auto const r = engine.run(wasm, "sp1_groth16_verifier");

        BEAST_EXPECT(r.has_value() && r->result);
    }

    void
    testWasmBG16Verifier()
    {
        testcase("Wasm BG16 zkproof verifier");
        auto const ws = boost::algorithm::unhex(zkProofHex);
        Bytes const wasm(ws.begin(), ws.end());
        auto& engine = WasmEngine::instance();

        auto const r = engine.run(wasm, "bellman_groth16_test");

        BEAST_EXPECT(r.has_value() && r->result);
    }

    void
    testWasmLedgerSqn()
    {
        testcase("Wasm get ledger sequence");

        auto wasmStr = boost::algorithm::unhex(ledgerSqnHex);
        Bytes wasm(wasmStr.begin(), wasmStr.end());

        using namespace test::jtx;

        Env env{*this};
        TestLedgerDataProvider ledgerDataProvider(&env);
        std::string const funcName("finish");

        std::vector<WasmImportFunc> imports;
        WASM_IMPORT_FUNC(imports, getLedgerSqn, &ledgerDataProvider);

        auto& engine = WasmEngine::instance();

        auto r = engine.run(
            wasm, funcName, {}, imports, nullptr, 1'000'000, env.journal);
        if (BEAST_EXPECT(r.has_value()))
            BEAST_EXPECT(!r->result);
        env.close();
        env.close();
        env.close();
        env.close();

        // empty module - run the same module
        r = engine.run(
            {}, funcName, {}, imports, nullptr, 1'000'000, env.journal);
        if (BEAST_EXPECT(r.has_value()))
            BEAST_EXPECT(r->result);
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
                BEAST_EXPECT(re.value().result);
        }
        {
            std::string str = "rHb9CJAWyB4rj91VRWn96DkukG4bwdty00";
            Bytes data(str.begin(), str.end());
            auto re = runEscrowWasm(
                wasm, funcName, wasmParams(data), nullptr, -1, env.journal);
            if (BEAST_EXPECT(re.has_value()))
                BEAST_EXPECT(!re.value().result);
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
            BEAST_EXPECT(re.value().result);

        std::vector<uint8_t> const lo_data2(lo_js2.begin(), lo_js2.end());
        re = runEscrowWasm(
            wasm,
            funcName,
            wasmParams(tx_data, lo_data2),
            nullptr,
            -1,
            env.journal);
        if (BEAST_EXPECT(re.has_value()))
            BEAST_EXPECT(!re.value().result);
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

        auto res = vm.run(wasm, "addTwo", wasmParams(1234, 5678), imports);

        // if (res) printf("invokeAdd get the result: %d\n", res.value());

        BEAST_EXPECT(res.has_value() && res->result == 6912);
    }

    void
    testBadWasm()
    {
        testcase("bad wasm test");

        using namespace test::jtx;
        Env env{*this};

        HostFunctions hfs;
        auto wasmHex = "00000000";
        auto wasmStr = boost::algorithm::unhex(std::string(wasmHex));
        std::vector<uint8_t> wasm(wasmStr.begin(), wasmStr.end());
        std::string funcName("mock_escrow");
        auto re = runEscrowWasm(wasm, funcName, {}, &hfs, 15, env.journal);
        BEAST_EXPECT(re.error());
    }

    void
    testEscrowWasmDN1()
    {
        testcase("escrow wasm devnet 1 test");
        std::string const wasmHex = allHostFunctionsHex;

        std::string const wasmStr = boost::algorithm::unhex(wasmHex);
        std::vector<uint8_t> wasm(wasmStr.begin(), wasmStr.end());

        //        let sender = get_tx_account_id();
        //        let owner = get_current_escrow_account_id();
        //        let dest = get_current_escrow_destination();
        //        let dest_balance = get_account_balance(dest);
        //        let escrow_data = get_current_escrow_data();
        //        let ed_str = String::from_utf8(escrow_data).unwrap();
        //        let threshold_balance = ed_str.parse::<u64>().unwrap();
        //        let pl_time = host_lib::getParentLedgerTime();
        //        let e_time = get_current_escrow_finish_after();
        //        sender == owner && dest_balance <= threshold_balance &&
        //        pl_time >= e_time

        using namespace test::jtx;
        Env env{*this};
        {
            TestHostFunctions nfs(env, 0);
            std::string funcName("finish");
            auto re = runEscrowWasm(wasm, funcName, {}, &nfs, 100000);
            if (BEAST_EXPECT(re.has_value()))
            {
                BEAST_EXPECT(re.value().result);
                // std::cout << "good case result " << re.value().result
                //           << " cost: " << re.value().cost << std::endl;
            }
        }

        env.close();
        env.close();
        env.close();
        env.close();

        {  // fail because current time < escrow_finish_after time
            TestHostFunctions nfs(env, -1);
            std::string funcName("finish");
            auto re = runEscrowWasm(wasm, funcName, {}, &nfs, 100000);
            if (BEAST_EXPECT(re.has_value()))
            {
                BEAST_EXPECT(!re.value().result);
                // std::cout << "bad case (current time < escrow_finish_after "
                //              "time) result "
                //           << re.value().result << " cost: " <<
                //           re.value().cost
                //           << std::endl;
            }
        }

        {  // fail because trying to access nonexistent field
            struct BadTestHostFunctions : public TestHostFunctions
            {
                explicit BadTestHostFunctions(Env& env) : TestHostFunctions(env)
                {
                }
                Expected<Bytes, int32_t>
                getTxField(SField const& fname) override
                {
                    return Unexpected(-1);
                }
            };
            BadTestHostFunctions nfs(env);
            std::string funcName("finish");
            auto re = runEscrowWasm(wasm, funcName, {}, &nfs, 100000);
            BEAST_EXPECT(re.error());
            // std::cout << "bad case (access nonexistent field) result "
            //           << re.error() << std::endl;
        }

        {  // fail because trying to allocate more than MAX_PAGES memory
            struct BadTestHostFunctions : public TestHostFunctions
            {
                explicit BadTestHostFunctions(Env& env) : TestHostFunctions(env)
                {
                }
                Expected<Bytes, int32_t>
                getTxField(SField const& fname) override
                {
                    return Bytes((MAX_PAGES + 1) * 64 * 1024, 1);
                }
            };
            BadTestHostFunctions nfs(env);
            std::string funcName("finish");
            auto re = runEscrowWasm(wasm, funcName, {}, &nfs, 100000);
            BEAST_EXPECT(!re);
            // std::cout << "bad case (more than MAX_PAGES) result "
            //           << re.error() << std::endl;
        }

        {  // fail because recursion too deep
            auto wasmHex = deepRecursionHex;
            auto wasmStr = boost::algorithm::unhex(std::string(wasmHex));
            std::vector<uint8_t> wasm(wasmStr.begin(), wasmStr.end());

            TestHostFunctionsOld nfs(&env);
            std::string funcName("recursive");
            auto re = runEscrowWasm(wasm, funcName, {}, &nfs, 1000'000'000);
            BEAST_EXPECT(re.error());
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

            BEAST_EXPECT(
                countSubstr(
                    sink.messages().str(), "WAMR error: failed to call func") ==
                1);
            BEAST_EXPECT(
                countSubstr(
                    sink.messages().str(),
                    "WAMR Exception: wasm operand stack overflow") == 1);
        }
    }

    void
    testEscrowWasmDN2()
    {
        testcase("wasm devnet 3 test");

        std::string const funcName("finish");

        using namespace test::jtx;

        Env env(*this);
        {
            std::string const wasmHex = xrplStdExampleHex;
            std::string const wasmStr = boost::algorithm::unhex(wasmHex);
            std::vector<uint8_t> const wasm(wasmStr.begin(), wasmStr.end());

            TestHostFunctions nfs(env, 0);

            auto re = runEscrowWasm(wasm, funcName, {}, &nfs, 100'000);
            if (BEAST_EXPECT(re.has_value()))
            {
                BEAST_EXPECT(re->result);
                // std::cout << "good case result " << re.value().result
                //           << " cost: " << re.value().cost << std::endl;
            }
        }

        env.close();
        env.close();
        env.close();
        env.close();
        env.close();

        {
            std::string const wasmHex = hostFunctions2Hex;
            std::string const wasmStr = boost::algorithm::unhex(wasmHex);
            std::vector<uint8_t> const wasm(wasmStr.begin(), wasmStr.end());

            TestHostFunctions nfs(env, 0);

            auto re = runEscrowWasm(wasm, funcName, {}, &nfs, 100'000);
            if (BEAST_EXPECT(re.has_value()))
            {
                BEAST_EXPECT(re->result);
                // std::cout << "good case result " << re.value().result
                //           << " cost: " << re.value().cost << std::endl;
            }
        }
    }

    void
    run() override
    {
        using namespace test::jtx;

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
        // testWasmBG16Verifier();

        // TODO: needs fix for new host functions interface
        // testEscrowWasmDN1();
        testEscrowWasmDN2();
    }
};

BEAST_DEFINE_TESTSUITE(Wasm, app, ripple);

}  // namespace test
}  // namespace ripple
