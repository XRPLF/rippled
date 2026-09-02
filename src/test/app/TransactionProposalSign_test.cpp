#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/delegate.h>
#include <test/jtx/multisign.h>
#include <test/jtx/pay.h>
#include <test/jtx/proposal.h>
#include <test/jtx/sig.h>
#include <test/jtx/tags.h>
#include <test/jtx/ter.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <string>

namespace xrpl::test {

struct TransactionProposalSign_test : public beast::unit_test::Suite
{
    json::Value
    proposedJson(jtx::Env const& env, jtx::Account const& target, std::uint32_t ticketSeq)
    {
        auto const sle = jtx::proposal::entry(env, target, ticketSeq);
        BEAST_EXPECT(sle);
        return sle->getFieldObject(sfProposedTransaction).getJson(JsonOptions::Values::None);
    }

    void
    testDisabled(FeatureBitset features)
    {
        testcase("amendment disabled");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features - featureCosign};

        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        json::Value jv;
        jv[jss::TransactionType] = "TransactionProposalSign";
        jv[jss::Account] = alice.human();
        jv[sfProposalID.jsonName] = to_string(proposal::id(alice, 1));
        jv[sfSigningFor.jsonName] = alice.human();
        auto& ps = jv[sfProposalSignature.jsonName];
        ps[jss::Account] = alice.human();
        ps[jss::SigningPubKey] = strHex(alice.pk().slice());
        ps[jss::TxnSignature] = "00";
        env(jv, Ter(temDISABLED));
        env.close();
    }

    void
    testPreflight(FeatureBitset features)
    {
        testcase("preflight");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const target{"target"};
        Account const bob{"bob"};
        Account const ceo{"ceo"};
        env.fund(XRP(10000), target, bob, ceo);
        env.close();

        env(signers(target, 1, {{ceo, 1}}));
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        env(proposal::create(
            target,
            proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq),
            proposal::expiration(env, 100s)));
        env.close();

        {
            json::Value jv = proposal::sign(env, ceo, target, ticketSeq, target, ceo);
            jv[sfProposalID.jsonName] = to_string(uint256{});
            env(jv, Ter(temMALFORMED));
            env.close();
        }

        {
            json::Value jv = proposal::sign(env, ceo, target, ticketSeq, target, ceo);
            jv[sfProposalSignature.jsonName][jss::SigningPubKey] = "";
            env(jv, Ter(temMALFORMED));
            env.close();
        }

        {
            json::Value jv = proposal::sign(env, ceo, target, ticketSeq, target, ceo);
            jv[sfProposalSignature.jsonName][jss::TxnSignature] = "00";
            env(jv, Ter(temBAD_SIGNATURE));
            env.close();
        }
    }

    void
    testNoEntry(FeatureBitset features)
    {
        testcase("no such proposal");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        json::Value jv;
        jv[jss::TransactionType] = "TransactionProposalSign";
        jv[jss::Account] = alice.human();
        jv[sfProposalID.jsonName] = to_string(proposal::id(alice, 1));
        jv[sfSigningFor.jsonName] = alice.human();
        auto& ps = jv[sfProposalSignature.jsonName];
        ps[jss::Account] = alice.human();
        ps[jss::SigningPubKey] = strHex(alice.pk().slice());
        ps[jss::TxnSignature] = "00";
        env(jv, Ter(tecNO_ENTRY));
        env.close();
    }

    void
    testOrdinaryMultiSign(FeatureBitset features)
    {
        testcase("ordinary multi-sign accumulate, then submit");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const target{"target"};
        Account const dest{"dest"};
        Account const ceo{"ceo"};
        Account const cfo{"cfo"};
        env.fund(XRP(10000), target, dest, ceo, cfo);
        env.close();

        env(signers(target, 6, {{ceo, 4}, {cfo, 3}}));
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        env(proposal::create(
            target,
            proposal::unsignedPayload(
                env, pay(target, dest, XRP(1)), ticketSeq, /*extraSigners=*/2),
            proposal::expiration(env, 100s)));
        env.close();

        BEAST_EXPECT(proposal::entry(env, target, ticketSeq));

        env(proposal::sign(env, ceo, target, ticketSeq, target, ceo));
        env.close();

        {
            auto const sle = proposal::entry(env, target, ticketSeq);
            if (!BEAST_EXPECT(sle))
                return;
            auto const stored = sle->getFieldObject(sfProposedTransaction);
            BEAST_EXPECT(stored.isFieldPresent(sfSigners));
            BEAST_EXPECT(stored.getFieldArray(sfSigners).size() == 1);
            BEAST_EXPECT(stored.getFieldArray(sfSigners)[0].getAccountID(sfAccount) == ceo.id());
            BEAST_EXPECT(stored.getFieldVL(sfSigningPubKey).empty());
        }

        env(proposal::sign(env, cfo, target, ticketSeq, target, cfo));
        env.close();

        {
            auto const sle = proposal::entry(env, target, ticketSeq);
            if (!BEAST_EXPECT(sle))
                return;
            auto const stored = sle->getFieldObject(sfProposedTransaction);
            auto const& signers = stored.getFieldArray(sfSigners);
            BEAST_EXPECT(signers.size() == 2);
            BEAST_EXPECT(signers[0].getAccountID(sfAccount) < signers[1].getAccountID(sfAccount));
        }

        env(proposedJson(env, target, ticketSeq), Sig(kNone));
        env.close();

        BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
        BEAST_EXPECT(!env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
        BEAST_EXPECT(env.balance(dest) == XRP(10000) + XRP(1));
    }

    void
    testOrdinarySingleSign(FeatureBitset features)
    {
        testcase("ordinary single-sign with the account's own key");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const target{"target"};
        Account const dest{"dest"};
        env.fund(XRP(10000), target, dest);
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        env(proposal::create(
            target,
            proposal::unsignedPayload(env, pay(target, dest, XRP(1)), ticketSeq),
            proposal::expiration(env, 100s)));
        env.close();

        env(proposal::sign(env, target, target, ticketSeq, target, target));
        env.close();

        {
            auto const sle = proposal::entry(env, target, ticketSeq);
            if (!BEAST_EXPECT(sle))
                return;
            auto const stored = sle->getFieldObject(sfProposedTransaction);
            BEAST_EXPECT(!stored.isFieldPresent(sfSigners));
            BEAST_EXPECT(!stored.getFieldVL(sfSigningPubKey).empty());
            BEAST_EXPECT(stored.isFieldPresent(sfTxnSignature));
        }

        env(proposedJson(env, target, ticketSeq), Sig(kNone));
        env.close();

        BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
        BEAST_EXPECT(env.balance(dest) == XRP(10000) + XRP(1));
    }

    void
    testOrdinaryDelegate(FeatureBitset features)
    {
        testcase("ordinary sign for the proposed transaction's Delegate");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const target{"target"};
        Account const dest{"dest"};
        Account const delegateAcct{"delegateAcct"};
        Account const ds1{"ds1"};
        env.fund(XRP(10000), target, dest, delegateAcct, ds1);
        env.close();

        env(delegate::set(target, delegateAcct, {"Payment"}));
        env(signers(delegateAcct, 1, {{ds1, 1}}));
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        json::Value tx = pay(target, dest, XRP(1));
        tx[sfDelegate.jsonName] = delegateAcct.human();
        env(proposal::create(
            target,
            proposal::unsignedPayload(env, tx, ticketSeq, /*extraSigners=*/1),
            proposal::expiration(env, 100s)));
        env.close();

        env(proposal::sign(env, ds1, target, ticketSeq, delegateAcct, ds1));
        env.close();

        {
            auto const sle = proposal::entry(env, target, ticketSeq);
            if (!BEAST_EXPECT(sle))
                return;
            auto const stored = sle->getFieldObject(sfProposedTransaction);
            BEAST_EXPECT(stored.getAccountID(sfDelegate) == delegateAcct.id());
            BEAST_EXPECT(stored.isFieldPresent(sfSigners));
            BEAST_EXPECT(stored.getFieldArray(sfSigners).size() == 1);
            BEAST_EXPECT(stored.getFieldArray(sfSigners)[0].getAccountID(sfAccount) == ds1.id());
        }

        env(proposedJson(env, target, ticketSeq), Sig(kNone));
        env.close();

        BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
        BEAST_EXPECT(env.balance(dest) == XRP(10000) + XRP(1));
    }

    void
    testExpired(FeatureBitset features)
    {
        testcase("sign against a terminal proposal deletes it");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const target{"target"};
        Account const dest{"dest"};
        Account const ceo{"ceo"};
        env.fund(XRP(10000), target, dest, ceo);
        env.close();

        env(signers(target, 1, {{ceo, 1}}));
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        std::uint32_t const ownersBefore = ownerCount(env, target);

        env(proposal::create(
            target,
            proposal::unsignedPayload(env, pay(target, dest, XRP(1)), ticketSeq),
            proposal::expiration(env, 1s)));
        env.close();
        BEAST_EXPECT(proposal::entry(env, target, ticketSeq));

        // Default close interval is 5s, so the 1s expiration has passed.
        env(proposal::sign(env, ceo, target, ticketSeq, target, ceo), Ter(tecEXPIRED));
        env.close();

        BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
        BEAST_EXPECT(env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
        BEAST_EXPECT(ownerCount(env, target) == ownersBefore);
    }

    void
    testExpiredWithBadSignature(FeatureBitset features)
    {
        testcase("bad signature against a terminal proposal still deletes it");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const target{"target"};
        Account const dest{"dest"};
        Account const ceo{"ceo"};
        env.fund(XRP(10000), target, dest, ceo);
        env.close();

        env(signers(target, 1, {{ceo, 1}}));
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        std::uint32_t const ownersBefore = ownerCount(env, target);

        env(proposal::create(
            target,
            proposal::unsignedPayload(env, pay(target, dest, XRP(1)), ticketSeq),
            proposal::expiration(env, 1s)));
        env.close();
        BEAST_EXPECT(proposal::entry(env, target, ticketSeq));

        // The proposal expired one second after creation and default close
        // interval is 5s, so it is now terminal. Preclaim must short-circuit
        // before signature verification, so a garbage TxnSignature that would
        // otherwise be rejected with temBAD_SIGNATURE still triggers cleanup
        // and returns tecEXPIRED (On-Chain Cosigner spec §6.3.2.2).
        {
            json::Value jv = proposal::sign(env, ceo, target, ticketSeq, target, ceo);
            jv[sfProposalSignature.jsonName][jss::TxnSignature] = std::string(128, 'A');
            env(jv, Ter(tecEXPIRED));
            env.close();
        }

        BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
        BEAST_EXPECT(env.le(keylet::ticket(target.id(), SeqProxy::rawTicket(ticketSeq))));
        BEAST_EXPECT(ownerCount(env, target) == ownersBefore);
    }

    void
    testWrongSign(FeatureBitset features)
    {
        testcase("wrong sign");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const target{"target"};
        Account const dest{"dest"};
        Account const ceo{"ceo"};
        Account const stranger{"stranger"};
        env.fund(XRP(10000), target, dest, ceo, stranger);
        env.close();

        env(signers(target, 1, {{ceo, 1}}));
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        env(proposal::create(
            target,
            proposal::unsignedPayload(env, pay(target, dest, XRP(1)), ticketSeq),
            proposal::expiration(env, 100s)));
        env.close();

        // Cryptographically valid, but stranger is not on the SignerList.
        env(proposal::sign(env, stranger, target, ticketSeq, target, stranger),
            Ter(tecNO_PERMISSION));
        env.close();
        BEAST_EXPECT(proposal::entry(env, target, ticketSeq));

        // SigningFor is not an account the payload needs a signature from.
        env(proposal::sign(env, ceo, target, ticketSeq, dest, ceo), Ter(tecNO_PERMISSION));
        env.close();

        // Broken signature bytes.
        {
            json::Value jv = proposal::sign(env, ceo, target, ticketSeq, target, ceo);
            jv[sfProposalSignature.jsonName][jss::TxnSignature] = std::string(128, 'A');
            env(jv, Ter(temBAD_SIGNATURE));
            env.close();
        }

        BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
    }

    void
    testDuplicateAndModeConflict(FeatureBitset features)
    {
        testcase("duplicate contribution and mode conflict");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const target{"target"};
        Account const dest{"dest"};
        Account const ceo{"ceo"};
        env.fund(XRP(10000), target, dest, ceo);
        env.close();

        env(signers(target, 1, {{ceo, 1}}));
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        env(proposal::create(
            target,
            proposal::unsignedPayload(env, pay(target, dest, XRP(1)), ticketSeq),
            proposal::expiration(env, 100s)));
        env.close();

        env(proposal::sign(env, ceo, target, ticketSeq, target, ceo));
        env.close();

        env(proposal::sign(env, ceo, target, ticketSeq, target, ceo), Ter(tecDUPLICATE));
        env.close();

        env(proposal::sign(env, target, target, ticketSeq, target, target), Ter(tecNO_PERMISSION));
        env.close();
    }

    void
    testBatch(FeatureBitset features)
    {
        testcase("batch outer plus participant signatures, then submit");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const outer{"outer"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const dest{"dest"};
        Account const outerKey{"outerKey"};
        Account const carolKey{"carolKey"};
        env.fund(XRP(10000), outer, bob, carol, dest, outerKey, carolKey);
        env.close();

        env(signers(outer, 1, {{outerKey, 1}}));
        env(signers(carol, 1, {{carolKey, 1}}));
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, outer);
        json::Value const proposedTx = proposal::unsignedBatch(
            env,
            outer,
            ticketSeq,
            tfAllOrNothing,
            {proposal::innerTx(pay(outer, dest, XRP(1)), env.seq(outer)),
             proposal::innerTx(pay(bob, dest, XRP(1)), env.seq(bob)),
             proposal::innerTx(pay(carol, dest, XRP(1)), env.seq(carol))},
            /*numSigners=*/3);

        env(proposal::create(outerKey, proposedTx, proposal::expiration(env, 100s)));
        env.close();

        env(proposal::sign(env, outerKey, outer, ticketSeq, outer, outerKey));
        env.close();
        env(proposal::sign(env, outerKey, outer, ticketSeq, bob, bob));
        env.close();
        env(proposal::sign(env, outerKey, outer, ticketSeq, carol, carolKey));
        env.close();

        {
            auto const sle = proposal::entry(env, outer, ticketSeq);
            if (!BEAST_EXPECT(sle))
                return;
            auto const stored = sle->getFieldObject(sfProposedTransaction);
            BEAST_EXPECT(stored.isFieldPresent(sfSigners));
            BEAST_EXPECT(stored.getFieldArray(sfSigners).size() == 1);
            BEAST_EXPECT(stored.isFieldPresent(sfBatchSigners));
            auto const& batchSigners = stored.getFieldArray(sfBatchSigners);
            BEAST_EXPECT(batchSigners.size() == 2);
            BEAST_EXPECT(
                batchSigners[0].getAccountID(sfAccount) < batchSigners[1].getAccountID(sfAccount));
        }

        env(proposedJson(env, outer, ticketSeq), Sig(kNone));
        env.close();

        BEAST_EXPECT(!proposal::entry(env, outer, ticketSeq));
        BEAST_EXPECT(env.balance(dest) == XRP(10000) + XRP(3));
    }

    void
    run() override
    {
        using namespace jtx;

        FeatureBitset const all{testableAmendments()};

        testDisabled(all);
        testPreflight(all);
        testNoEntry(all);
        testOrdinaryMultiSign(all);
        testOrdinarySingleSign(all);
        testOrdinaryDelegate(all);
        testExpired(all);
        testExpiredWithBadSignature(all);
        testWrongSign(all);
        testDuplicateAndModeConflict(all);
        testBatch(all);
    }
};

BEAST_DEFINE_TESTSUITE(TransactionProposalSign, app, xrpl);

}  // namespace xrpl::test
