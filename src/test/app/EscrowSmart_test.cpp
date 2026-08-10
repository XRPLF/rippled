#include <test/app/wasm_fixtures/fixtures.h>
#include <test/jtx/AMM.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/credentials.h>
#include <test/jtx/delegate.h>
#include <test/jtx/deposit.h>
#include <test/jtx/did.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/escrow.h>
#include <test/jtx/fee.h>
#include <test/jtx/mpt.h>
#include <test/jtx/multisign.h>
#include <test/jtx/noop.h>
#include <test/jtx/offer.h>
#include <test/jtx/pay.h>
#include <test/jtx/permissioned_domains.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>
#include <test/jtx/token.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>
#include <test/jtx/vault.h>

#include <xrpld/core/Config.h>

#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/core/StartUpType.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <source_location>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::test {

struct EscrowSmart_test : public beast::unit_test::Suite
{
    void
    testCreateBytecodePreflight(FeatureBitset features)
    {
        testcase("Test preflight checks involving Bytecode");

        using namespace jtx;
        using namespace std::chrono;

        Account const alice{"alice"};
        Account const carol{"carol"};

        // Tests whether the ledger index is >= 5
        // getLedgerSqn() >= 5}

        {
            // featureSmartEscrow disabled
            Env env(*this, features - featureSmartEscrow);
            env.fund(XRP(5000), alice, carol);
            XRPAmount const txnFees = env.current()->fees().base + 1000;
            auto const escrowCreate = escrow::create(alice, carol, XRP(1000));
            env(escrowCreate,
                escrow::Bytecode(kLedgerSqnWasmHex),
                escrow::kCancelTime(env.now() + 100s),
                Fee(txnFees),
                Ter(temDISABLED));
            env.close();

            env(escrowCreate,
                escrow::Bytecode(kLedgerSqnWasmHex),
                escrow::kCancelTime(env.now() + 100s),
                escrow::Data("00112233"),
                Fee(txnFees),
                Ter(temDISABLED));
            env.close();
        }

        {
            // Bytecode > max length
            Env env(
                *this,
                envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->fees.bytecodeSizeLimit = 10;  // 10 bytes
                    return cfg;
                }),
                features);
            XRPAmount const txnFees = env.current()->fees().base + 1000;
            // create escrow
            env.fund(XRP(5000), alice, carol);

            auto const escrowCreate = escrow::create(alice, carol, XRP(500));

            // 11-byte string
            std::string const longWasmHex = "00112233445566778899AA";
            env(escrowCreate,
                escrow::Bytecode(longWasmHex),
                escrow::kCancelTime(env.now() + 100s),
                Fee(txnFees),
                Ter(temMALFORMED));
            env.close();
        }

        {
            // compute limit set to 0
            Env env(
                *this,
                envconfig([](std::unique_ptr<Config> cfg) {
                    // WASM runtime disabled
                    cfg->fees.gasLimit = 0;
                    return cfg;
                }),
                features);
            XRPAmount const txnFees = env.current()->fees().base + 1000;
            // create escrow
            env.fund(XRP(5000), alice, carol);

            auto const escrowCreate = escrow::create(alice, carol, XRP(500));

            env(escrowCreate,
                escrow::Bytecode(kLedgerSqnWasmHex),
                escrow::kCancelTime(env.now() + 100s),
                escrow::Gas(100),
                Fee(txnFees),
                Ter(temMALFORMED));
            env.close();
        }

        {
            // size limit set to 0
            Env env(
                *this,
                envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->fees.bytecodeSizeLimit = 0;  // WASM upload disabled
                    return cfg;
                }),
                features);
            XRPAmount const txnFees = env.current()->fees().base + 1000;
            // create escrow
            env.fund(XRP(5000), alice, carol);

            auto const escrowCreate = escrow::create(alice, carol, XRP(500));

            // 2-byte string
            env(escrowCreate,
                escrow::Bytecode("AA"),
                escrow::kCancelTime(env.now() + 100s),
                Fee(txnFees),
                Ter(temTEMP_DISABLED));
            env.close();

            env(escrowCreate,
                escrow::Bytecode(kLedgerSqnWasmHex),
                escrow::kCancelTime(env.now() + 100s),
                Fee(txnFees),
                Ter(temTEMP_DISABLED));
            env.close();
        }

        {
            // Data without Bytecode
            Env env(*this, features);
            XRPAmount const txnFees = env.current()->fees().base + 100000;
            // create escrow
            env.fund(XRP(5000), alice, carol);

            auto const escrowCreate = escrow::create(alice, carol, XRP(500));

            std::string const longData(4, 'A');
            env(escrowCreate,
                escrow::Data(longData),
                escrow::kFinishTime(env.now() + 100s),
                Fee(txnFees),
                Ter(temMALFORMED));
            env.close();
        }

        {
            // Data > max length
            Env env(*this, features);
            XRPAmount const txnFees = env.current()->fees().base + 100000;
            // create escrow
            env.fund(XRP(5000), alice, carol);

            auto const escrowCreate = escrow::create(alice, carol, XRP(500));

            // string of length kMaxWasmDataLength * 2 + 2
            std::string const longData((kMaxWasmDataLength + 1) * 2, 'B');
            env(escrowCreate,
                escrow::Data(longData),
                escrow::Bytecode(kLedgerSqnWasmHex),
                escrow::kCancelTime(env.now() + 100s),
                Fee(txnFees),
                Ter(temMALFORMED));
            env.close();
        }

        Env env(
            *this,
            envconfig([](std::unique_ptr<Config> cfg) {
                cfg->startUp = StartUpType::Fresh;
                return cfg;
            }),
            features);
        XRPAmount const txnFees =
            env.current()->fees().base * 10 + kLedgerSqnWasmHex.size() / 2 * 5;
        // create escrow
        env.fund(XRP(5000), alice, carol);

        auto escrowCreate = escrow::create(alice, carol, XRP(500));

        // Success situations
        {
            // Bytecode + CancelAfter
            env(escrowCreate,
                escrow::Bytecode(kLedgerSqnWasmHex),
                escrow::kCancelTime(env.now() + 20s),
                Fee(txnFees));
            env.close();
        }
        {
            // Bytecode + Condition + CancelAfter
            env(escrowCreate,
                escrow::Bytecode(kLedgerSqnWasmHex),
                escrow::kCancelTime(env.now() + 30s),
                escrow::kCondition(escrow::kCb1),
                Fee(txnFees));
            env.close();
        }
        {
            // Bytecode + FinishAfter + CancelAfter
            env(escrowCreate,
                escrow::Bytecode(kLedgerSqnWasmHex),
                escrow::kCancelTime(env.now() + 40s),
                escrow::kFinishTime(env.now() + 2s),
                Fee(txnFees));
            env.close();
        }
        {
            // Bytecode + FinishAfter + Condition + CancelAfter
            env(escrowCreate,
                escrow::Bytecode(kLedgerSqnWasmHex),
                escrow::kCancelTime(env.now() + 50s),
                escrow::kCondition(escrow::kCb1),
                escrow::kFinishTime(env.now() + 2s),
                Fee(txnFees));
            env.close();
        }

        // Failure situations (i.e. all other combinations)
        {
            // only Bytecode
            env(escrowCreate,
                escrow::Bytecode(kLedgerSqnWasmHex),
                Fee(txnFees),
                Ter(temBAD_EXPIRATION));
            env.close();
        }
        {
            // Bytecode + FinishAfter
            env(escrowCreate,
                escrow::Bytecode(kLedgerSqnWasmHex),
                escrow::kFinishTime(env.now() + 2s),
                Fee(txnFees),
                Ter(temBAD_EXPIRATION));
            env.close();
        }
        {
            // Bytecode + Condition
            env(escrowCreate,
                escrow::Bytecode(kLedgerSqnWasmHex),
                escrow::kCondition(escrow::kCb1),
                Fee(txnFees),
                Ter(temBAD_EXPIRATION));
            env.close();
        }
        {
            // Bytecode + FinishAfter + Condition
            env(escrowCreate,
                escrow::Bytecode(kLedgerSqnWasmHex),
                escrow::kCondition(escrow::kCb1),
                escrow::kFinishTime(env.now() + 2s),
                Fee(txnFees),
                Ter(temBAD_EXPIRATION));
            env.close();
        }
        {
            // Bytecode 0 length
            env(escrowCreate,
                escrow::Bytecode(""),
                escrow::kCancelTime(env.now() + 60s),
                Fee(txnFees),
                Ter(temMALFORMED));
            env.close();
        }
        {
            // Not enough fees
            env(escrowCreate,
                escrow::Bytecode(kLedgerSqnWasmHex),
                escrow::kCancelTime(env.now() + 70s),
                Fee(txnFees - 1),
                Ter(telINSUF_FEE_P));
            env.close();
        }

        {
            // Bytecode nonexistent host function
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
                escrow::Bytecode(badWasmHex),
                escrow::kCancelTime(env.now() + 100s),
                Fee(txnFees),
                Ter(temINVALID_BYTECODE));
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

        {
            // featureSmartEscrow disabled
            Env env(*this, features - featureSmartEscrow);
            env.fund(XRP(5000), alice, carol);
            XRPAmount const txnFees =
                env.current()->fees().base * 10 + kLedgerSqnWasmHex.size() / 2 * 5;
            env(escrow::finish(carol, alice, 1), Fee(txnFees), escrow::Gas(4), Ter(temDISABLED));
            env.close();
        }

        {
            // Gas > max compute limit
            Env env(
                *this,
                envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->fees.gasLimit = 1'000;  // in gas
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
                Fee(env.current()->fees().base + allowance),
                escrow::Gas(allowance),
                Ter(temBAD_LIMIT));
        }

        {
            // WASM compute disabled
            using namespace test::jtx;
            using namespace std::chrono;
            Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
                        cfg->fees.gasLimit = 0;
                        return cfg;
                    })};

            Account const alice{"alice"};
            env.fund(XRP(1000), alice);
            env.close();

            auto const seq = env.seq(alice);
            auto const keylet = keylet::escrow(alice.id(), SeqProxy::rawSequence(seq));
            env(noop(alice));  // to align sequence numbers

            // This adds the Escrow ledger object by hand, bypassing normal
            // transaction processing This is necessary because the config
            // cannot be updated in the middle of a test, and we cannot easily
            // create a Smart Escrow while the compute limit is set to 0
            env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal j) {
                auto sle = std::make_shared<SLE>(keylet);

                sle->setAccountID(sfAccount, alice.id());
                sle->setFieldAmount(sfAmount, XRP(100));
                sle->setFieldU32(sfCancelAfter, 110);
                sle->setAccountID(sfDestination, alice.id());
                sle->setFieldVL(sfBytecode, strUnHex(kLedgerSqnWasmHex).value());
                sle->setFieldU32(sfFlags, 0);
                sle->setFieldU64(sfOwnerNode, 0);
                uint256 tmp;
                BEAST_EXPECT(tmp.parseHex(
                    "F63D1A452A96C19EFD77901FB37D236C59EAA746771A6"
                    "85D1BBA57A2238B9401"));
                sle->setFieldH256(sfPreviousTxnID, tmp);
                sle->setFieldU32(sfPreviousTxnLgrSeq, 4);
                sle->setFieldU32(sfSequence, seq);

                view.rawInsert(sle);
                return true;
            });
            BEAST_EXPECT(env.le(keylet));

            env(escrow::finish(alice, alice, seq),
                escrow::Gas(1000),
                Fee(env.current()->fees().base + 1000),
                Ter(temTEMP_DISABLED));
        }

        Env env(*this, features);

        // Run past the flag ledger so that a Fee change vote occurs and
        // updates FeeSettings. (It also activates all supported
        // amendments.)
        for (auto i = env.current()->seq(); i <= 257; ++i)
            env.close();

        XRPAmount const txnFees =
            env.current()->fees().base * 10 + kLedgerSqnWasmHex.size() / 2 * 5;
        env.fund(XRP(5000), alice, carol);

        // create escrow
        auto const seq = env.seq(alice);
        env(escrow::create(alice, carol, XRP(500)),
            escrow::Bytecode(kLedgerSqnWasmHex),
            escrow::kCancelTime(env.now() + 100s),
            Fee(txnFees));
        env.close();

        {
            // no Gas field
            env(escrow::finish(carol, alice, seq), Ter(tefBYTECODE_NOT_INCLUDED));
        }

        {
            // Gas value of 0
            env(escrow::finish(carol, alice, seq), escrow::Gas(0), Ter(temBAD_LIMIT));
        }

        {
            // not enough fees
            // This function takes 4 gas
            // In testing, 1 gas costs 1 drop
            auto const finishFee = env.current()->fees().base + 3;
            env(escrow::finish(carol, alice, seq),
                Fee(finishFee),
                escrow::Gas(4),
                Ter(telINSUF_FEE_P));
        }

        {
            // not enough gas
            // This function takes 4 gas
            // In testing, 1 gas costs 1 drop
            auto const finishFee = env.current()->fees().base + 4;
            env(escrow::finish(carol, alice, seq),
                Fee(finishFee),
                escrow::Gas(2),
                Ter(tecOUT_OF_GAS));

            // Running out of gas still reports the gas consumed, which is the
            // whole allowance. The function did not run to completion, so
            // there is no return code to report.
            auto const txMeta = env.meta();
            if (BEAST_EXPECT(txMeta && txMeta->isFieldPresent(sfGasUsed)))
            {
                BEAST_EXPECTS(
                    txMeta->getFieldU32(sfGasUsed) == 2,
                    std::to_string(txMeta->getFieldU32(sfGasUsed)));
            }
            BEAST_EXPECT(txMeta && !txMeta->isFieldPresent(sfVMReturnCode));
        }

        {
            // Gas field included w/no Bytecode on
            // escrow
            auto const seq2 = env.seq(alice);
            env(escrow::create(alice, carol, XRP(500)),
                escrow::kFinishTime(env.now() + 10s),
                escrow::kCancelTime(env.now() + 100s));
            env.close();

            auto const allowance = 100;
            env(escrow::finish(carol, alice, seq2),
                Fee(env.current()->fees().base +
                    (allowance * env.current()->fees().gasPrice) / microDropsPerDrop + 1),
                escrow::Gas(allowance),
                Ter(tefNO_BYTECODE));
        }

        {
            // a trap in the wasm code reports the gas it burned, which is only
            // part of the allowance
            auto const trapSeq = env.seq(alice);
            env(escrow::create(alice, carol, XRP(500)),
                escrow::Bytecode(kTrapUnreachableHex),
                escrow::kCancelTime(env.now() + 100s),
                Fee(env.current()->fees().base * 10 + kTrapUnreachableHex.size() / 2 * 5));
            env.close();

            std::uint32_t const allowance = 1000;
            env(escrow::finish(carol, alice, trapSeq),
                Fee(env.current()->fees().base +
                    (allowance * env.current()->fees().gasPrice) / microDropsPerDrop + 1),
                escrow::Gas(allowance),
                Ter(tecFAILED_PROCESSING));

            auto const txMeta = env.meta();
            if (BEAST_EXPECT(txMeta && txMeta->isFieldPresent(sfGasUsed)))
            {
                auto const gasUsed = txMeta->getFieldU32(sfGasUsed);
                BEAST_EXPECTS(gasUsed < allowance, std::to_string(gasUsed));
            }
            BEAST_EXPECT(txMeta && !txMeta->isFieldPresent(sfVMReturnCode));
        }
    }

    void
    testBytecode(FeatureBitset features)
    {
        testcase("Example escrow function");

        using namespace jtx;
        using namespace std::chrono;

        Account const alice{"alice"};
        Account const carol{"carol"};

        // Tests whether the ledger index is >= 5
        // getLedgerSqn() >= 5}
        std::uint32_t const allowance = 467;
        auto escrowCreate = escrow::create(alice, carol, XRP(1000));
        auto [createFee, finishFee] = [&]() {
            Env const env(*this, features);
            auto createFee = env.current()->fees().base * 10 + kLedgerSqnWasmHex.size() / 2 * 5;
            auto finishFee = env.current()->fees().base +
                (allowance * env.current()->fees().gasPrice) / microDropsPerDrop + 1;
            return std::make_pair(createFee, finishFee);
        }();

        {
            // basic Bytecode situation
            Env env(*this, features);
            // create escrow
            env.fund(XRP(5000), alice, carol);
            auto const seq = env.seq(alice);
            BEAST_EXPECT(env.ownerCount(alice) == 0);
            env(escrowCreate,
                escrow::Bytecode(kLedgerSqnWasmHex),
                escrow::kCancelTime(env.now() + 100s),
                Fee(createFee));
            env.close();

            if (BEAST_EXPECT(env.ownerCount(alice) == 2))
            {
                env.require(Balance(alice, XRP(4000) - createFee));
                env.require(Balance(carol, XRP(5000)));

                env(escrow::finish(carol, alice, seq),
                    escrow::Gas(allowance),
                    Fee(finishFee),
                    Ter(tecBYTECODE_REJECTED));
                env(escrow::finish(alice, alice, seq),
                    escrow::Gas(allowance),
                    Fee(finishFee),
                    Ter(tecBYTECODE_REJECTED));
                env(escrow::finish(alice, alice, seq),
                    escrow::Gas(allowance),
                    Fee(finishFee),
                    Ter(tecBYTECODE_REJECTED));
                env(escrow::finish(carol, alice, seq),
                    escrow::Gas(allowance),
                    Fee(finishFee),
                    Ter(tecBYTECODE_REJECTED));
                env(escrow::finish(carol, alice, seq),
                    escrow::Gas(allowance),
                    Fee(finishFee),
                    Ter(tecBYTECODE_REJECTED));
                env.close();

                {
                    auto const txMeta = env.meta();
                    if (BEAST_EXPECT(txMeta->isFieldPresent(sfGasUsed)))
                    {
                        BEAST_EXPECTS(
                            env.meta()->getFieldU32(sfGasUsed) == allowance,
                            std::to_string(env.meta()->getFieldU32(sfGasUsed)));
                    }
                }

                env(escrow::finish(alice, alice, seq),
                    Fee(finishFee),
                    escrow::Gas(allowance),
                    Ter(tesSUCCESS));

                auto const txMeta = env.meta();
                if (BEAST_EXPECT(txMeta->isFieldPresent(sfGasUsed)))
                {
                    BEAST_EXPECTS(
                        txMeta->getFieldU32(sfGasUsed) == allowance,
                        std::to_string(txMeta->getFieldU32(sfGasUsed)));
                }
                if (BEAST_EXPECT(txMeta->isFieldPresent(sfVMReturnCode)))
                {
                    BEAST_EXPECTS(
                        txMeta->getFieldI32(sfVMReturnCode) == 5,
                        std::to_string(txMeta->getFieldI32(sfVMReturnCode)));
                }

                BEAST_EXPECT(env.ownerCount(alice) == 0);
            }
        }

        {
            // Bytecode + Condition
            Env env(*this, features);
            env.fund(XRP(5000), alice, carol);
            BEAST_EXPECT(env.ownerCount(alice) == 0);
            auto const seq = env.seq(alice);
            // create escrow
            env(escrowCreate,
                escrow::Bytecode(kLedgerSqnWasmHex),
                escrow::kCondition(escrow::kCb1),
                escrow::kCancelTime(env.now() + 100s),
                Fee(createFee));
            env.close();
            auto const conditionFinishFee =
                finishFee + env.current()->fees().base * (32 + (escrow::kFb1.size() / 16));

            if (BEAST_EXPECT(env.ownerCount(alice) == 2))
            {
                env.require(Balance(alice, XRP(4000) - createFee));
                env.require(Balance(carol, XRP(5000)));

                // no fulfillment provided, function fails
                env(escrow::finish(carol, alice, seq),
                    escrow::Gas(allowance),
                    Fee(finishFee),
                    Ter(tecCRYPTOCONDITION_ERROR));
                // fulfillment provided, function fails
                env(escrow::finish(carol, alice, seq),
                    escrow::kCondition(escrow::kCb1),
                    escrow::kFulfillment(escrow::kFb1),
                    escrow::Gas(allowance),
                    Fee(conditionFinishFee),
                    Ter(tecBYTECODE_REJECTED));
                if (BEAST_EXPECT(env.meta()->isFieldPresent(sfGasUsed)))
                {
                    BEAST_EXPECTS(
                        env.meta()->getFieldU32(sfGasUsed) == allowance,
                        std::to_string(env.meta()->getFieldU32(sfGasUsed)));
                }
                env.close();
                // no fulfillment provided, function succeeds
                env(escrow::finish(alice, alice, seq),
                    escrow::Gas(allowance),
                    Fee(conditionFinishFee),
                    Ter(tecCRYPTOCONDITION_ERROR));
                // wrong fulfillment provided, function succeeds
                env(escrow::finish(alice, alice, seq),
                    escrow::kCondition(escrow::kCb1),
                    escrow::kFulfillment(escrow::kFb2),
                    escrow::Gas(allowance),
                    Fee(conditionFinishFee),
                    Ter(tecCRYPTOCONDITION_ERROR));
                // fulfillment provided, function succeeds, tx succeeds
                env(escrow::finish(alice, alice, seq),
                    escrow::kCondition(escrow::kCb1),
                    escrow::kFulfillment(escrow::kFb1),
                    escrow::Gas(allowance),
                    Fee(conditionFinishFee),
                    Ter(tesSUCCESS));

                auto const txMeta = env.meta();
                if (BEAST_EXPECT(txMeta->isFieldPresent(sfGasUsed)))
                {
                    BEAST_EXPECTS(
                        txMeta->getFieldU32(sfGasUsed) == allowance,
                        std::to_string(txMeta->getFieldU32(sfGasUsed)));
                }
                if (BEAST_EXPECT(txMeta->isFieldPresent(sfVMReturnCode)))
                {
                    BEAST_EXPECTS(
                        txMeta->getFieldI32(sfVMReturnCode) == 5,
                        std::to_string(txMeta->getFieldI32(sfVMReturnCode)));
                }

                env.close();
                BEAST_EXPECT(env.ownerCount(alice) == 0);
            }
        }

        {
            // Bytecode + FinishAfter
            Env env(*this, features);
            // create escrow
            env.fund(XRP(5000), alice, carol);
            auto const seq = env.seq(alice);
            BEAST_EXPECT(env.ownerCount(alice) == 0);
            auto const ts = env.now() + 97s;
            env(escrowCreate,
                escrow::Bytecode(kLedgerSqnWasmHex),
                escrow::kFinishTime(ts),
                escrow::kCancelTime(env.now() + 1000s),
                Fee(createFee));
            env.close();

            if (BEAST_EXPECT(env.ownerCount(alice) == 2))
            {
                env.require(Balance(alice, XRP(4000) - createFee));
                env.require(Balance(carol, XRP(5000)));

                // finish time hasn't passed, function fails
                env(escrow::finish(carol, alice, seq),
                    escrow::Gas(allowance),
                    Fee(finishFee + 1),
                    Ter(tecNO_PERMISSION));
                env.close();
                // finish time hasn't passed, function succeeds
                for (; env.now() < ts; env.close())
                {
                    env(escrow::finish(carol, alice, seq),
                        escrow::Gas(allowance),
                        Fee(finishFee + 2),
                        Ter(tecNO_PERMISSION));
                }

                env(escrow::finish(carol, alice, seq),
                    escrow::Gas(allowance),
                    Fee(finishFee + 1),
                    Ter(tesSUCCESS));

                auto const txMeta = env.meta();
                if (BEAST_EXPECT(txMeta->isFieldPresent(sfGasUsed)))
                    BEAST_EXPECT(txMeta->getFieldU32(sfGasUsed) == allowance);
                if (BEAST_EXPECT(txMeta->isFieldPresent(sfVMReturnCode)))
                {
                    BEAST_EXPECTS(
                        txMeta->getFieldI32(sfVMReturnCode) == 5,
                        std::to_string(txMeta->getFieldI32(sfVMReturnCode)));
                }

                BEAST_EXPECT(env.ownerCount(alice) == 0);
            }
        }

        {
            // Bytecode + FinishAfter #2
            Env env(*this, features);
            // create escrow
            env.fund(XRP(5000), alice, carol);
            auto const seq = env.seq(alice);
            BEAST_EXPECT(env.ownerCount(alice) == 0);
            env(escrowCreate,
                escrow::Bytecode(kLedgerSqnWasmHex),
                escrow::kFinishTime(env.now() + 2s),
                escrow::kCancelTime(env.now() + 100s),
                Fee(createFee));
            // Don't close the ledger here

            if (BEAST_EXPECT(env.ownerCount(alice) == 2))
            {
                env.require(Balance(alice, XRP(4000) - createFee));
                env.require(Balance(carol, XRP(5000)));

                // finish time hasn't passed, function fails
                env(escrow::finish(carol, alice, seq),
                    escrow::Gas(allowance),
                    Fee(finishFee),
                    Ter(tecNO_PERMISSION));
                env.close();

                // finish time has passed, function fails
                env(escrow::finish(carol, alice, seq),
                    escrow::Gas(allowance),
                    Fee(finishFee),
                    Ter(tecBYTECODE_REJECTED));
                if (BEAST_EXPECT(env.meta()->isFieldPresent(sfGasUsed)))
                {
                    BEAST_EXPECTS(
                        env.meta()->getFieldU32(sfGasUsed) == allowance,
                        std::to_string(env.meta()->getFieldU32(sfGasUsed)));
                }
                env.close();
                // finish time has passed, function succeeds, tx succeeds
                env(escrow::finish(carol, alice, seq),
                    escrow::Gas(allowance),
                    Fee(finishFee),
                    Ter(tesSUCCESS));

                auto const txMeta = env.meta();
                if (BEAST_EXPECT(txMeta->isFieldPresent(sfGasUsed)))
                    BEAST_EXPECT(txMeta->getFieldU32(sfGasUsed) == allowance);
                if (BEAST_EXPECT(txMeta->isFieldPresent(sfVMReturnCode)))
                {
                    BEAST_EXPECTS(
                        txMeta->getFieldI32(sfVMReturnCode) == 5,
                        std::to_string(txMeta->getFieldI32(sfVMReturnCode)));
                }

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
        Account const alice{"alice"};
        Account const carol{"carol"};

        Env env(*this, features);
        // create escrow
        env.fund(XRP(5000), alice);
        auto const seq = env.seq(alice);
        BEAST_EXPECT(env.ownerCount(alice) == 0);
        auto escrowCreate = escrow::create(alice, alice, XRP(1000));
        XRPAmount const txnFees =
            env.current()->fees().base * 10 + kUpdateDataWasmHex.size() / 2 * 5;
        env(escrowCreate,
            escrow::Bytecode(kUpdateDataWasmHex),
            escrow::kFinishTime(env.now() + 2s),
            escrow::kCancelTime(env.now() + 100s),
            Fee(txnFees));
        env.close();
        env.close();
        env.close();

        if (BEAST_EXPECT(env.ownerCount(alice) == (1 + (kUpdateDataWasmHex.size() / 2 / 500))))
        {
            env.require(Balance(alice, XRP(4000) - txnFees));

            auto const allowance = 1420;
            XRPAmount const finishFee = env.current()->fees().base +
                (allowance * env.current()->fees().gasPrice) / microDropsPerDrop + 1;

            // FinishAfter time hasn't passed
            env(escrow::finish(alice, alice, seq),
                escrow::Gas(allowance),
                Fee(finishFee),
                Ter(tecBYTECODE_REJECTED));

            auto const txMeta = env.meta();
            if (BEAST_EXPECT(txMeta && txMeta->isFieldPresent(sfGasUsed)))
            {
                BEAST_EXPECTS(
                    txMeta->getFieldU32(sfGasUsed) == allowance,
                    std::to_string(txMeta->getFieldU32(sfGasUsed)));
            }
            if (BEAST_EXPECT(txMeta->isFieldPresent(sfVMReturnCode)))
            {
                BEAST_EXPECTS(
                    txMeta->getFieldI32(sfVMReturnCode) == -256,
                    std::to_string(txMeta->getFieldI32(sfVMReturnCode)));
            }

            auto const sle = env.le(keylet::escrow(alice, SeqProxy::rawSequence(seq)));
            if (BEAST_EXPECT(sle && sle->isFieldPresent(sfData)))
                BEAST_EXPECTS(checkVL(sle, sfData, "Data"), strHex(sle->getFieldVL(sfData)));
        }
    }

    void
    testFees(FeatureBitset features)
    {
        testcase("Fees");

        using namespace jtx;
        using namespace std::chrono;

        Account const alice{"alice"};
        Account const carol{"carol"};

        // Tests whether the ledger index is >= 5
        // getLedgerSqn() >= 5}
        uint64_t const allowance = 467;
        auto escrowCreate = escrow::create(alice, carol, XRP(1000));
        auto createFee = [&]() {
            Env const env(*this, features);
            auto createFee = env.current()->fees().base * 10 + kLedgerSqnWasmHex.size() / 2 * 5;
            return createFee;
        }();

        {
            // ensure fees don't overflow
            Env env(
                *this,
                envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->fees.gasPrice = 1'000'000;  // in gas
                    return cfg;
                }),
                features);
            // Run past the flag ledger so that a Fee change vote occurs and
            // updates FeeSettings. (It also activates all supported
            // amendments.)
            for (auto i = env.current()->seq(); i <= 257; ++i)
                env.close();

            // create escrow
            env.fund(XRP(5000), alice, carol);
            auto const seq = env.seq(alice);
            BEAST_EXPECT(env.ownerCount(alice) == 0);
            env(escrowCreate,
                escrow::Bytecode(kLedgerSqnWasmHex),
                escrow::kCancelTime(env.now() + 100s),
                Fee(createFee));
            env.close();

            if (BEAST_EXPECT(env.ownerCount(alice) == 2))
            {
                env.require(Balance(alice, XRP(4000) - createFee));
                env.require(Balance(carol, XRP(5000)));
                env.close();

                auto const bigAllowance = 996'433;
                uint64_t const partialFeeCalc =
                    ((static_cast<uint64_t>(bigAllowance) * 1'000'000) / microDropsPerDrop) + 1;
                auto finishFee = env.current()->fees().base + partialFeeCalc;
                BEAST_EXPECT(finishFee.drops() > bigAllowance);

                // Intentional low value to test overflow handling
                auto finishFeeOverflow = drops(30);

                env(escrow::finish(alice, alice, seq),
                    Fee(finishFeeOverflow),  // enough if there's an overflow
                    escrow::Gas(bigAllowance),
                    Ter(telINSUF_FEE_P));

                env(escrow::finish(alice, alice, seq),
                    Fee(finishFee - 1),
                    escrow::Gas(bigAllowance),
                    Ter(telINSUF_FEE_P));

                env(escrow::finish(alice, alice, seq),
                    Fee(finishFee),
                    escrow::Gas(bigAllowance),
                    Ter(tesSUCCESS));

                auto const txMeta = env.meta();
                if (BEAST_EXPECT(txMeta->isFieldPresent(sfGasUsed)))
                {
                    BEAST_EXPECTS(
                        txMeta->getFieldU32(sfGasUsed) == allowance,
                        std::to_string(txMeta->getFieldU32(sfGasUsed)));
                }
                if (BEAST_EXPECT(txMeta->isFieldPresent(sfVMReturnCode)))
                {
                    BEAST_EXPECTS(
                        txMeta->getFieldI32(sfVMReturnCode) == 5,
                        std::to_string(txMeta->getFieldI32(sfVMReturnCode)));
                }

                BEAST_EXPECT(env.ownerCount(alice) == 0);
            }
        }
    }

    void
    testAllHostFunctions(FeatureBitset features)
    {
        testcase("Test all host functions");

        using namespace jtx;
        using namespace std::chrono;

        Account const alice{"alice"};
        Account const carol{"carol"};

        {
            Env env(*this, features);
            // create escrow
            env.fund(XRP(5000), alice, carol);
            auto const seq = env.seq(alice);
            BEAST_EXPECT(env.ownerCount(alice) == 0);
            auto escrowCreate = escrow::create(alice, carol, XRP(1000));
            XRPAmount const txnFees =
                env.current()->fees().base * 10 + kAllHostFunctionsWasmHex.size() / 2 * 5;
            env(escrowCreate,
                escrow::Bytecode(kAllHostFunctionsWasmHex),
                escrow::kFinishTime(env.now() + 11s),
                escrow::kCancelTime(env.now() + 100s),
                escrow::Data("1000000000"),  // 1000 XRP in drops
                Fee(txnFees));
            env.close();

            if (BEAST_EXPECT(
                    env.ownerCount(alice) == (1 + (kAllHostFunctionsWasmHex.size() / 2 / 500))))
            {
                env.require(Balance(alice, XRP(4000) - txnFees));
                env.require(Balance(carol, XRP(5000)));

                auto const allowance = 1'000'000;
                XRPAmount const finishFee = env.current()->fees().base +
                    (allowance * env.current()->fees().gasPrice) / microDropsPerDrop + 1;

                // FinishAfter time hasn't passed
                env(escrow::finish(carol, alice, seq),
                    escrow::Gas(allowance),
                    Fee(finishFee),
                    Ter(tecNO_PERMISSION));
                env.close();
                env.close();
                env.close();

                // reduce the destination balance
                env(pay(carol, alice, XRP(4500)));
                env.close();
                env.close();

                env(escrow::finish(alice, alice, seq),
                    escrow::Gas(allowance),
                    Fee(finishFee),
                    Ter(tesSUCCESS));

                auto const txMeta = env.meta();
                if (BEAST_EXPECT(txMeta && txMeta->isFieldPresent(sfGasUsed)))
                {
                    BEAST_EXPECTS(
                        txMeta->getFieldU32(sfGasUsed) == 48'433,
                        std::to_string(txMeta->getFieldU32(sfGasUsed)));
                }
                if (BEAST_EXPECT(txMeta->isFieldPresent(sfVMReturnCode)))
                    BEAST_EXPECT(txMeta->getFieldI32(sfVMReturnCode) == 1);

                env.close();
                BEAST_EXPECT(env.ownerCount(alice) == 0);
            }
        }
    }

    // TODO: this test is disabled until the all_keylets fixture is
    // regenerated; the call in run() is commented out.
    //
    // kAllKeyletsWasmHex was built against the old trace ABI, where the trace_*
    // host functions returned i32 rather than void, so the module is rejected
    // with temINVALID_BYTECODE and the escrow below is never created.
    //
    // Regenerating it is not just a rebuild: all_keylets/ is still pinned to
    // xrpl-wasm-stdlib @ "renames" and uses modules that moved on
    // xrpl-common-stdlib @ "error-and-trace" (core::keylets,
    // core::ledger_objects::*, core::types::*, and trace_data/DataRepr). The
    // fixture source has to be ported first, along with float_tests/ and
    // float_0/, which are stale for the same reason. The gas expectations here
    // will also need rechecking once the module runs again.
    //
    // Keylet coverage is not lost meanwhile: HostFuncImpl_test.cpp exercises
    // every keylet host function directly. What is missing is the end-to-end
    // path through a live escrow.
    void
    testKeyletHostFunctions(FeatureBitset features)
    {
        testcase("Test all keylet host functions");

        using namespace jtx;
        using namespace std::chrono;

        // TODO: create wasm module for all host functions
        Account const alice{"alice"};
        Account const carol{"carol"};

        {
            Env env{*this};
            env.fund(XRP(10000), alice, carol);

            BEAST_EXPECT(env.seq(alice) == 4);
            BEAST_EXPECT(env.ownerCount(alice) == 0);

            // base objects that need to be created first
            auto const tokenId = token::getNextID(env, alice, 0, tfTransferable);
            env(token::mint(alice, 0u), Txflags(tfTransferable));
            env(trust(alice, carol["USD"](1'000'000)));
            env.close();
            BEAST_EXPECT(env.seq(alice) == 6);
            BEAST_EXPECT(env.ownerCount(alice) == 2);

            // set up a bunch of objects to check their keylets
            AMM const amm(env, carol, XRP(10), carol["USD"](1000));
            env(check::create(alice, carol, XRP(100)));
            env(credentials::create(alice, alice, "termsandconditions"));
            env(delegate::set(alice, carol, {"TrustSet"}));
            env(deposit::auth(alice, carol));
            env(did::set(alice), did::Data("alice_did"));
            env(escrow::create(alice, carol, XRP(100)), escrow::kFinishTime(env.now() + 100s));
            MPTTester mptTester{env, alice, {.fund = false}};
            mptTester.create();
            mptTester.authorize({.account = carol});
            env(token::createOffer(carol, tokenId, XRP(100)), token::Owner(alice));
            env(offer(alice, carol["GBP"](0.1), XRP(100)));
            env(paychan::create(alice, carol, XRP(1000), 100s, alice.pk()));
            pdomain::Credentials const credentials{
                {.issuer = alice, .credType = "first credential"}};
            env(pdomain::setTx(alice, credentials));
            env(signers(alice, 1, {{carol, 1}}));
            env(ticket::create(alice, 1));
            Vault const vault{env};
            auto [tx, _keylet] = vault.create({.owner = alice, .asset = xrpIssue()});
            env(tx);
            env.close();

            BEAST_EXPECTS(env.ownerCount(alice) == 17, std::to_string(env.ownerCount(alice)));
            if (BEAST_EXPECTS(env.seq(alice) == 20, std::to_string(env.seq(alice))))
            {
                auto const seq = env.seq(alice);
                XRPAmount const txnFees =
                    env.current()->fees().base * 10 + kAllKeyletsWasmHex.size() / 2 * 5;
                env(escrow::create(alice, carol, XRP(1000)),
                    escrow::Bytecode(kAllKeyletsWasmHex),
                    escrow::kFinishTime(env.now() + 2s),
                    escrow::kCancelTime(env.now() + 100s),
                    Fee(txnFees));
                env.close();
                env.close();
                env.close();

                auto const allowance = 184'375;
                auto const finishFee = env.current()->fees().base +
                    (allowance * env.current()->fees().gasPrice) / microDropsPerDrop + 1;
                env(escrow::finish(carol, alice, seq), escrow::Gas(allowance), Fee(finishFee));
                env.close();

                auto const txMeta = env.meta();
                if (BEAST_EXPECT(txMeta && txMeta->isFieldPresent(sfGasUsed)))
                {
                    auto const gasUsed = txMeta->getFieldU32(sfGasUsed);
                    BEAST_EXPECTS(gasUsed == allowance, std::to_string(gasUsed));
                }
                BEAST_EXPECTS(env.ownerCount(alice) == 17, std::to_string(env.ownerCount(alice)));
            }
        }
    }

    void
    testLargeWasmModules(FeatureBitset features)
    {
        testcase("Test large wasm modules");

        using namespace jtx;
        using namespace std::chrono;
        using namespace wasm_constants;

        enum class ExpectedStatus { Success, Malformed, Crash };

        auto runTest = [&](std::vector<uint8_t> const& wasm,
                           std::optional<uint32_t> sizeLimit,
                           ExpectedStatus expectedStatus,
                           std::source_location const& loc = std::source_location::current()) {
            auto makeEnv = [&]() -> Env {
                if (sizeLimit)
                {
                    return Env(
                        *this,
                        envconfig([&sizeLimit](std::unique_ptr<Config> cfg) {
                            cfg->fees.bytecodeSizeLimit = *sizeLimit;
                            return cfg;
                        }),
                        features);
                }
                return Env(*this, features);
            };
            Env env = makeEnv();

            auto const alice = Account("alice");
            env.fund(XRP(1'000'000), alice);
            env.close();

            auto const wasmHex = strHex(wasm);
            try
            {
                env(escrow::create(alice, alice, XRP(1000)),
                    escrow::Bytecode(wasmHex),
                    escrow::kCancelTime(env.now() + 100s),
                    Fee(env.current()->fees().base * 10 + wasmHex.size() / 2 * 5),
                    Ter(expectedStatus == ExpectedStatus::Success ? TER{tesSUCCESS}
                                                                  : TER{temMALFORMED}));
                if (expectedStatus == ExpectedStatus::Crash)
                {
                    fail("Expected crash", loc.file_name(), loc.line());
                }
                else
                {
                    pass();
                }
            }
            catch (std::exception const& e)
            {
                if (expectedStatus == ExpectedStatus::Crash)
                {
                    pass();
                }
                else
                {
                    fail(e.what(), loc.file_name(), loc.line());
                }
            }
        };

        // Table-driven test cases
        struct TestCase
        {
            enum class BlobType { Code, Data };
            BlobType type;
            uint32_t size;
            std::optional<uint32_t> sizeLimit;
            ExpectedStatus expected;
        };

        std::vector<TestCase> const testCases = {
            // Code blob tests
            {.type = TestCase::BlobType::Code,
             .size = 99'950,
             .sizeLimit = std::nullopt,
             .expected = ExpectedStatus::Success},  // just under 100kb
            {.type = TestCase::BlobType::Code,
             .size = 99'955,
             .sizeLimit = std::nullopt,
             .expected = ExpectedStatus::Malformed},  // just over 100kb
            {.type = TestCase::BlobType::Code,
             .size = 200'000,
             .sizeLimit = 10'000'000,
             .expected = ExpectedStatus::Success},  // ~200kb
            {.type = TestCase::BlobType::Code,
             .size = 490'000,
             .sizeLimit = 10'000'000,
             .expected = ExpectedStatus::Success},  // just under 1MB JSON
            {.type = TestCase::BlobType::Code,
             .size = 999'999,
             .sizeLimit = 10'000'000,
             .expected = ExpectedStatus::Crash},  // just over 1MB JSON
            // Data blob tests
            {.type = TestCase::BlobType::Data,
             .size = 99'939,
             .sizeLimit = std::nullopt,
             .expected = ExpectedStatus::Success},  // just under 100kb
            {.type = TestCase::BlobType::Data,
             .size = 99'941,
             .sizeLimit = std::nullopt,
             .expected = ExpectedStatus::Malformed},  // just over 100kb
            {.type = TestCase::BlobType::Data,
             .size = 200'000,
             .sizeLimit = 10'000'000,
             .expected = ExpectedStatus::Success},  // ~200kb
            {.type = TestCase::BlobType::Data,
             .size = 490'000,
             .sizeLimit = 10'000'000,
             .expected = ExpectedStatus::Success},  // just under 1MB JSON
            {.type = TestCase::BlobType::Data,
             .size = 999'950,
             .sizeLimit = 10'000'000,
             .expected = ExpectedStatus::Crash},  // just over 1MB JSON
        };

        for (auto const& tc : testCases)
        {
            auto const wasm = tc.type == TestCase::BlobType::Code ? generateCodeBlob(tc.size)
                                                                  : generateDataBlob(tc.size);
            runTest(wasm, tc.sizeLimit, tc.expected);
        }
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testCreateBytecodePreflight(features);
        testFinishWasmFailures(features);
        testBytecode(features);
        testUpdateDataOnFailure(features);
        testFees(features);

        // TODO: Update module with new host functions
        testAllHostFunctions(features);
        // TODO: re-enable once the all_keylets fixture is regenerated (see
        // testKeyletHostFunctions)
        // testKeyletHostFunctions(features);

        testLargeWasmModules(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{testableAmendments()};
        testWithFeats(all);
    }
};

BEAST_DEFINE_TESTSUITE(EscrowSmart, app, xrpl);

}  // namespace xrpl::test
