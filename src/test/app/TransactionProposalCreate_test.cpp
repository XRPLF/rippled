#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

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
        json::Value proposedTx = pay(target, bob, XRP(1));
        proposedTx[jss::Sequence] = 0;
        proposedTx[sfTicketSequence.getJsonName()] = targetTicketSeq;
        proposedTx[jss::Fee] = std::to_string(env.current()->fees().base.drops());
        proposedTx[jss::SigningPubKey] = "";

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

        // The proposal reserves five owner increments against the proposer.
        // The target only owns the Ticket used by the proposed transaction.
        BEAST_EXPECT(ownerCount(env, alice) == 5);
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

        std::string const feeDrops = std::to_string(env.current()->fees().base.drops());
        std::uint32_t const expiration = (env.now() + 100s).time_since_epoch().count();

        // A payload that is accepted as-is; every case starts from this.
        auto payload = [&]() {
            json::Value tx = pay(target, bob, XRP(1));
            tx[jss::Sequence] = 0;
            tx[sfTicketSequence.getJsonName()] = targetTicketSeq;
            tx[jss::Fee] = feeDrops;
            tx[jss::SigningPubKey] = "";
            return tx;
        };

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
            json::Value tx = pay(target, bob, XRP(1));
            tx[jss::Sequence] = 0;
            tx[sfTicketSequence.getJsonName()] = targetTicketSeq;
            tx[jss::Fee] = feeDrops;
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

    void
    run() override
    {
        using namespace jtx;
        testCreate(testableAmendments());
        testRejectedPayload(testableAmendments());
    }
};

BEAST_DEFINE_TESTSUITE(TransactionProposalCreate, app, xrpl);

}  // namespace xrpl::test
