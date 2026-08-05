#include <test/jtx/AMM.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/batch.h>
#include <test/jtx/pay.h>
#include <test/jtx/sponsor.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
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

        // The remaining signature containers are just as forbidden as a bare
        // TxnSignature or Signers array.
        {
            json::Value tx = payload();
            auto& bs = tx[sfBatchSigners.getJsonName()][0u][sfBatchSigner.getJsonName()];
            bs[jss::Account] = bob.human();
            bs[jss::SigningPubKey] = strHex(bob.pk().slice());
            bs[sfTxnSignature.getJsonName()] = "DEADBEEF";
            reject(tx, temBAD_SIGNER);
        }
        {
            json::Value tx = payload();
            tx[sfCounterpartySignature.getJsonName()][jss::SigningPubKey] =
                strHex(bob.pk().slice());
            reject(tx, temBAD_SIGNER);
        }
        {
            json::Value tx = payload();
            tx[sfSponsorSignature.getJsonName()][jss::SigningPubKey] = strHex(bob.pk().slice());
            reject(tx, temBAD_SIGNER);
        }

        // The proposed transaction must be ticket-based: a missing
        // TicketSequence, or a live Sequence alongside it, is rejected.
        {
            json::Value tx = payload();
            tx.removeMember(sfTicketSequence.getJsonName());
            reject(tx, temSEQ_AND_TICKET);
        }
        {
            json::Value tx = payload();
            tx[jss::Sequence] = 1;
            reject(tx, temSEQ_AND_TICKET);
        }

        // A payload that fails its own transaction type's preflight surfaces
        // that type's own code, not a generic error (On-Chain Cosigner spec §5.3.1.2).
        {
            json::Value tx = payload();
            tx[jss::Amount] = "0";
            reject(tx, temBAD_AMOUNT);
        }

        // Expiration must be present and non-zero.
        {
            env(proposalCreate(alice, payload(), 0), Ter(temBAD_EXPIRATION));
            env.close();
            BEAST_EXPECT(!env.le(keylet::txProposal(target.id(), targetTicketSeq)));
            BEAST_EXPECT(ownerCount(env, alice) == 0);
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

        // A LastLedgerSequence equal to the current ledger leaves no window to
        // collect signatures before the proposed transaction's own bound
        // passes, so it is rejected the same as one already in the past
        // (On-Chain Cosigner spec §5.3.2.2).
        {
            json::Value tx = unsignedPayload(env, target, bob, firstTicketSeq);
            tx[sfLastLedgerSequence.getJsonName()] = env.current()->seq();
            env(proposalCreate(alice, tx, expiration), Ter(tecEXPIRED));
            env.close();
            BEAST_EXPECT(!env.le(keylet::txProposal(target.id(), firstTicketSeq)));
            BEAST_EXPECT(ownerCount(env, alice) == 0);
        }

        // With no ledger bound on the proposed transaction, the proposal is
        // created normally.
        {
            env(proposalCreate(
                alice, unsignedPayload(env, target, bob, firstTicketSeq), expiration));
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

    // A multi-account Batch is the primary motivating case (On-Chain Cosigner spec §10): its inner
    // transactions touch accounts other than the outer one, so submitting it
    // directly would require a BatchSigners entry per participant. A proposal is
    // stored unsigned, so those signatures are collected on-ledger afterward and
    // the signer-presence match is skipped at creation time (On-Chain Cosigner spec §5.3.1.2).
    void
    testMultiAccountBatch(FeatureBitset features)
    {
        testcase("proposed multi-account batch");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};  // outer account of the batch
        Account const bob{"bob"};        // a distinct inner participant
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        std::uint32_t const targetTicketSeq = env.seq(target) + 1;
        env(ticket::create(target, 1));
        env.close();

        auto inner = [&](Account const& from, Account const& to, std::uint32_t seq) {
            json::Value tx = pay(from, to, XRP(1));
            tx[jss::Sequence] = seq;
            tx[jss::Fee] = "0";
            tx[jss::Flags] = tfInnerBatchTxn;
            tx[jss::SigningPubKey] = "";
            return tx;
        };

        // One inner from the outer account, one from bob: bob is a required
        // signer, so a direct submission would need his BatchSigners entry.
        json::Value proposedTx;
        proposedTx[jss::TransactionType] = jss::Batch;
        proposedTx[jss::Account] = target.human();
        proposedTx[jss::Flags] = tfAllOrNothing;
        proposedTx[jss::Sequence] = 0;
        proposedTx[sfTicketSequence.getJsonName()] = targetTicketSeq;
        proposedTx[jss::Fee] = std::to_string(batch::calcBatchFee(env, 1, 2).drops());
        proposedTx[jss::SigningPubKey] = "";
        proposedTx[jss::RawTransactions][0u][jss::RawTransaction] =
            inner(target, bob, env.seq(target));
        proposedTx[jss::RawTransactions][1u][jss::RawTransaction] =
            inner(bob, target, env.seq(bob));

        std::uint32_t const expiration = (env.now() + 100s).time_since_epoch().count();

        env(proposalCreate(alice, proposedTx, expiration));
        env.close();

        auto const sle = env.le(keylet::txProposal(target.id(), targetTicketSeq));
        BEAST_EXPECT(sle);
        if (!sle)
            return;

        // The proposal is stored without any BatchSigners: the participants'
        // signatures are collected later through TransactionProposalSign.
        auto const stored = sle->getFieldObject(sfProposedTransaction);
        BEAST_EXPECT(!stored.isFieldPresent(sfBatchSigners));
        BEAST_EXPECT(ownerCount(env, alice) == kBatchProposalOwnerCount);
    }

    // The target account must be able to authorize a transaction through a
    // SignerList, so a pseudo-account (here an AMM's) cannot be a target even
    // though it exists on-ledger (On-Chain Cosigner spec §5.3.2.5).
    void
    testPseudoTarget(FeatureBitset features)
    {
        testcase("reject proposal targeting a pseudo-account");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const proposer{"proposer"};
        Account const alice{"alice"};  // the AMM creator
        Account const gw{"gw"};
        Account const bob{"bob"};
        // NOLINTNEXTLINE(readability-identifier-naming)
        auto const USD = gw["USD"];
        env.fund(XRP(10000), proposer, alice, gw, bob);
        env.close();
        env.trust(USD(1'000'000), alice);
        env.close();
        env(pay(gw, alice, USD(10'000)));
        env.close();

        AMM const amm(env, alice, XRP(1'000), USD(1'000), Ter(tesSUCCESS));
        env.close();

        // A well-formed Payment whose target is the AMM's pseudo-account.
        json::Value proposedTx = pay(alice, bob, XRP(1));
        proposedTx[jss::Account] = toBase58(amm.ammAccount());
        proposedTx[jss::Sequence] = 0;
        proposedTx[sfTicketSequence.getJsonName()] = 1;
        proposedTx[jss::Fee] = std::to_string(env.current()->fees().base.drops());
        proposedTx[jss::SigningPubKey] = "";

        std::uint32_t const expiration = (env.now() + 100s).time_since_epoch().count();

        env(proposalCreate(proposer, proposedTx, expiration), Ter(tecNO_PERMISSION));
        env.close();
        BEAST_EXPECT(!env.le(keylet::txProposal(amm.ammAccount(), 1)));
        BEAST_EXPECT(ownerCount(env, proposer) == 0);
    }

    // A proposed transaction may itself require an auxiliary co-signer beyond
    // its own Account: a LoanSet's Counterparty, or the Sponsor of an
    // account-level SponsorshipTransfer (On-Chain Cosigner spec §6.1, §6.6.3). That co-signature
    // field is collected later via TransactionProposalSign, so — just like
    // the ordinary signature fields — it must be absent, not required, at
    // creation time.
    void
    testAuxiliaryCoSignatureTypes(FeatureBitset features)
    {
        testcase("proposal for a transaction type with an auxiliary co-signature");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};        // the proposer
        Account const borrower{"borrower"};  // the target account

        env.fund(XRP(10000), alice, borrower);
        env.close();

        std::uint32_t const expiration = (env.now() + 100s).time_since_epoch().count();

        // LoanSet: the Counterparty's signature is collected later; it must
        // not be required up front.
        {
            std::uint32_t const ticketSeq = env.seq(borrower) + 1;
            env(ticket::create(borrower, 1));
            env.close();

            json::Value tx = loan::set(borrower, uint256{1}, 1'000);
            tx[jss::Sequence] = 0;
            tx[sfTicketSequence.getJsonName()] = ticketSeq;
            tx[jss::Fee] = std::to_string(env.current()->fees().base.drops());
            tx[jss::SigningPubKey] = "";

            env(proposalCreate(alice, tx, expiration));
            env.close();
            BEAST_EXPECT(env.le(keylet::txProposal(borrower.id(), ticketSeq)));
        }

        // SponsorshipTransfer (account-level reserve sponsorship): the
        // Sponsor's signature is likewise collected later.
        {
            std::uint32_t const ticketSeq = env.seq(borrower) + 1;
            env(ticket::create(borrower, 1));
            env.close();

            json::Value tx = sponsor::transfer(borrower, tfSponsorshipCreate);
            tx[sfSponsor.getJsonName()] = alice.human();
            tx[sfSponsorFlags.getJsonName()] = spfSponsorReserve;
            tx[jss::Sequence] = 0;
            tx[sfTicketSequence.getJsonName()] = ticketSeq;
            tx[jss::Fee] = std::to_string(env.current()->fees().base.drops());
            tx[jss::SigningPubKey] = "";

            env(proposalCreate(alice, tx, expiration));
            env.close();
            BEAST_EXPECT(env.le(keylet::txProposal(borrower.id(), ticketSeq)));
        }
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
        testMultiAccountBatch(testableAmendments());
        testPseudoTarget(testableAmendments());
        testAuxiliaryCoSignatureTypes(testableAmendments());
    }
};

BEAST_DEFINE_TESTSUITE(TransactionProposalCreate, app, xrpl);

}  // namespace xrpl::test
