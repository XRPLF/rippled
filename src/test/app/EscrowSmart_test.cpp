#include <test/app/wasm_fixtures/fixtures.h>
#include <test/jtx.h>

#include <xrpld/app/tx/applySteps.h>
#include <xrpld/app/wasm/WasmVM.h>

#include <xrpl/ledger/Dir.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <iterator>

namespace ripple {
namespace test {

struct EscrowSmart_test : public beast::unit_test::suite
{
    void
    testCreateFinishFunctionPreflight(FeatureBitset features)
    {
        testcase("Test preflight checks involving FinishFunction");

        using namespace jtx;
        using namespace std::chrono;

        Account const alice{"alice"};
        Account const carol{"carol"};

        // Tests whether the ledger index is >= 5
        // getLedgerSqn() >= 5}
        static auto wasmHex = ledgerSqnWasmHex;

        {
            // featureSmartEscrow disabled
            Env env(*this, features - featureSmartEscrow);
            env.fund(XRP(5000), alice, carol);
            XRPAmount const txnFees = env.current()->fees().base + 1000;
            auto escrowCreate = escrow::create(alice, carol, XRP(1000));
            env(escrowCreate,
                escrow::finish_function(wasmHex),
                escrow::cancel_time(env.now() + 100s),
                fee(txnFees),
                ter(temDISABLED));
            env.close();

            env(escrowCreate,
                escrow::finish_function(wasmHex),
                escrow::cancel_time(env.now() + 100s),
                escrow::data("00112233"),
                fee(txnFees),
                ter(temDISABLED));
            env.close();
        }

        {
            // FinishFunction > max length
            Env env(
                *this,
                envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->FEES.extension_size_limit = 10;  // 10 bytes
                    return cfg;
                }),
                features);
            XRPAmount const txnFees = env.current()->fees().base + 1000;
            // create escrow
            env.fund(XRP(5000), alice, carol);

            auto escrowCreate = escrow::create(alice, carol, XRP(500));

            // 11-byte string
            std::string longWasmHex = "00112233445566778899AA";
            env(escrowCreate,
                escrow::finish_function(longWasmHex),
                escrow::cancel_time(env.now() + 100s),
                fee(txnFees),
                ter(temMALFORMED));
            env.close();
        }

        {
            // Data without FinishFunction
            Env env(*this, features);
            XRPAmount const txnFees = env.current()->fees().base + 100000;
            // create escrow
            env.fund(XRP(5000), alice, carol);

            auto escrowCreate = escrow::create(alice, carol, XRP(500));

            std::string longData(4, 'A');
            env(escrowCreate,
                escrow::data(longData),
                escrow::finish_time(env.now() + 100s),
                fee(txnFees),
                ter(temMALFORMED));
            env.close();
        }

        {
            // Data > max length
            Env env(*this, features);
            XRPAmount const txnFees = env.current()->fees().base + 100000;
            // create escrow
            env.fund(XRP(5000), alice, carol);

            auto escrowCreate = escrow::create(alice, carol, XRP(500));

            // string of length maxWasmDataLength * 2 + 2
            std::string longData(maxWasmDataLength * 2 + 2, 'B');
            env(escrowCreate,
                escrow::data(longData),
                escrow::finish_function(wasmHex),
                escrow::cancel_time(env.now() + 100s),
                fee(txnFees),
                ter(temMALFORMED));
            env.close();
        }

        Env env(
            *this,
            envconfig([](std::unique_ptr<Config> cfg) {
                cfg->START_UP = Config::FRESH;
                return cfg;
            }),
            features);
        XRPAmount const txnFees =
            env.current()->fees().base * 10 + wasmHex.size() / 2 * 5;
        // create escrow
        env.fund(XRP(5000), alice, carol);

        auto escrowCreate = escrow::create(alice, carol, XRP(500));

        // Success situations
        {
            // FinishFunction + CancelAfter
            env(escrowCreate,
                escrow::finish_function(wasmHex),
                escrow::cancel_time(env.now() + 20s),
                fee(txnFees));
            env.close();
        }
        {
            // FinishFunction + Condition + CancelAfter
            env(escrowCreate,
                escrow::finish_function(wasmHex),
                escrow::cancel_time(env.now() + 30s),
                escrow::condition(escrow::cb1),
                fee(txnFees));
            env.close();
        }
        {
            // FinishFunction + FinishAfter + CancelAfter
            env(escrowCreate,
                escrow::finish_function(wasmHex),
                escrow::cancel_time(env.now() + 40s),
                escrow::finish_time(env.now() + 2s),
                fee(txnFees));
            env.close();
        }
        {
            // FinishFunction + FinishAfter + Condition + CancelAfter
            env(escrowCreate,
                escrow::finish_function(wasmHex),
                escrow::cancel_time(env.now() + 50s),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 2s),
                fee(txnFees));
            env.close();
        }

        // Failure situations (i.e. all other combinations)
        {
            // only FinishFunction
            env(escrowCreate,
                escrow::finish_function(wasmHex),
                fee(txnFees),
                ter(temBAD_EXPIRATION));
            env.close();
        }
        {
            // FinishFunction + FinishAfter
            env(escrowCreate,
                escrow::finish_function(wasmHex),
                escrow::finish_time(env.now() + 2s),
                fee(txnFees),
                ter(temBAD_EXPIRATION));
            env.close();
        }
        {
            // FinishFunction + Condition
            env(escrowCreate,
                escrow::finish_function(wasmHex),
                escrow::condition(escrow::cb1),
                fee(txnFees),
                ter(temBAD_EXPIRATION));
            env.close();
        }
        {
            // FinishFunction + FinishAfter + Condition
            env(escrowCreate,
                escrow::finish_function(wasmHex),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 2s),
                fee(txnFees),
                ter(temBAD_EXPIRATION));
            env.close();
        }
        {
            // FinishFunction 0 length
            env(escrowCreate,
                escrow::finish_function(""),
                escrow::cancel_time(env.now() + 60s),
                fee(txnFees),
                ter(temMALFORMED));
            env.close();
        }
        {
            // Not enough fees
            env(escrowCreate,
                escrow::finish_function(wasmHex),
                escrow::cancel_time(env.now() + 70s),
                fee(txnFees - 1),
                ter(telINSUF_FEE_P));
            env.close();
        }

        {
            // FinishFunction nonexistent host function
            // pub fn finish() -> bool {
            //     unsafe { host_lib::bad() >= 5 }
            // }
            auto const badWasmHex =
                "0061736d010000000105016000017f02100108686f73745f6c696203626164"
                "00000302010005030100100611027f00418080c0000b7f00418080c0000b07"
                "2e04066d656d6f727902000666696e69736800010a5f5f646174615f656e64"
                "03000b5f5f686561705f6261736503010a09010700100041044a0b004d0970"
                "726f64756365727302086c616e6775616765010452757374000c70726f6365"
                "737365642d6279010572757374631d312e38352e3120283465623136313235"
                "3020323032352d30332d31352900490f7461726765745f6665617475726573"
                "042b0f6d757461626c652d676c6f62616c732b087369676e2d6578742b0f72"
                "65666572656e63652d74797065732b0a6d756c746976616c7565";
            env(escrowCreate,
                escrow::finish_function(badWasmHex),
                escrow::cancel_time(env.now() + 100s),
                fee(txnFees),
                ter(temBAD_WASM));
            env.close();
        }
    }

    void
    testFinishWasmFailures(FeatureBitset features)
    {
        testcase("EscrowFinish Smart Escrow failures");

        using namespace jtx;
        using namespace std::chrono;

        Account const alice{"alice"};
        Account const carol{"carol"};

        // Tests whether the ledger index is >= 5
        // getLedgerSqn() >= 5}
        static auto wasmHex = ledgerSqnWasmHex;

        {
            // featureSmartEscrow disabled
            Env env(*this, features - featureSmartEscrow);
            env.fund(XRP(5000), alice, carol);
            XRPAmount const txnFees =
                env.current()->fees().base * 10 + wasmHex.size() / 2 * 5;
            env(escrow::finish(carol, alice, 1),
                fee(txnFees),
                escrow::comp_allowance(4),
                ter(temDISABLED));
            env.close();
        }

        {
            // ComputationAllowance > max compute limit
            Env env(
                *this,
                envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->FEES.extension_compute_limit = 1'000;  // in gas
                    return cfg;
                }),
                features);
            env.fund(XRP(5000), alice, carol);
            // Run past the flag ledger so that a Fee change vote occurs and
            // updates FeeSettings. (It also activates all supported
            // amendments.)
            for (auto i = env.current()->seq(); i <= 257; ++i)
                env.close();

            auto const allowance = 1'001;
            env(escrow::finish(carol, alice, 1),
                fee(env.current()->fees().base + allowance),
                escrow::comp_allowance(allowance),
                ter(temBAD_LIMIT));
        }

        Env env(*this, features);

        // Run past the flag ledger so that a Fee change vote occurs and
        // updates FeeSettings. (It also activates all supported
        // amendments.)
        for (auto i = env.current()->seq(); i <= 257; ++i)
            env.close();

        XRPAmount const txnFees =
            env.current()->fees().base * 10 + wasmHex.size() / 2 * 5;
        env.fund(XRP(5000), alice, carol);

        // create escrow
        auto const seq = env.seq(alice);
        env(escrow::create(alice, carol, XRP(500)),
            escrow::finish_function(wasmHex),
            escrow::cancel_time(env.now() + 100s),
            fee(txnFees));
        env.close();

        {
            // no ComputationAllowance field
            env(escrow::finish(carol, alice, seq),
                ter(tefWASM_FIELD_NOT_INCLUDED));
        }

        {
            // ComputationAllowance value of 0
            env(escrow::finish(carol, alice, seq),
                escrow::comp_allowance(0),
                ter(temBAD_LIMIT));
        }

        {
            // not enough fees
            // This function takes 4 gas
            // In testing, 1 gas costs 1 drop
            auto const finishFee = env.current()->fees().base;
            env(escrow::finish(carol, alice, seq),
                fee(finishFee),
                escrow::comp_allowance(4),
                ter(telINSUF_FEE_P));
        }

        {
            // not enough gas
            // This function takes 4 gas
            // In testing, 1 gas costs 1 drop
            auto const finishFee = env.current()->fees().base + 4;
            env(escrow::finish(carol, alice, seq),
                fee(finishFee),
                escrow::comp_allowance(2),
                ter(tecFAILED_PROCESSING));
        }

        {
            // ComputationAllowance field included w/no FinishFunction on
            // escrow
            auto const seq2 = env.seq(alice);
            env(escrow::create(alice, carol, XRP(500)),
                escrow::finish_time(env.now() + 10s),
                escrow::cancel_time(env.now() + 100s));
            env.close();

            auto const allowance = 100;
            env(escrow::finish(carol, alice, seq2),
                fee(env.current()->fees().base +
                    (allowance * env.current()->fees().gasPrice) /
                        MICRO_DROPS_PER_DROP +
                    1),
                escrow::comp_allowance(allowance),
                ter(tefNO_WASM));
        }
    }

    void
    testFinishFunction(FeatureBitset features)
    {
        testcase("Example escrow function");

        using namespace jtx;
        using namespace std::chrono;

        Account const alice{"alice"};
        Account const carol{"carol"};

        // Tests whether the ledger index is >= 5
        // getLedgerSqn() >= 5}
        auto const& wasmHex = ledgerSqnWasmHex;
        std::uint32_t const allowance = 5;
        auto escrowCreate = escrow::create(alice, carol, XRP(1000));
        auto [createFee, finishFee] = [&]() {
            Env env(*this, features);
            auto createFee =
                env.current()->fees().base * 10 + wasmHex.size() / 2 * 5;
            auto finishFee = env.current()->fees().base +
                (allowance * env.current()->fees().gasPrice) /
                    MICRO_DROPS_PER_DROP +
                1;
            return std::make_pair(createFee, finishFee);
        }();

        {
            // basic FinishFunction situation
            Env env(*this, features);
            // create escrow
            env.fund(XRP(5000), alice, carol);
            auto const seq = env.seq(alice);
            BEAST_EXPECT(env.ownerCount(alice) == 0);
            env(escrowCreate,
                escrow::finish_function(wasmHex),
                escrow::cancel_time(env.now() + 100s),
                fee(createFee));
            env.close();

            if (BEAST_EXPECT(env.ownerCount(alice) == 2))
            {
                env.require(balance(alice, XRP(4000) - createFee));
                env.require(balance(carol, XRP(5000)));

                env(escrow::finish(carol, alice, seq),
                    escrow::comp_allowance(allowance),
                    fee(finishFee),
                    ter(tecWASM_REJECTED));
                env(escrow::finish(alice, alice, seq),
                    escrow::comp_allowance(allowance),
                    fee(finishFee),
                    ter(tecWASM_REJECTED));
                env(escrow::finish(alice, alice, seq),
                    escrow::comp_allowance(allowance),
                    fee(finishFee),
                    ter(tecWASM_REJECTED));
                env(escrow::finish(carol, alice, seq),
                    escrow::comp_allowance(allowance),
                    fee(finishFee),
                    ter(tecWASM_REJECTED));
                env(escrow::finish(carol, alice, seq),
                    escrow::comp_allowance(allowance),
                    fee(finishFee),
                    ter(tecWASM_REJECTED));
                env.close();

                {
                    auto const txMeta = env.meta();
                    if (BEAST_EXPECT(txMeta->isFieldPresent(sfGasUsed)))
                        BEAST_EXPECTS(
                            env.meta()->getFieldU32(sfGasUsed) == allowance,
                            std::to_string(env.meta()->getFieldU32(sfGasUsed)));
                }

                env(escrow::finish(alice, alice, seq),
                    fee(finishFee),
                    escrow::comp_allowance(allowance),
                    ter(tesSUCCESS));

                auto const txMeta = env.meta();
                if (BEAST_EXPECT(txMeta->isFieldPresent(sfGasUsed)))
                    BEAST_EXPECTS(
                        txMeta->getFieldU32(sfGasUsed) == allowance,
                        std::to_string(txMeta->getFieldU32(sfGasUsed)));
                if (BEAST_EXPECT(txMeta->isFieldPresent(sfWasmReturnCode)))
                    BEAST_EXPECTS(
                        txMeta->getFieldI32(sfWasmReturnCode) == 5,
                        std::to_string(txMeta->getFieldI32(sfWasmReturnCode)));

                BEAST_EXPECT(env.ownerCount(alice) == 0);
            }
        }

        {
            // FinishFunction + Condition
            Env env(*this, features);
            env.fund(XRP(5000), alice, carol);
            BEAST_EXPECT(env.ownerCount(alice) == 0);
            auto const seq = env.seq(alice);
            // create escrow
            env(escrowCreate,
                escrow::finish_function(wasmHex),
                escrow::condition(escrow::cb1),
                escrow::cancel_time(env.now() + 100s),
                fee(createFee));
            env.close();
            auto const conditionFinishFee = finishFee +
                env.current()->fees().base * (32 + (escrow::fb1.size() / 16));

            if (BEAST_EXPECT(env.ownerCount(alice) == 2))
            {
                env.require(balance(alice, XRP(4000) - createFee));
                env.require(balance(carol, XRP(5000)));

                // no fulfillment provided, function fails
                env(escrow::finish(carol, alice, seq),
                    escrow::comp_allowance(allowance),
                    fee(finishFee),
                    ter(tecCRYPTOCONDITION_ERROR));
                // fulfillment provided, function fails
                env(escrow::finish(carol, alice, seq),
                    escrow::condition(escrow::cb1),
                    escrow::fulfillment(escrow::fb1),
                    escrow::comp_allowance(allowance),
                    fee(conditionFinishFee),
                    ter(tecWASM_REJECTED));
                if (BEAST_EXPECT(env.meta()->isFieldPresent(sfGasUsed)))
                    BEAST_EXPECTS(
                        env.meta()->getFieldU32(sfGasUsed) == allowance,
                        std::to_string(env.meta()->getFieldU32(sfGasUsed)));
                env.close();
                // no fulfillment provided, function succeeds
                env(escrow::finish(alice, alice, seq),
                    escrow::comp_allowance(allowance),
                    fee(conditionFinishFee),
                    ter(tecCRYPTOCONDITION_ERROR));
                // wrong fulfillment provided, function succeeds
                env(escrow::finish(alice, alice, seq),
                    escrow::condition(escrow::cb1),
                    escrow::fulfillment(escrow::fb2),
                    escrow::comp_allowance(allowance),
                    fee(conditionFinishFee),
                    ter(tecCRYPTOCONDITION_ERROR));
                // fulfillment provided, function succeeds, tx succeeds
                env(escrow::finish(alice, alice, seq),
                    escrow::condition(escrow::cb1),
                    escrow::fulfillment(escrow::fb1),
                    escrow::comp_allowance(allowance),
                    fee(conditionFinishFee),
                    ter(tesSUCCESS));

                auto const txMeta = env.meta();
                if (BEAST_EXPECT(txMeta->isFieldPresent(sfGasUsed)))
                    BEAST_EXPECT(txMeta->getFieldU32(sfGasUsed) == allowance);
                if (BEAST_EXPECT(txMeta->isFieldPresent(sfWasmReturnCode)))
                    BEAST_EXPECTS(
                        txMeta->getFieldI32(sfWasmReturnCode) == 6,
                        std::to_string(txMeta->getFieldI32(sfWasmReturnCode)));

                env.close();
                BEAST_EXPECT(env.ownerCount(alice) == 0);
            }
        }

        {
            // FinishFunction + FinishAfter
            Env env(*this, features);
            // create escrow
            env.fund(XRP(5000), alice, carol);
            auto const seq = env.seq(alice);
            BEAST_EXPECT(env.ownerCount(alice) == 0);
            auto const ts = env.now() + 97s;
            env(escrowCreate,
                escrow::finish_function(wasmHex),
                escrow::finish_time(ts),
                escrow::cancel_time(env.now() + 1000s),
                fee(createFee));
            env.close();

            if (BEAST_EXPECT(env.ownerCount(alice) == 2))
            {
                env.require(balance(alice, XRP(4000) - createFee));
                env.require(balance(carol, XRP(5000)));

                // finish time hasn't passed, function fails
                env(escrow::finish(carol, alice, seq),
                    escrow::comp_allowance(allowance),
                    fee(finishFee + 1),
                    ter(tecNO_PERMISSION));
                env.close();
                // finish time hasn't passed, function succeeds
                for (; env.now() < ts; env.close())
                    env(escrow::finish(carol, alice, seq),
                        escrow::comp_allowance(allowance),
                        fee(finishFee + 2),
                        ter(tecNO_PERMISSION));

                env(escrow::finish(carol, alice, seq),
                    escrow::comp_allowance(allowance),
                    fee(finishFee + 1),
                    ter(tesSUCCESS));

                auto const txMeta = env.meta();
                if (BEAST_EXPECT(txMeta->isFieldPresent(sfGasUsed)))
                    BEAST_EXPECT(txMeta->getFieldU32(sfGasUsed) == allowance);
                if (BEAST_EXPECT(txMeta->isFieldPresent(sfWasmReturnCode)))
                    BEAST_EXPECTS(
                        txMeta->getFieldI32(sfWasmReturnCode) == 13,
                        std::to_string(txMeta->getFieldI32(sfWasmReturnCode)));

                BEAST_EXPECT(env.ownerCount(alice) == 0);
            }
        }

        {
            // FinishFunction + FinishAfter #2
            Env env(*this, features);
            // create escrow
            env.fund(XRP(5000), alice, carol);
            auto const seq = env.seq(alice);
            BEAST_EXPECT(env.ownerCount(alice) == 0);
            env(escrowCreate,
                escrow::finish_function(wasmHex),
                escrow::finish_time(env.now() + 2s),
                escrow::cancel_time(env.now() + 100s),
                fee(createFee));
            // Don't close the ledger here

            if (BEAST_EXPECT(env.ownerCount(alice) == 2))
            {
                env.require(balance(alice, XRP(4000) - createFee));
                env.require(balance(carol, XRP(5000)));

                // finish time hasn't passed, function fails
                env(escrow::finish(carol, alice, seq),
                    escrow::comp_allowance(allowance),
                    fee(finishFee),
                    ter(tecNO_PERMISSION));
                env.close();

                // finish time has passed, function fails
                env(escrow::finish(carol, alice, seq),
                    escrow::comp_allowance(allowance),
                    fee(finishFee),
                    ter(tecWASM_REJECTED));
                if (BEAST_EXPECT(env.meta()->isFieldPresent(sfGasUsed)))
                    BEAST_EXPECTS(
                        env.meta()->getFieldU32(sfGasUsed) == allowance,
                        std::to_string(env.meta()->getFieldU32(sfGasUsed)));
                env.close();
                // finish time has passed, function succeeds, tx succeeds
                env(escrow::finish(carol, alice, seq),
                    escrow::comp_allowance(allowance),
                    fee(finishFee),
                    ter(tesSUCCESS));

                auto const txMeta = env.meta();
                if (BEAST_EXPECT(txMeta->isFieldPresent(sfGasUsed)))
                    BEAST_EXPECT(txMeta->getFieldU32(sfGasUsed) == allowance);
                if (BEAST_EXPECT(txMeta->isFieldPresent(sfWasmReturnCode)))
                    BEAST_EXPECTS(
                        txMeta->getFieldI32(sfWasmReturnCode) == 6,
                        std::to_string(txMeta->getFieldI32(sfWasmReturnCode)));

                env.close();
                BEAST_EXPECT(env.ownerCount(alice) == 0);
            }
        }
    }

    void
    testUpdateDataOnFailure(FeatureBitset features)
    {
        testcase("Update escrow data on failure");

        using namespace jtx;
        using namespace std::chrono;

        // wasm that always fails
        static auto const wasmHex = updateDataWasmHex;

        Account const alice{"alice"};
        Account const carol{"carol"};

        Env env(*this, features);
        // create escrow
        env.fund(XRP(5000), alice);
        auto const seq = env.seq(alice);
        BEAST_EXPECT(env.ownerCount(alice) == 0);
        auto escrowCreate = escrow::create(alice, alice, XRP(1000));
        XRPAmount txnFees =
            env.current()->fees().base * 10 + wasmHex.size() / 2 * 5;
        env(escrowCreate,
            escrow::finish_function(wasmHex),
            escrow::finish_time(env.now() + 2s),
            escrow::cancel_time(env.now() + 100s),
            fee(txnFees));
        env.close();
        env.close();
        env.close();

        if (BEAST_EXPECT(
                env.ownerCount(alice) == (1 + wasmHex.size() / 2 / 500)))
        {
            env.require(balance(alice, XRP(4000) - txnFees));

            auto const allowance = 14;
            XRPAmount const finishFee = env.current()->fees().base +
                (allowance * env.current()->fees().gasPrice) /
                    MICRO_DROPS_PER_DROP +
                1;

            // FinishAfter time hasn't passed
            env(escrow::finish(alice, alice, seq),
                escrow::comp_allowance(allowance),
                fee(finishFee),
                ter(tecWASM_REJECTED));

            auto const txMeta = env.meta();
            if (BEAST_EXPECT(txMeta && txMeta->isFieldPresent(sfGasUsed)))
                BEAST_EXPECTS(
                    txMeta->getFieldU32(sfGasUsed) == allowance,
                    std::to_string(txMeta->getFieldU32(sfGasUsed)));
            if (BEAST_EXPECT(txMeta->isFieldPresent(sfWasmReturnCode)))
                BEAST_EXPECTS(
                    txMeta->getFieldI32(sfWasmReturnCode) == -256,
                    std::to_string(txMeta->getFieldI32(sfWasmReturnCode)));

            auto const sle = env.le(keylet::escrow(alice, seq));
            if (BEAST_EXPECT(sle && sle->isFieldPresent(sfData)))
                BEAST_EXPECTS(
                    checkVL(sle, sfData, "Data"),
                    strHex(sle->getFieldVL(sfData)));
        }
    }

    void
    testAllHostFunctions(FeatureBitset features)
    {
        testcase("Test all host functions");

        using namespace jtx;
        using namespace std::chrono;

        // TODO: create wasm module for all host functions
        static auto wasmHex = allHostFunctionsWasmHex;

        Account const alice{"alice"};
        Account const carol{"carol"};

        {
            Env env(*this, features);
            // create escrow
            env.fund(XRP(5000), alice, carol);
            auto const seq = env.seq(alice);
            BEAST_EXPECT(env.ownerCount(alice) == 0);
            auto escrowCreate = escrow::create(alice, carol, XRP(1000));
            XRPAmount txnFees =
                env.current()->fees().base * 10 + wasmHex.size() / 2 * 5;
            env(escrowCreate,
                escrow::finish_function(wasmHex),
                escrow::finish_time(env.now() + 11s),
                escrow::cancel_time(env.now() + 100s),
                escrow::data("1000000000"),  // 1000 XRP in drops
                fee(txnFees));
            env.close();

            if (BEAST_EXPECT(
                    env.ownerCount(alice) == (1 + wasmHex.size() / 2 / 500)))
            {
                env.require(balance(alice, XRP(4000) - txnFees));
                env.require(balance(carol, XRP(5000)));

                auto const allowance = 1'000'000;
                XRPAmount const finishFee = env.current()->fees().base +
                    (allowance * env.current()->fees().gasPrice) /
                        MICRO_DROPS_PER_DROP +
                    1;

                // FinishAfter time hasn't passed
                env(escrow::finish(carol, alice, seq),
                    escrow::comp_allowance(allowance),
                    fee(finishFee),
                    ter(tecNO_PERMISSION));
                env.close();
                env.close();
                env.close();

                // reduce the destination balance
                env(pay(carol, alice, XRP(4500)));
                env.close();
                env.close();

                env(escrow::finish(alice, alice, seq),
                    escrow::comp_allowance(allowance),
                    fee(finishFee),
                    ter(tesSUCCESS));

                auto const txMeta = env.meta();
                if (BEAST_EXPECT(txMeta && txMeta->isFieldPresent(sfGasUsed)))
                    BEAST_EXPECTS(
                        txMeta->getFieldU32(sfGasUsed) == 794,
                        std::to_string(txMeta->getFieldU32(sfGasUsed)));
                if (BEAST_EXPECT(txMeta->isFieldPresent(sfWasmReturnCode)))
                    BEAST_EXPECT(txMeta->getFieldI32(sfWasmReturnCode) == 1);

                env.close();
                BEAST_EXPECT(env.ownerCount(alice) == 0);
            }
        }
    }

    void
    testKeyletHostFunctions(FeatureBitset features)
    {
        testcase("Test all keylet host functions");

        using namespace jtx;
        using namespace std::chrono;

        // TODO: create wasm module for all host functions
        static auto wasmHex = allKeyletsWasmHex;

        Account const alice{"alice"};
        Account const carol{"carol"};

        {
            Env env{*this};
            env.fund(XRP(10000), alice, carol);

            BEAST_EXPECT(env.seq(alice) == 4);
            BEAST_EXPECT(env.ownerCount(alice) == 0);

            // base objects that need to be created first
            auto const tokenId =
                token::getNextID(env, alice, 0, tfTransferable);
            env(token::mint(alice, 0u), txflags(tfTransferable));
            env(trust(alice, carol["USD"](1'000'000)));
            env.close();
            BEAST_EXPECT(env.seq(alice) == 6);
            BEAST_EXPECT(env.ownerCount(alice) == 2);

            // set up a bunch of objects to check their keylets
            AMM amm(env, carol, XRP(10), carol["USD"](1000));
            env(check::create(alice, carol, XRP(100)));
            env(credentials::create(alice, alice, "termsandconditions"));
            env(delegate::set(alice, carol, {"TrustSet"}));
            env(deposit::auth(alice, carol));
            env(did::set(alice), did::data("alice_did"));
            env(escrow::create(alice, carol, XRP(100)),
                escrow::finish_time(env.now() + 100s));
            MPTTester mptTester{env, alice, {.fund = false}};
            mptTester.create();
            mptTester.authorize({.account = carol});
            env(token::createOffer(carol, tokenId, XRP(100)),
                token::owner(alice));
            env(offer(alice, carol["GBP"](0.1), XRP(100)));
            env(paychan::create(alice, carol, XRP(1000), 100s, alice.pk()));
            pdomain::Credentials credentials{{alice, "first credential"}};
            env(pdomain::setTx(alice, credentials));
            env(signers(alice, 1, {{carol, 1}}));
            env(ticket::create(alice, 1));
            Vault vault{env};
            auto [tx, _keylet] =
                vault.create({.owner = alice, .asset = xrpIssue()});
            env(tx);
            env.close();

            BEAST_EXPECTS(
                env.ownerCount(alice) == 17,
                std::to_string(env.ownerCount(alice)));
            if (BEAST_EXPECTS(
                    env.seq(alice) == 20, std::to_string(env.seq(alice))))
            {
                auto const seq = env.seq(alice);
                XRPAmount txnFees =
                    env.current()->fees().base * 10 + wasmHex.size() / 2 * 5;
                env(escrow::create(alice, carol, XRP(1000)),
                    escrow::finish_function(wasmHex),
                    escrow::finish_time(env.now() + 2s),
                    escrow::cancel_time(env.now() + 100s),
                    fee(txnFees));
                env.close();
                env.close();
                env.close();

                auto const allowance = 2'985;
                auto const finishFee = env.current()->fees().base +
                    (allowance * env.current()->fees().gasPrice) /
                        MICRO_DROPS_PER_DROP +
                    1;
                env(escrow::finish(carol, alice, seq),
                    escrow::comp_allowance(allowance),
                    fee(finishFee));
                env.close();

                auto const txMeta = env.meta();
                if (BEAST_EXPECT(txMeta && txMeta->isFieldPresent(sfGasUsed)))
                {
                    auto const gasUsed = txMeta->getFieldU32(sfGasUsed);
                    BEAST_EXPECTS(
                        gasUsed == allowance, std::to_string(gasUsed));
                }
                BEAST_EXPECTS(
                    env.ownerCount(alice) == 17,
                    std::to_string(env.ownerCount(alice)));
            }
        }
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testCreateFinishFunctionPreflight(features);
        testFinishWasmFailures(features);
        testFinishFunction(features);
        testUpdateDataOnFailure(features);

        // TODO: Update module with new host functions
        testAllHostFunctions(features);
        testKeyletHostFunctions(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{testable_amendments()};
        testWithFeats(all);
    }
};

BEAST_DEFINE_TESTSUITE(EscrowSmart, app, ripple);

}  // namespace test
}  // namespace ripple
