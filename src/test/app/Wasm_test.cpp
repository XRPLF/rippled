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

#include <xrpld/app/misc/WasmVM.h>

#include <iwasm/wasm_c_api.h>

namespace ripple {
namespace test {

using get_ledger_sqn_proto = std::int32_t();
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

static wasm_trap_t*
get_ledger_sqn_wrap(void* env, wasm_val_vec_t const*, wasm_val_vec_t* results)
{
    auto sqn = reinterpret_cast<TestLedgerDataProvider*>(env)->get_ledger_sqn();
    if (results->size)
    {
        results->data[0] = WASM_I32_VAL(sqn);
        if (!results->num_elems)
            results->num_elems = 1;
    }

    return nullptr;
}

struct Wasm_test : public beast::unit_test::suite
{
    void
    testWasmFib()
    {
        testcase("Wasm fibo");

        auto const ws = boost::algorithm::unhex(fib32Hex);
        wbytes const wasm(ws.begin(), ws.end());
        auto& engine = WasmEngine::instance();

        auto const r = engine.run(wasm, "fib", {}, wasmParams(10));

        BEAST_EXPECT(r.has_value() && (r.value() == 55));
    }

    void
    testWasmSha()
    {
        testcase("Wasm sha");

        auto const ws = boost::algorithm::unhex(sha512PureHex);
        wbytes const wasm(ws.begin(), ws.end());
        auto& engine = WasmEngine::instance();

        auto const r =
            engine.run(wasm, "sha512_process", {}, wasmParams(sha512PureHex));

        BEAST_EXPECT(r.has_value() && (r.value() == 34432));
    }

    void
    testWasmB58()
    {
        testcase("Wasm base58");
        auto const ws = boost::algorithm::unhex(b58Hex);
        wbytes const wasm(ws.begin(), ws.end());
        auto& engine = WasmEngine::instance();

        wbytes outb;
        outb.resize(1024);

        auto const r = engine.run(
            wasm,
            "b58enco",
            {},
            wasmParams(
                outb,
                std::string_view(
                    b58Hex.c_str(), std::min(512ul, b58Hex.size()))));

        BEAST_EXPECT(r.has_value() && r.value());
    }

    void
    testWasmSP1Verifier()
    {
        testcase("Wasm sp1 zkproof verifier");
        auto const ws = boost::algorithm::unhex(sp1_wasm);
        wbytes const wasm(ws.begin(), ws.end());
        auto& engine = WasmEngine::instance();

        auto const r = engine.run(wasm, "sp1_groth16_verifier");

        BEAST_EXPECT(r.has_value() && r.value());
    }

    void
    testWasmBG16Verifier()
    {
        testcase("Wasm BG16 zkproof verifier");
        auto const ws = boost::algorithm::unhex(zkProofHex);
        wbytes const wasm(ws.begin(), ws.end());
        auto& engine = WasmEngine::instance();

        auto const r = engine.run(wasm, "bellman_groth16_test");

        BEAST_EXPECT(r.has_value() && r.value());
    }

    void
    testWasmLedgerSqn()
    {
        testcase("Wasm get ledger sequence");

        auto wasmStr = boost::algorithm::unhex(ledgerSqnHex);
        wbytes wasm(wasmStr.begin(), wasmStr.end());

        using namespace test::jtx;

        Env env{*this};
        TestLedgerDataProvider ledgerDataProvider(&env);
        std::string const funcName("ready");

        std::vector<WasmImportFunc> imports;
        WASM_IMPORT_FUNC(imports, get_ledger_sqn, &ledgerDataProvider);

        auto& engine = WasmEngine::instance();

        auto r = engine.run(wasm, funcName, imports);
        if (BEAST_EXPECT(r.has_value()))
            BEAST_EXPECT(!r.value());
        env.close();
        env.close();
        env.close();
        env.close();

        r = engine.run({}, funcName, imports);
        if (BEAST_EXPECT(r.has_value()))
            BEAST_EXPECT(r.value());
    }

    void
    testWasmCheckJson()
    {
        testcase("Wasm check json");

        auto const wasmStr = boost::algorithm::unhex(checkJsonHex);
        wbytes const wasm(wasmStr.begin(), wasmStr.end());
        std::string const funcName("check_accountID");
        {
            std::string str = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
            wbytes data(str.begin(), str.end());
            auto re = runEscrowWasm(wasm, funcName, {}, -1, wasmParams(data));
            if (BEAST_EXPECT(re.has_value()))
                BEAST_EXPECT(re.value().result);
        }
        {
            std::string str = "rHb9CJAWyB4rj91VRWn96DkukG4bwdty00";
            wbytes data(str.begin(), str.end());
            auto re = runEscrowWasm(wasm, funcName, {}, -1, wasmParams(data));
            if (BEAST_EXPECT(re.has_value()))
                BEAST_EXPECT(!re.value().result);
        }
    }

    void
    testWasmCompareJson()
    {
        testcase("Wasm compare json");

        auto wasmStr = boost::algorithm::unhex(compareJsonHex);
        std::vector<uint8_t> wasm(wasmStr.begin(), wasmStr.end());
        std::string funcName("compare_accountID");

        std::vector<uint8_t> const tx_data(tx_js.begin(), tx_js.end());
        std::vector<uint8_t> const lo_data(lo_js.begin(), lo_js.end());
        auto re =
            runEscrowWasm(wasm, funcName, {}, -1, wasmParams(tx_data, lo_data));
        if (BEAST_EXPECT(re.has_value()))
            BEAST_EXPECT(re.value().result);

        std::vector<uint8_t> const lo_data2(lo_js2.begin(), lo_js2.end());
        re = runEscrowWasm(
            wasm, funcName, {}, -1, wasmParams(tx_data, lo_data2));
        if (BEAST_EXPECT(re.has_value()))
            BEAST_EXPECT(!re.value().result);
    }

    void
    testWasmLib()
    {
        testcase("wasmtime lib test");
        // clang-format off
        /* The WASM module buffer. */
        wbytes const wasm = {/* WASM header */
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

        auto res = vm.run(wasm, "addTwo", imports, wasmParams(1234, 5678));

        // if (res) printf("invokeAdd get the result: %d\n", res.value());

        BEAST_EXPECT(res.has_value() && res.value() == 6912);
    }

    void
    testBadWasm()
    {
        testcase("bad wasm test");

        HostFunctions hfs;
        auto wasmHex = "00000000";
        auto wasmStr = boost::algorithm::unhex(std::string(wasmHex));
        std::vector<uint8_t> wasm(wasmStr.begin(), wasmStr.end());
        std::string funcName("mock_escrow");
        auto re = runEscrowWasm(wasm, funcName, &hfs, 15);
        BEAST_EXPECT(re.error());
    }

    void
    testEscrowWasmDN1()
    {
        testcase("escrow wasm devnet 1 test");
        auto wasmHex = allHostFunctionsHex;

        auto wasmStr = boost::algorithm::unhex(std::string(wasmHex));
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
        struct TestHostFunctions : public HostFunctions
        {
            Env* env_;
            Bytes accountID_;
            Bytes data_;
            int clock_drift_ = 0;
            test::StreamSink sink_;
            beast::Journal jlog_;

        public:
            explicit TestHostFunctions(Env* env, int cd = 0)
                : env_(env)
                , clock_drift_(cd)
                , sink_(beast::severities::kTrace)
                , jlog_(sink_)
            {
                std::string s = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
                accountID_ = Bytes{s.begin(), s.end()};
                std::string t = "10000";
                data_ = Bytes{t.begin(), t.end()};
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
                return env_->current()
                           ->parentCloseTime()
                           .time_since_epoch()
                           .count() +
                    clock_drift_;
            }

            std::optional<Bytes>
            getTxField(std::string const& fname) override
            {
                return accountID_;
            }

            std::optional<Bytes>
            getLedgerEntryField(
                int32_t type,
                Bytes const& kdata,
                std::string const& fname) override
            {
                return data_;
            }

            std::optional<Bytes>
            getCurrentLedgerEntryField(std::string const& fname) override
            {
                if (fname == "Destination" || fname == "Account")
                    return accountID_;
                else if (fname == "Data")
                    return data_;
                else if (fname == "FinishAfter")
                {
                    auto t = env_->current()
                                 ->parentCloseTime()
                                 .time_since_epoch()
                                 .count();
                    std::string s = std::to_string(t);
                    return Bytes{s.begin(), s.end()};
                }

                return std::nullopt;
            }
        };

        Env env{*this};

        {
            TestHostFunctions nfs(&env, 0);
            std::string funcName("finish");
            auto re = runEscrowWasm(wasm, funcName, &nfs, 100000);
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
            TestHostFunctions nfs(&env, -1);
            std::string funcName("finish");
            auto re = runEscrowWasm(wasm, funcName, &nfs, 100000);
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
                explicit BadTestHostFunctions(Env* env) : TestHostFunctions(env)
                {
                }
                std::optional<Bytes>
                getTxField(std::string const& fname) override
                {
                    return std::nullopt;
                }
            };
            BadTestHostFunctions nfs(&env);
            std::string funcName("finish");
            auto re = runEscrowWasm(wasm, funcName, &nfs, 100000);
            BEAST_EXPECT(re.error());
            // std::cout << "bad case (access nonexistent field) result "
            //           << re.error() << std::endl;
        }

        {  // fail because trying to allocate more than MAX_PAGES memory
            struct BadTestHostFunctions : public TestHostFunctions
            {
                explicit BadTestHostFunctions(Env* env) : TestHostFunctions(env)
                {
                }
                std::optional<Bytes>
                getTxField(std::string const& fname) override
                {
                    return Bytes((MAX_PAGES + 1) * 64 * 1024, 1);
                }
            };
            BadTestHostFunctions nfs(&env);
            std::string funcName("finish");
            auto re = runEscrowWasm(wasm, funcName, &nfs, 100000);
            BEAST_EXPECT(!re);
            // std::cout << "bad case (more than MAX_PAGES) result "
            //           << re.error() << std::endl;
        }

        {  // fail because recursion too deep
            auto wasmHex = deepRecursionHex;
            auto wasmStr = boost::algorithm::unhex(std::string(wasmHex));
            std::vector<uint8_t> wasm(wasmStr.begin(), wasmStr.end());

            TestHostFunctions nfs(&env);
            std::string funcName("recursive");
            auto re = runEscrowWasm(wasm, funcName, &nfs, 1000'000'000);
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
                    "WAMR trap: Exception: wasm operand stack overflow") == 1);
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
        testEscrowWasmDN1();

        testWasmFib();
        testWasmSha();
        testWasmB58();
        // testWasmSP1Verifier();
        // testWasmBG16Verifier();
    }
};

static inline uint64_t
usecs()
{
    uint64_t x =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch())
            .count();
    return x;
}

BEAST_DEFINE_TESTSUITE(Wasm, app, ripple);

}  // namespace test
}  // namespace ripple
