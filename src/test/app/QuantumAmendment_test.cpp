#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/batch.h>
#include <test/jtx/fee.h>
#include <test/jtx/multisign.h>
#include <test/jtx/noop.h>
#include <test/jtx/pay.h>
#include <test/jtx/paychan.h>
#include <test/jtx/sig.h>
#include <test/jtx/ter.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/PayChan.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/applySteps.h>
#include <xrpl/tx/transactors/account/SetRegularKey.h>

#include <optional>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace xrpl::test {

// Every ledger path that accepts a Dilithium key accepts it only while featureQuantum is enabled.
class QuantumAmendment_test : public beast::unit_test::Suite
{
    // Exposes Transactor's protected signer checks.
    struct SignerChecks : Transactor
    {
        using Transactor::checkMultiSign;
        using Transactor::checkSign;
    };

    static FeatureBitset
    features(bool quantum)
    {
        auto const all = jtx::testableAmendments();
        return quantum ? all : all - featureQuantum;
    }

    void
    testPayment()
    {
        using namespace jtx;
        testcase("Dilithium-signed Payment");
        for (bool const quantum : {false, true})
        {
            Env env{*this, features(quantum)};
            Account const alice{"alice", KeyType::Dilithium};
            Account const bob{"bob"};
            env.fund(XRP(1000), noripple(alice), bob);
            env.close();

            env(pay(alice, bob, XRP(10)), Ter(std::ignore));
            env.close();
            BEAST_EXPECT(env.balance(bob) == (quantum ? XRP(1010) : XRP(1000)));
        }
    }

    void
    testMultiSign()
    {
        using namespace jtx;
        testcase("Dilithium multisign signer");
        for (bool const quantum : {false, true})
        {
            Env env{*this, features(quantum)};
            Account const alice{"alice"};
            Account const carol{"carol", KeyType::Dilithium};
            env.fund(XRP(1000), alice);
            env.close();
            env(signers(alice, 1, {{carol, 1}}));
            env.close();

            auto const seq = env.seq(alice);
            env(noop(alice), Msig(carol), Fee(2 * env.current()->fees().base), Ter(std::ignore));
            env.close();
            BEAST_EXPECT(env.seq(alice) == (quantum ? seq + 1 : seq));
        }
    }

    void
    testBatchSigner()
    {
        using namespace jtx;
        testcase("Dilithium batch signer");
        for (bool const quantum : {false, true})
        {
            Env env{*this, features(quantum)};
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const carol{"carol", KeyType::Dilithium};
            env.fund(XRP(1000), alice, bob, noripple(carol));
            env.close();

            auto const batchFee = batch::calcBatchFee(env, 1, 2);
            auto const seq = env.seq(alice);
            env(batch::outer(alice, seq, batchFee, tfAllOrNothing),
                batch::Inner(pay(alice, bob, XRP(1)), seq + 1),
                batch::Inner(pay(carol, bob, XRP(1)), env.seq(carol)),
                batch::Sig(carol),
                Ter(std::ignore));
            env.close();
            BEAST_EXPECT(env.balance(bob) == (quantum ? XRP(1002) : XRP(1000)));
        }
    }

    void
    testPayChanCreate()
    {
        using namespace jtx;
        using namespace std::chrono_literals;
        testcase("PaymentChannelCreate with a Dilithium PublicKey");
        for (bool const quantum : {false, true})
        {
            Env env{*this, features(quantum)};
            Account const alice{"alice"};
            Account const bob{"bob"};
            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const keys = randomKeyPair(KeyType::Dilithium);
            auto const seq = env.seq(alice);
            env(paychan::create(alice, bob, XRP(100), 100s, keys.first),
                Ter(quantum ? TER{tesSUCCESS} : TER{temMALFORMED}));
            env.close();
            auto const chan =
                env.le(keylet::payChannel(alice.id(), bob.id(), SeqProxy::rawSequence(seq)));
            BEAST_EXPECT(static_cast<bool>(chan) == quantum);
        }
    }

    void
    testPayChanClaim()
    {
        using namespace jtx;
        using namespace std::chrono_literals;
        testcase("PaymentChannelClaim signed by a Dilithium key");

        Env env{*this, features(true)};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(1000), alice, bob);
        env.close();

        auto const keys = randomKeyPair(KeyType::Dilithium);
        auto const chan = paychan::channel(alice, bob, env.seq(alice));
        env(paychan::create(alice, bob, XRP(100), 100s, keys.first));
        env.close();

        auto const claim = [&](STAmount const& amount) {
            Serializer msg;
            serializePayChanAuthorization(msg, chan, amount.xrp());
            auto const sig = sign(keys.first, keys.second, msg.slice());
            return paychan::claim(bob, chan, amount, amount, Slice(sig), keys.first);
        };
        auto const channelBalance = [&] {
            auto const sle = env.le(Keylet{ltPAYCHAN, chan});
            return sle ? (*sle)[sfBalance] : STAmount{};
        };

        env.disableFeature(featureQuantum);
        env.close();
        env(claim(XRP(10)), Ter(temMALFORMED));
        env.close();
        BEAST_EXPECT(channelBalance() == XRP(0));

        env.enableFeature(featureQuantum);
        env.close();
        env(claim(XRP(20)));
        env.close();
        BEAST_EXPECT(channelBalance() == XRP(20));
    }

    void
    testMessageKey()
    {
        using namespace jtx;
        testcase("AccountSet with a Dilithium MessageKey");
        for (bool const quantum : {false, true})
        {
            Env env{*this, features(quantum)};
            Account const alice{"alice"};
            env.fund(XRP(1000), alice);
            env.close();

            auto const key = randomKeyPair(KeyType::Dilithium).first;
            auto jv = noop(alice);
            jv[sfMessageKey.jsonName] = strHex(key.slice());
            env(jv, Ter(quantum ? TER{tesSUCCESS} : TER{telBAD_PUBLIC_KEY}));
            env.close();
            auto const sle = env.le(alice);
            BEAST_EXPECT(sle && sle->isFieldPresent(sfMessageKey) == quantum);
        }
    }

    void
    testSetRegularKeyFee()
    {
        using namespace jtx;
        testcase("SetRegularKey fee for a Dilithium master key");
        for (bool const quantum : {false, true})
        {
            Env env{*this, features(quantum)};
            Account const alice{"alice", KeyType::Dilithium};
            env.fund(XRP(1000), noripple(alice));
            env.close();

            STTx const tx(ttREGULAR_KEY_SET, [&alice](auto& obj) {
                obj.setAccountID(sfAccount, alice.id());
                obj.setFieldAmount(sfFee, STAmount{XRPAmount{10}});
                obj.setFieldU32(sfSequence, 1);
                obj.setFieldVL(sfSigningPubKey, alice.pk().slice());
            });
            auto const fee = SetRegularKey::calculateBaseFee(*env.current(), tx);
            if (quantum)
            {
                BEAST_EXPECT(fee == XRPAmount{0});
            }
            else
            {
                BEAST_EXPECT(fee == Transactor::calculateBaseFee(*env.current(), tx));
            }
        }
    }

    void
    testSTTxCheckSign()
    {
        testcase("STTx::checkSign of a Dilithium signature");
        auto const keys = randomKeyPair(KeyType::Dilithium);
        STTx tx(ttACCOUNT_SET, [&keys](auto& obj) {
            obj.setAccountID(sfAccount, calcAccountID(keys.first));
            obj.setFieldAmount(sfFee, STAmount{XRPAmount{10}});
            obj.setFieldU32(sfSequence, 1);
            obj.setFieldVL(sfSigningPubKey, keys.first.slice());
        });
        tx.sign(keys.first, keys.second);

        std::unordered_set<uint256, beast::Uhash<>> const none;
        std::unordered_set<uint256, beast::Uhash<>> const quantum{featureQuantum};
        BEAST_EXPECT(!tx.checkSign(Rules{none}));
        BEAST_EXPECT(tx.checkSign(Rules{quantum}));
    }

    void
    testTransactorSignerChecks()
    {
        using namespace jtx;
        testcase("Transactor signer checks of a Dilithium key");
        for (bool const quantum : {false, true})
        {
            Env env{*this, features(quantum)};
            Account const alice{"alice", KeyType::Dilithium};
            Account const bob{"bob"};
            Account const carol{"carol", KeyType::Dilithium};
            env.fund(XRP(1000), noripple(alice), bob, noripple(carol));
            env.close();
            env(signers(bob, 1, {{carol, 1}}));
            env.close();

            // alice signs with her Dilithium master key
            STTx const single(ttACCOUNT_SET, [&alice](auto& obj) {
                obj.setAccountID(sfAccount, alice.id());
                obj.setFieldAmount(sfFee, STAmount{XRPAmount{10}});
                obj.setFieldU32(sfSequence, 1);
                obj.setFieldVL(sfSigningPubKey, alice.pk().slice());
            });
            auto const singleResult = SignerChecks::checkSign(
                *env.current(), TapNone, std::nullopt, alice.id(), single, env.journal);
            BEAST_EXPECT(quantum ? singleResult == tesSUCCESS : singleResult == tefBAD_AUTH);

            // carol multisigns for bob with her Dilithium master key
            STObject signer(sfSigner);
            signer.setAccountID(sfAccount, carol.id());
            signer.setFieldVL(sfSigningPubKey, carol.pk().slice());
            signer.setFieldVL(sfTxnSignature, Blob{0x01});
            STArray signerArray(sfSigners, 1);
            signerArray.push_back(std::move(signer));
            STTx const multi(ttACCOUNT_SET, [&bob, &signerArray](auto& obj) {
                obj.setAccountID(sfAccount, bob.id());
                obj.setFieldAmount(sfFee, STAmount{XRPAmount{20}});
                obj.setFieldU32(sfSequence, 1);
                obj.setFieldVL(sfSigningPubKey, Slice{});
                obj.setFieldArray(sfSigners, signerArray);
            });
            auto const multiResult =
                SignerChecks::checkMultiSign(*env.current(), TapNone, bob.id(), multi, env.journal);
            BEAST_EXPECT(quantum ? multiResult == tesSUCCESS : multiResult == tefBAD_SIGNATURE);
        }
    }

    void
    testLoanSetCounterparty()
    {
        using namespace jtx;
        testcase("LoanSet counterparty with a Dilithium key");
        for (bool const quantum : {false, true})
        {
            Env env{*this, features(quantum)};
            Account const lender{"lender"};
            Account const borrower{"borrower", KeyType::Dilithium};
            env.fund(XRP(1000), lender, noripple(borrower));
            env.close();

            // preflight runs the key check; the submit RPC's signature check would refuse first
            auto const jt = env.jt(
                loan::set(lender, UInt256{1}, Number{100}), Sig(sfCounterpartySignature, borrower));
            if (!BEAST_EXPECT(jt.stx))
                continue;
            auto const result =
                preflight(env.app(), env.current()->rules(), *jt.stx, TapNone, env.journal);
            BEAST_EXPECTS((result.ter == temBAD_SIGNATURE) != quantum, transToken(result.ter));
        }
    }

public:
    void
    run() override
    {
        testPayment();
        testMultiSign();
        testBatchSigner();
        testPayChanCreate();
        testPayChanClaim();
        testMessageKey();
        testSetRegularKeyFee();
        testSTTxCheckSign();
        testTransactorSignerChecks();
        testLoanSetCounterparty();
    }
};

BEAST_DEFINE_TESTSUITE(QuantumAmendment, app, xrpl);

}  // namespace xrpl::test
