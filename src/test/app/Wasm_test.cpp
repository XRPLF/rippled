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

#include <test/jtx.h>

#include <xrpld/app/misc/WasmVM.h>

#include <iwasm/wasm_c_api.h>

#include <filesystem>

namespace ripple {
namespace test {

static std::string const
getWasmFixture(std::string const& fixtureName)
{
    namespace fs = std::filesystem;
    auto const currentFile = fs::path(__FILE__);
    auto const filename =
        fs::weakly_canonical(currentFile / "../wasm_fixtures" / fixtureName)
            .string();
    std::ifstream file(filename);  // Open the file
    if (!file.is_open())
    {
        throw std::runtime_error("Unable to open file");
    }
    std::stringstream buffer;
    buffer << file.rdbuf();  // Read the entire file content into the buffer
    std::string bufferStr = buffer.str();

    // strip all white spaces
    std::erase(bufferStr, ' ');
    std::erase(bufferStr, '\n');
    file.close();  // Close the file
    return bufferStr;
}

/* Host function body definition. */
using Add_proto = int32_t(int32_t, int32_t);
wasm_trap_t*
Add(void* env, const wasm_val_vec_t* params, wasm_val_vec_t* results)
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
    testWasmtimeLib()
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
        auto wasmHex = getWasmFixture("all_host_functions.hex");

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
            Env* env;
            Bytes accountID;
            Bytes data;
            int clock_drift = 0;

        public:
            explicit TestHostFunctions(Env* env, int cd = 0)
                : env(env), clock_drift(cd)
            {
                std::string s = "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh";
                accountID = Bytes{s.begin(), s.end()};
                std::string t = "10000";
                data = Bytes{t.begin(), t.end()};
            }

            int32_t
            getLedgerSqn() override
            {
                return (int32_t)env->current()->seq();
            }

            int32_t
            getParentLedgerTime() override
            {
                return env->current()
                           ->parentCloseTime()
                           .time_since_epoch()
                           .count() +
                    clock_drift;
            }

            std::optional<Bytes>
            getTxField(std::string const& fname) override
            {
                return accountID;
            }

            std::optional<Bytes>
            getLedgerEntryField(
                int32_t type,
                Bytes const& kdata,
                std::string const& fname) override
            {
                return data;
            }

            std::optional<Bytes>
            getCurrentLedgerEntryField(std::string const& fname) override
            {
                if (fname == "Destination" || fname == "Account")
                    return accountID;
                else if (fname == "Data")
                    return data;
                else if (fname == "FinishAfter")
                {
                    auto t = env->current()
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
            TestHostFunctions nfs(&env);
            std::string funcName("finish");
            auto re = runEscrowWasm(wasm, funcName, &nfs, 100000);
            if (BEAST_EXPECT(re.has_value()))
            {
                BEAST_EXPECT(re.value().result);
                std::cout << "good case result " << re.value().result
                          << " cost: " << re.value().cost << std::endl;
            }
        }

        env.close();
        env.close();
        env.close();
        env.close();

        {  // fail because current time < escrow_finish_after time
            TestHostFunctions nfs(&env);
            nfs.clock_drift = -1;
            std::string funcName("finish");
            auto re = runEscrowWasm(wasm, funcName, &nfs, 100000);
            if (BEAST_EXPECT(re.has_value()))
            {
                BEAST_EXPECT(!re.value().result);
                std::cout << "bad case (current time < escrow_finish_after "
                             "time) result "
                          << re.value().result << " cost: " << re.value().cost
                          << std::endl;
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
            std::cout << "bad case (access nonexistent field) result "
                      << re.error() << std::endl;
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
            if (BEAST_EXPECT(!re))
                std::cout << "bad case (more than MAX_PAGES) result "
                          << re.error() << std::endl;
        }

        {  // fail because recursion too deep
            auto wasmHex = getWasmFixture("deep_recursion.hex");
            auto wasmStr = boost::algorithm::unhex(std::string(wasmHex));
            std::vector<uint8_t> wasm(wasmStr.begin(), wasmStr.end());

            TestHostFunctions nfs(&env);
            std::string funcName("recursive");
            auto re = runEscrowWasm(wasm, funcName, &nfs, 1000'000'000);
            if (BEAST_EXPECT(re.error()))
                std::cout << "bad case (deep recursion) result " << re.error()
                          << std::endl;
        }
    }

    void
    run() override
    {
        using namespace test::jtx;
        testWasmtimeLib();
        testBadWasm();
        testEscrowWasmDN1();
    }
};

BEAST_DEFINE_TESTSUITE(Wasm, app, ripple);

}  // namespace test
}  // namespace ripple
