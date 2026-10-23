#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/batch.h>
#include <test/jtx/fee.h>
#include <test/jtx/multisign.h>
#include <test/jtx/noop.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/xchain_bridge.h>

#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PayChan.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/transactors/account/SetRegularKey.h>

#include <chrono>

namespace xrpl {
namespace test {

// Every ledger path that accepts a Dilithium key accepts it only while featureQuantum is enabled.
class QuantumAmendment_test : public beast::unit_test::Suite
{
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

        auto claim = [&](STAmount const& amount) {
            Serializer msg;
            serializePayChanAuthorization(msg, chan, amount.xrp());
            auto const sig = sign(keys.first, keys.second, msg.slice());
            return paychan::claim(bob, chan, amount, amount, Slice(sig), keys.first);
        };
        auto channelBalance = [&] {
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
                BEAST_EXPECT(fee == XRPAmount{0});
            else
                BEAST_EXPECT(fee == Transactor::calculateBaseFee(*env.current(), tx));
        }
    }

    void
    testXChainAttestation()
    {
        using namespace jtx;
        testcase("XChain attestation with a Dilithium PublicKey");
        for (bool const quantum : {false, true})
        {
            Env env{*this, features(quantum)};
            Account const alice{"alice"};
            Account const door{"door"};
            Account const attester{"attester", KeyType::Dilithium};
            env.fund(XRP(1000), alice, door);
            env.close();

            auto const jvb = bridge(door, xrpIssue(), Account::kMaster, xrpIssue());
            auto const att = claimAttestation(
                alice, jvb, alice, XRP(1), alice, true, 1, std::nullopt, Signer{attester});
            env(att, Ter(std::ignore));
            BEAST_EXPECT((env.ter() == temMALFORMED) != quantum);
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
        testXChainAttestation();
        testSTTxCheckSign();
    }
};

BEAST_DEFINE_TESTSUITE(QuantumAmendment, app, xrpl);

}  // namespace test
}  // namespace xrpl
