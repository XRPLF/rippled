#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/escrow.h>
#include <test/jtx/fee.h>
#include <test/jtx/noop.h>
#include <test/jtx/ter.h>

#include <xrpld/core/Config.h>

#include <xrpl/basics/StringUtilities.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/StartUpType.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace xrpl::test {

/**
 * Smart Escrow's `Bytecode`, `Data` and `Gas` fields.
 *
 *  The WASM engine that runs an escrow's bytecode is not in this build, so the
 *  `Bytecode` field is opaque here: `EscrowCreate` weighs its size and nothing
 *  reads it, and `EscrowFinish` has nothing to run it with. Everything around
 *  the run is testable, and is tested below - the preflight and preclaim rules,
 *  the fee the bytecode costs, the owner reserve it occupies, and the refund of
 *  that reserve.
 *
 *  What waits on the engine: a successful finish, tecBYTECODE_REJECTED, the
 *  gas and return code the run reports in the transaction metadata, and the
 *  `Data` a rejected contract leaves behind.
 */
struct EscrowSmart_test : public beast::unit_test::Suite
{
    // A blob of `bytes` bytes, as hex. Opens with the WASM preamble - "\0asm"
    // and version 1 - so a reader can see what the field is meant to carry,
    // but nothing in this build parses it.
    static std::string
    bytecodeOfSize(std::size_t bytes)
    {
        static std::string const preamble = "0061736D01000000";
        std::string hex = preamble;
        hex.append(bytes * 2 - preamble.size(), 'A');
        return hex;
    }

    // What EscrowCreate charges for `bytecodeHex`: ten base fees, plus five
    // drops per byte.
    static XRPAmount
    createFeeFor(jtx::Env const& env, std::string const& bytecodeHex)
    {
        return env.current()->fees().base * 10 + bytecodeHex.size() / 2 * 5;
    }

    // What EscrowFinish charges for `gas`.
    static XRPAmount
    finishFeeFor(jtx::Env const& env, std::uint64_t gas)
    {
        return env.current()->fees().base +
            (gas * env.current()->fees().gasPrice) / microDropsPerDrop + 1;
    }

    void
    testCreatePreflight(FeatureBitset features)
    {
        testcase("EscrowCreate preflight");

        using namespace jtx;
        using namespace std::chrono;

        Account const alice{"alice"};
        Account const carol{"carol"};

        auto const bytecode = bytecodeOfSize(64);

        {
            // featureSmartEscrow disabled
            Env env(*this, features - featureSmartEscrow);
            env.fund(XRP(5000), alice, carol);
            XRPAmount const txnFees = env.current()->fees().base + 1000;
            auto const escrowCreate = escrow::create(alice, carol, XRP(1000));
            env(escrowCreate,
                escrow::Bytecode(bytecode),
                escrow::kCancelTime(env.now() + 100s),
                Fee(txnFees),
                Ter(temDISABLED));
            env.close();

            env(escrowCreate,
                escrow::Bytecode(bytecode),
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
            env.fund(XRP(5000), alice, carol);

            auto const escrowCreate = escrow::create(alice, carol, XRP(500));

            // 11-byte string
            std::string const longBytecode = "00112233445566778899AA";
            env(escrowCreate,
                escrow::Bytecode(longBytecode),
                escrow::kCancelTime(env.now() + 100s),
                Fee(txnFees),
                Ter(temMALFORMED));
            env.close();
        }

        {
            // gas limit set to 0
            Env env(
                *this,
                envconfig([](std::unique_ptr<Config> cfg) {
                    // WASM runtime disabled
                    cfg->fees.gasLimit = 0;
                    return cfg;
                }),
                features);
            XRPAmount const txnFees = env.current()->fees().base + 1000;
            env.fund(XRP(5000), alice, carol);

            auto const escrowCreate = escrow::create(alice, carol, XRP(500));

            env(escrowCreate,
                escrow::Bytecode(bytecode),
                escrow::kCancelTime(env.now() + 100s),
                Fee(txnFees),
                Ter(temTEMP_DISABLED));
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
            env.fund(XRP(5000), alice, carol);

            auto const escrowCreate = escrow::create(alice, carol, XRP(500));

            // 1-byte string
            env(escrowCreate,
                escrow::Bytecode("AA"),
                escrow::kCancelTime(env.now() + 100s),
                Fee(txnFees),
                Ter(temTEMP_DISABLED));
            env.close();

            env(escrowCreate,
                escrow::Bytecode(bytecode),
                escrow::kCancelTime(env.now() + 100s),
                Fee(txnFees),
                Ter(temTEMP_DISABLED));
            env.close();
        }

        {
            // Data without Bytecode
            Env env(*this, features);
            XRPAmount const txnFees = env.current()->fees().base + 100000;
            env.fund(XRP(5000), alice, carol);

            auto const escrowCreate = escrow::create(alice, carol, XRP(500));

            std::string const data(4, 'A');
            env(escrowCreate,
                escrow::Data(data),
                escrow::kFinishTime(env.now() + 100s),
                Fee(txnFees),
                Ter(temMALFORMED));
            env.close();
        }

        {
            // Data > max length
            Env env(*this, features);
            XRPAmount const txnFees = env.current()->fees().base + 100000;
            env.fund(XRP(5000), alice, carol);

            auto const escrowCreate = escrow::create(alice, carol, XRP(500));

            // string of length (kMaxWasmDataLength + 1) * 2
            std::string const longData((kMaxWasmDataLength + 1) * 2, 'B');
            env(escrowCreate,
                escrow::Data(longData),
                escrow::Bytecode(bytecode),
                escrow::kCancelTime(env.now() + 100s),
                Fee(txnFees),
                Ter(temMALFORMED));
            env.close();
        }

        // Bytecode joins FinishAfter and Condition as a way to say how the
        // escrow completes, but it always needs a CancelAfter: nothing else
        // can free the funds if the contract never accepts.
        Env env(
            *this,
            envconfig([](std::unique_ptr<Config> cfg) {
                cfg->startUp = StartUpType::Fresh;
                return cfg;
            }),
            features);
        env.fund(XRP(5000), alice, carol);

        auto const escrowCreate = escrow::create(alice, carol, XRP(500));
        XRPAmount const txnFees = createFeeFor(env, bytecode);

        // Success situations
        {
            // Bytecode + CancelAfter
            env(escrowCreate,
                escrow::Bytecode(bytecode),
                escrow::kCancelTime(env.now() + 20s),
                Fee(txnFees));
            env.close();
        }
        {
            // Bytecode + Condition + CancelAfter
            env(escrowCreate,
                escrow::Bytecode(bytecode),
                escrow::kCancelTime(env.now() + 30s),
                escrow::kCondition(escrow::kCb1),
                Fee(txnFees));
            env.close();
        }
        {
            // Bytecode + FinishAfter + CancelAfter
            env(escrowCreate,
                escrow::Bytecode(bytecode),
                escrow::kCancelTime(env.now() + 40s),
                escrow::kFinishTime(env.now() + 2s),
                Fee(txnFees));
            env.close();
        }
        {
            // Bytecode + Data + CancelAfter
            env(escrowCreate,
                escrow::Bytecode(bytecode),
                escrow::Data("00112233"),
                escrow::kCancelTime(env.now() + 50s),
                Fee(txnFees));
            env.close();
        }

        // Failure situations (i.e. all other combinations)
        {
            // only Bytecode
            env(escrowCreate, escrow::Bytecode(bytecode), Fee(txnFees), Ter(temBAD_EXPIRATION));
            env.close();
        }
        {
            // Bytecode + FinishAfter
            env(escrowCreate,
                escrow::Bytecode(bytecode),
                escrow::kFinishTime(env.now() + 2s),
                Fee(txnFees),
                Ter(temBAD_EXPIRATION));
            env.close();
        }
        {
            // Bytecode + Condition
            env(escrowCreate,
                escrow::Bytecode(bytecode),
                escrow::kCondition(escrow::kCb1),
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
                escrow::Bytecode(bytecode),
                escrow::kCancelTime(env.now() + 70s),
                Fee(txnFees - 1),
                Ter(telINSUF_FEE_P));
            env.close();
        }
    }

    void
    testCreateFeeAndReserve(FeatureBitset features)
    {
        testcase("EscrowCreate fee and reserve");

        using namespace jtx;
        using namespace std::chrono;

        Account const alice{"alice"};
        Account const carol{"carol"};

        {
            // The fee scales with the bytecode: ten base fees plus five drops
            // per byte, and a drop short of it is refused.
            Env env(*this, features);
            env.fund(XRP(5000), alice, carol);
            env.close();

            auto const bytecode = bytecodeOfSize(1'000);
            auto const escrowCreate = escrow::create(alice, carol, XRP(500));
            auto const fee = createFeeFor(env, bytecode);
            BEAST_EXPECT(fee == env.current()->fees().base * 10 + 5'000);

            env(escrowCreate,
                escrow::Bytecode(bytecode),
                escrow::kCancelTime(env.now() + 100s),
                Fee(fee - 1),
                Ter(telINSUF_FEE_P));
            env.close();

            auto const seq = env.seq(alice);
            env(escrowCreate,
                escrow::Bytecode(bytecode),
                escrow::Data("00112233"),
                escrow::kCancelTime(env.now() + 100s),
                Fee(fee));
            env.close();

            // The ledger entry carries both fields.
            auto const sle = env.le(keylet::escrow(alice, SeqProxy::rawSequence(seq)));
            if (BEAST_EXPECT(sle))
            {
                BEAST_EXPECT(sle->isFieldPresent(sfBytecode));
                BEAST_EXPECT(strHex(sle->getFieldVL(sfBytecode)) == bytecode);
                BEAST_EXPECT(strHex(sle->getFieldVL(sfData)) == "00112233");
            }
        }

        {
            // The reserve is one owner increment per 500 bytes beyond the
            // first 500, on top of the increment every escrow costs.
            Env env(*this, features);
            env.fund(XRP(5000), alice, carol);
            env.close();

            std::uint32_t expectedOwnerCount = 0;
            for (auto const size :
                 {std::size_t{64},
                  std::size_t{500},
                  std::size_t{501},
                  std::size_t{1'000},
                  std::size_t{1'001}})
            {
                auto const bytecode = bytecodeOfSize(size);
                env(escrow::create(alice, carol, XRP(100)),
                    escrow::Bytecode(bytecode),
                    escrow::kCancelTime(env.now() + 1000s),
                    Fee(createFeeFor(env, bytecode)));
                env.close();

                expectedOwnerCount += 1 + size / 500;
                BEAST_EXPECTS(
                    env.ownerCount(alice) == expectedOwnerCount,
                    std::to_string(size) + " bytes: " + std::to_string(env.ownerCount(alice)));
            }
        }

        {
            // A bytecode escrow the owner cannot reserve is refused, where the
            // same escrow without bytecode would have been accepted.
            Env env(*this, features);
            // Base 200 XRP + 50 XRP per owner object, from the unit-test
            // config. Enough for one owner object, not for three.
            env.fund(XRP(300), alice, carol);
            env.close();

            auto const bytecode = bytecodeOfSize(1'000);
            env(escrow::create(alice, carol, XRP(20)),
                escrow::Bytecode(bytecode),
                escrow::kCancelTime(env.now() + 100s),
                Fee(createFeeFor(env, bytecode)),
                Ter(tecINSUFFICIENT_RESERVE));
            env.close();
            BEAST_EXPECT(env.ownerCount(alice) == 0);

            // The same escrow without bytecode costs one increment and fits.
            env(escrow::create(alice, carol, XRP(20)),
                escrow::kFinishTime(env.now() + 10s),
                escrow::kCancelTime(env.now() + 100s));
            env.close();
            BEAST_EXPECT(env.ownerCount(alice) == 1);
        }
    }

    void
    testFinishPreflight(FeatureBitset features)
    {
        testcase("EscrowFinish preflight");

        using namespace jtx;
        using namespace std::chrono;

        Account const alice{"alice"};
        Account const carol{"carol"};

        {
            // featureSmartEscrow disabled
            Env env(*this, features - featureSmartEscrow);
            env.fund(XRP(5000), alice, carol);
            env(escrow::finish(carol, alice, 1),
                Fee(env.current()->fees().base + 1000),
                escrow::Gas(4),
                Ter(temDISABLED));
            env.close();
        }

        {
            // Gas > gas limit
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

            auto const gas = 1'001;
            env(escrow::finish(carol, alice, 1),
                Fee(finishFeeFor(env, gas)),
                escrow::Gas(gas),
                Ter(temBAD_LIMIT));
        }

        {
            // Gas of 0
            Env env(*this, features);
            env.fund(XRP(5000), alice, carol);
            env.close();
            env(escrow::finish(carol, alice, 1),
                Fee(env.current()->fees().base + 1000),
                escrow::Gas(0),
                Ter(temBAD_LIMIT));
        }

        {
            // WASM compute disabled after the escrow was created.
            //
            // The Escrow ledger object is added by hand, bypassing normal
            // transaction processing: the config cannot be changed mid-test,
            // and a Smart Escrow cannot be created while the gas limit is 0.
            Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
                        cfg->fees.gasLimit = 0;
                        return cfg;
                    })};

            env.fund(XRP(1000), alice);
            env.close();

            auto const seq = env.seq(alice);
            auto const keylet = keylet::escrow(alice.id(), SeqProxy::rawSequence(seq));
            env(noop(alice));  // to align sequence numbers

            env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal j) {
                auto sle = std::make_shared<SLE>(keylet);

                sle->setAccountID(sfAccount, alice.id());
                sle->setFieldAmount(sfAmount, XRP(100));
                sle->setFieldU32(sfCancelAfter, 110);
                sle->setAccountID(sfDestination, alice.id());
                sle->setFieldVL(sfBytecode, strUnHex(bytecodeOfSize(64)).value());
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
    }

    void
    testFinishBytecodePairing(FeatureBitset features)
    {
        testcase("EscrowFinish Bytecode and Gas must agree");

        using namespace jtx;
        using namespace std::chrono;

        Account const alice{"alice"};
        Account const carol{"carol"};

        Env env(*this, features);

        // Run past the flag ledger so that a Fee change vote occurs and
        // updates FeeSettings. (It also activates all supported amendments.)
        for (auto i = env.current()->seq(); i <= 257; ++i)
            env.close();

        env.fund(XRP(5000), alice, carol);

        auto const bytecode = bytecodeOfSize(64);
        auto const seq = env.seq(alice);
        env(escrow::create(alice, carol, XRP(500)),
            escrow::Bytecode(bytecode),
            escrow::kCancelTime(env.now() + 100s),
            Fee(createFeeFor(env, bytecode)));
        env.close();

        {
            // Bytecode on the escrow, no Gas on the finish
            env(escrow::finish(carol, alice, seq), Ter(tefBYTECODE_NOT_INCLUDED));
        }

        {
            // Gas on the finish, no Bytecode on the escrow
            auto const plainSeq = env.seq(alice);
            env(escrow::create(alice, carol, XRP(500)),
                escrow::kFinishTime(env.now() + 10s),
                escrow::kCancelTime(env.now() + 100s));
            env.close();

            auto const gas = 100;
            env(escrow::finish(carol, alice, plainSeq),
                Fee(finishFeeFor(env, gas)),
                escrow::Gas(gas),
                Ter(tefNO_BYTECODE));
        }
    }

    void
    testFinishFee(FeatureBitset features)
    {
        testcase("EscrowFinish fee");

        using namespace jtx;
        using namespace std::chrono;

        Account const alice{"alice"};
        Account const carol{"carol"};

        Env env(
            *this,
            envconfig([](std::unique_ptr<Config> cfg) {
                cfg->fees.gasPrice = 1'000'000;  // 1 drop per gas
                return cfg;
            }),
            features);
        // Run past the flag ledger so that a Fee change vote occurs and
        // updates FeeSettings. (It also activates all supported amendments.)
        for (auto i = env.current()->seq(); i <= 257; ++i)
            env.close();

        env.fund(XRP(5000), alice, carol);

        auto const bytecode = bytecodeOfSize(64);
        auto const seq = env.seq(alice);
        env(escrow::create(alice, carol, XRP(1000)),
            escrow::Bytecode(bytecode),
            escrow::kCancelTime(env.now() + 100s),
            Fee(createFeeFor(env, bytecode)));
        env.close();

        // A large gas allowance costs more than the allowance itself, and does
        // not wrap.
        auto const gas = 996'433;
        auto const fee = finishFeeFor(env, gas);
        BEAST_EXPECT(fee.drops() > gas);

        // Intentional low value to test overflow handling
        env(escrow::finish(carol, alice, seq),
            Fee(drops(30)),
            escrow::Gas(gas),
            Ter(telINSUF_FEE_P));

        env(escrow::finish(carol, alice, seq), Fee(fee - 1), escrow::Gas(gas), Ter(telINSUF_FEE_P));

        // TODO(SmartEscrow): tesSUCCESS once the WASM engine lands. Until then
        // the fee is charged and the run reports that it could not happen.
        env(escrow::finish(carol, alice, seq),
            Fee(fee),
            escrow::Gas(gas),
            Ter(tecFAILED_PROCESSING));
    }

    void
    testFinishWithoutEngine(FeatureBitset features)
    {
        testcase("EscrowFinish has no WASM engine");

        using namespace jtx;
        using namespace std::chrono;

        Account const alice{"alice"};
        Account const carol{"carol"};

        Env env(*this, features);
        env.fund(XRP(5000), alice, carol);
        env.close();

        auto const bytecode = bytecodeOfSize(1'000);
        auto const seq = env.seq(alice);
        env(escrow::create(alice, carol, XRP(500)),
            escrow::Bytecode(bytecode),
            escrow::kCancelTime(env.now() + 20s),
            Fee(createFeeFor(env, bytecode)));
        env.close();
        BEAST_EXPECT(env.ownerCount(alice) == 3);

        // TODO(SmartEscrow): the contract decides this once the WASM engine
        // lands - tesSUCCESS on accept, tecBYTECODE_REJECTED on reject.
        auto const gas = 1'000;
        env(escrow::finish(carol, alice, seq),
            Fee(finishFeeFor(env, gas)),
            escrow::Gas(gas),
            Ter(tecFAILED_PROCESSING));
        env.close();

        // The escrow and its reserve survive.
        BEAST_EXPECT(env.le(keylet::escrow(alice, SeqProxy::rawSequence(seq))));
        BEAST_EXPECT(env.ownerCount(alice) == 3);

        // Cancelling returns the whole reserve, not just one increment.
        env.close(30s);
        env(escrow::cancel(carol, alice, seq));
        env.close();
        BEAST_EXPECT(!env.le(keylet::escrow(alice, SeqProxy::rawSequence(seq))));
        BEAST_EXPECT(env.ownerCount(alice) == 0);
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testCreatePreflight(features);
        testCreateFeeAndReserve(features);
        testFinishPreflight(features);
        testFinishBytecodePairing(features);
        testFinishFee(features);
        testFinishWithoutEngine(features);
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
