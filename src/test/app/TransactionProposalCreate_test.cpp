#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/batch.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/transactors/proposal/ProposalHelpers.h>

#include <chrono>
#include <cstdint>
#include <string>

namespace xrpl::test {

struct TransactionProposalCreate_test : public beast::unit_test::Suite
{
    // A TransactionProposalCreate carrying an unsigned proposed transaction.
    static json::Value
    proposalCreate(
        jtx::Account const& proposer,
        json::Value const& proposedTx,
        std::uint32_t expiration)
    {
        json::Value jv;
        jv[jss::TransactionType] = "TransactionProposalCreate";
        jv[jss::Account] = proposer.human();
        jv[sfProposedTransaction.getJsonName()] = proposedTx;
        jv[sfExpiration.getJsonName()] = expiration;
        return jv;
    }

    // A proposed transaction in the form the ledger stores it: unsigned,
    // ticket-based, with the fee the target account will pay fixed now.
    static json::Value
    unsignedPayload(
        jtx::Env const& env,
        jtx::Account const& target,
        jtx::Account const& dest,
        std::uint32_t ticketSeq)
    {
        json::Value tx = jtx::pay(target, dest, jtx::XRP(1));
        tx[jss::Sequence] = 0;
        tx[sfTicketSequence.getJsonName()] = ticketSeq;
        tx[jss::Fee] = std::to_string(env.current()->fees().base.drops());
        tx[jss::SigningPubKey] = "";
        return tx;
    }

    void
    testCreate(FeatureBitset features)
    {
        testcase("create proposal object");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};    // the proposer
        Account const target{"target"};  // the account the proposal is for
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        std::uint32_t const targetTicketSeq = env.seq(target) + 1;
        env(ticket::create(target, 1));
        env.close();

        // The proposed transaction is stored unsigned: no signature fields and
        // an empty SigningPubKey. It is ticket-based so unrelated target account
        // activity cannot invalidate it while signatures are collected.
        json::Value const proposedTx = unsignedPayload(env, target, bob, targetTicketSeq);

        std::uint32_t const expiration = (env.now() + 100s).time_since_epoch().count();

        env(proposalCreate(alice, proposedTx, expiration));
        env.close();

        auto const sle = env.le(keylet::txProposal(target.id(), targetTicketSeq));
        BEAST_EXPECT(sle);
        if (!sle)
            return;

        BEAST_EXPECT(sle->getAccountID(sfOwner) == alice.id());
        BEAST_EXPECT(sle->getFieldU32(sfExpiration) == expiration);

        auto const stored = sle->getFieldObject(sfProposedTransaction);
        BEAST_EXPECT(stored.getAccountID(sfAccount) == target.id());
        BEAST_EXPECT(stored.getFieldU32(sfSequence) == 0);
        BEAST_EXPECT(stored.getFieldU32(sfTicketSequence) == targetTicketSeq);
        BEAST_EXPECT(stored.getFieldVL(sfSigningPubKey).empty());

        // The proposal reserves several owner increments against the proposer.
        // The target only owns the Ticket used by the proposed transaction.
        BEAST_EXPECT(ownerCount(env, alice) == kProposalOwnerCount);
        BEAST_EXPECT(ownerCount(env, target) == 1);
    }

    // The proposed transaction must be storable in unsigned canonical form and
    // must be a transaction that could be submitted on its own. Each case below
    // takes an otherwise valid payload and breaks exactly one of those rules.
    void
    testRejectedPayload(FeatureBitset features)
    {
        testcase("reject payload that must not be stored");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        std::uint32_t const targetTicketSeq = env.seq(target) + 1;
        env(ticket::create(target, 1));
        env.close();

        std::uint32_t const expiration = (env.now() + 100s).time_since_epoch().count();

        // A payload that is accepted as-is; every case starts from this.
        auto payload = [&]() { return unsignedPayload(env, target, bob, targetTicketSeq); };

        auto reject = [&](json::Value const& proposedTx, TER expected) {
            env(proposalCreate(alice, proposedTx, expiration), Ter(expected));
            env.close();
            BEAST_EXPECT(!env.le(keylet::txProposal(target.id(), targetTicketSeq)));
            BEAST_EXPECT(ownerCount(env, alice) == 0);
        };

        // Signatures may only ever arrive through TransactionProposalSign.
        {
            json::Value tx = payload();
            tx[sfTxnSignature.getJsonName()] = "DEADBEEF";
            reject(tx, temBAD_SIGNER);
        }
        {
            json::Value tx = payload();
            auto& signer = tx[sfSigners.getJsonName()][0u][sfSigner.getJsonName()];
            signer[jss::Account] = bob.human();
            signer[jss::SigningPubKey] = strHex(bob.pk().slice());
            signer[sfTxnSignature.getJsonName()] = "DEADBEEF";
            reject(tx, temBAD_SIGNER);
        }

        // SigningPubKey must be present and empty: absent is not the same as
        // empty, and a set key means the payload was signed for single-signing.
        {
            json::Value tx = payload();
            tx.removeMember(jss::SigningPubKey);
            reject(tx, temBAD_SIGNER);
        }
        {
            json::Value tx = payload();
            tx[jss::SigningPubKey] = strHex(target.pk().slice());
            reject(tx, temBAD_SIGNER);
        }

        // A pseudo-transaction is never submittable by an account.
        {
            json::Value tx = payload();
            tx[jss::TransactionType] = jss::EnableAmendment;
            reject(tx, temINVALID);
        }

        // An inner batch transaction bypasses the ordinary signature checks.
        {
            json::Value tx = payload();
            tx[jss::Flags] = tfInnerBatchTxn;
            reject(tx, temINVALID);
        }

        // Proposals do not nest.
        {
            json::Value tx = payload();
            tx[jss::TransactionType] = "TransactionProposalCreate";
            reject(tx, temINVALID);
        }
    }

    // Nothing about the transaction is available before the amendment is
    // active, not even to an otherwise valid proposal.
    void
    testDisabled(FeatureBitset features)
    {
        testcase("amendment disabled");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features - featureCosign};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        std::uint32_t const targetTicketSeq = env.seq(target) + 1;
        env(ticket::create(target, 1));
        env.close();

        std::uint32_t const expiration = (env.now() + 100s).time_since_epoch().count();

        env(proposalCreate(alice, unsignedPayload(env, target, bob, targetTicketSeq), expiration),
            Ter(temDISABLED));
        env.close();

        BEAST_EXPECT(!env.le(keylet::txProposal(target.id(), targetTicketSeq)));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
    }

    // A proposal that could never be completed must not be stored, and a
    // target-and-ticket pair may hold at most one proposal.
    void
    testPreclaim(FeatureBitset features)
    {
        testcase("reject proposal that cannot be completed");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        Account const carol{"carol"};  // never funded
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        std::uint32_t const firstTicketSeq = env.seq(target) + 1;
        env(ticket::create(target, 3));
        env.close();

        std::uint32_t const expiration = (env.now() + 100s).time_since_epoch().count();

        // The proposal's own expiration has already passed.
        {
            std::uint32_t const past = env.now().time_since_epoch().count();
            env(proposalCreate(alice, unsignedPayload(env, target, bob, firstTicketSeq), past),
                Ter(tecEXPIRED));
            env.close();
            BEAST_EXPECT(!env.le(keylet::txProposal(target.id(), firstTicketSeq)));
            BEAST_EXPECT(ownerCount(env, alice) == 0);
        }

        // The proposed transaction's own ledger bound has passed: the ordinary
        // path would reject it with tefMAX_LEDGER, so it can never complete.
        {
            json::Value tx = unsignedPayload(env, target, bob, firstTicketSeq);
            tx[sfLastLedgerSequence.getJsonName()] = env.current()->seq() - 1;
            env(proposalCreate(alice, tx, expiration), Ter(tecEXPIRED));
            env.close();
            BEAST_EXPECT(!env.le(keylet::txProposal(target.id(), firstTicketSeq)));
            BEAST_EXPECT(ownerCount(env, alice) == 0);
        }

        // The last ledger in which the proposed transaction may still be
        // submitted is the current one, so the proposal is still alive.
        {
            json::Value tx = unsignedPayload(env, target, bob, firstTicketSeq);
            tx[sfLastLedgerSequence.getJsonName()] = env.current()->seq();
            env(proposalCreate(alice, tx, expiration));
            env.close();
            BEAST_EXPECT(env.le(keylet::txProposal(target.id(), firstTicketSeq)));
            BEAST_EXPECT(ownerCount(env, alice) == kProposalOwnerCount);
        }

        // The target and ticket already carry a proposal.
        {
            env(proposalCreate(
                    alice, unsignedPayload(env, target, bob, firstTicketSeq), expiration),
                Ter(tecDUPLICATE));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == kProposalOwnerCount);
        }

        // A different ticket of the same target is a different proposal.
        {
            env(proposalCreate(
                alice, unsignedPayload(env, target, bob, firstTicketSeq + 1), expiration));
            env.close();
            BEAST_EXPECT(env.le(keylet::txProposal(target.id(), firstTicketSeq + 1)));
            BEAST_EXPECT(ownerCount(env, alice) == 2 * kProposalOwnerCount);
        }

        // The target account does not exist, so it can never sign.
        {
            env(proposalCreate(alice, unsignedPayload(env, carol, bob, 1), expiration),
                Ter(tecNO_TARGET));
            env.close();
            BEAST_EXPECT(!env.le(keylet::txProposal(carol.id(), 1)));
            BEAST_EXPECT(ownerCount(env, alice) == 2 * kProposalOwnerCount);
        }
    }

    // The proposer holds the proposal's reserve until it is resolved.
    void
    testReserve(FeatureBitset features)
    {
        testcase("proposer reserve");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), target, bob);
        env.close();

        std::uint32_t const targetTicketSeq = env.seq(target) + 1;
        env(ticket::create(target, 1));
        env.close();

        // Fund alice just short of the reserve the proposal requires.
        env.fund(env.current()->fees().accountReserve(kProposalOwnerCount, 1) - drops(1), alice);
        env.close();

        std::uint32_t const expiration = (env.now() + 100s).time_since_epoch().count();
        json::Value const proposedTx = unsignedPayload(env, target, bob, targetTicketSeq);

        env(proposalCreate(alice, proposedTx, expiration), Ter(tecINSUFFICIENT_RESERVE));
        env.close();
        BEAST_EXPECT(!env.le(keylet::txProposal(target.id(), targetTicketSeq)));
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        env(pay(bob, alice, XRP(10)));
        env.close();

        env(proposalCreate(alice, proposedTx, expiration));
        env.close();
        BEAST_EXPECT(env.le(keylet::txProposal(target.id(), targetTicketSeq)));
        BEAST_EXPECT(ownerCount(env, alice) == kProposalOwnerCount);
    }

    // A proposed Batch holds several inner transactions and the signatures of
    // every account they touch, so it reserves more than an ordinary proposal.
    void
    testBatchReserve(FeatureBitset features)
    {
        testcase("proposed batch reserve");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        std::uint32_t const targetTicketSeq = env.seq(target) + 1;
        env(ticket::create(target, 1));
        env.close();

        auto inner = [&](std::uint32_t seq) {
            json::Value tx = pay(target, bob, XRP(1));
            tx[jss::Sequence] = seq;
            tx[jss::Fee] = "0";
            tx[jss::Flags] = tfInnerBatchTxn;
            tx[jss::SigningPubKey] = "";
            return tx;
        };

        json::Value proposedTx;
        proposedTx[jss::TransactionType] = jss::Batch;
        proposedTx[jss::Account] = target.human();
        proposedTx[jss::Flags] = tfAllOrNothing;
        proposedTx[jss::Sequence] = 0;
        proposedTx[sfTicketSequence.getJsonName()] = targetTicketSeq;
        proposedTx[jss::Fee] = std::to_string(batch::calcBatchFee(env, 0, 2).drops());
        proposedTx[jss::SigningPubKey] = "";
        proposedTx[jss::RawTransactions][0u][jss::RawTransaction] = inner(env.seq(target));
        proposedTx[jss::RawTransactions][1u][jss::RawTransaction] = inner(env.seq(target) + 1);

        std::uint32_t const expiration = (env.now() + 100s).time_since_epoch().count();

        env(proposalCreate(alice, proposedTx, expiration));
        env.close();

        BEAST_EXPECT(env.le(keylet::txProposal(target.id(), targetTicketSeq)));
        BEAST_EXPECT(ownerCount(env, alice) == kBatchProposalOwnerCount);
    }

    void
    run() override
    {
        using namespace jtx;
        testDisabled(testableAmendments());
        testCreate(testableAmendments());
        testRejectedPayload(testableAmendments());
        testPreclaim(testableAmendments());
        testReserve(testableAmendments());
        testBatchReserve(testableAmendments());
    }
};

BEAST_DEFINE_TESTSUITE(TransactionProposalCreate, app, xrpl);

}  // namespace xrpl::test
