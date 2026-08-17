#include <test/jtx/AMM.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/deposit.h>
#include <test/jtx/multisign.h>
#include <test/jtx/noop.h>
#include <test/jtx/offer.h>
#include <test/jtx/pay.h>
#include <test/jtx/proposal.h>
#include <test/jtx/sig.h>
#include <test/jtx/sponsor.h>
#include <test/jtx/ter.h>
#include <test/jtx/token.h>
#include <test/jtx/trust.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <chrono>  // IWYU pragma: keep
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace xrpl::test {

struct TransactionProposalCreate_test : public beast::unit_test::Suite
{
    void
    testReserveCounts()
    {
        testcase("proposal reserve");

        using namespace jtx;

        BEAST_EXPECT(proposal::kProposalOwnerCount == 5);
        BEAST_EXPECT(proposal::kBatchProposalOwnerCount == 10);
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

        std::uint32_t const targetTicketSeq = proposal::createTicket(env, target);

        env(proposal::create(
                alice,
                proposal::unsignedPayload(env, pay(target, bob, XRP(1)), targetTicketSeq),
                proposal::expiration(env, 100s)),
            Ter(temDISABLED));
        env.close();

        BEAST_EXPECT(!proposal::entry(env, target, targetTicketSeq));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
    }

    // The proposed transaction must be a transaction that could be submitted on
    // its own. Each case below takes an otherwise valid payload and breaks
    // exactly one of those rules; the rules about its signature fields are
    // covered by testRejectedSignatureFields.
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

        std::uint32_t const targetTicketSeq = proposal::createTicket(env, target);

        std::uint32_t const expiration = proposal::expiration(env, 100s);

        // A payload that is accepted as-is; every case starts from this.
        auto payload = [&]() {
            return proposal::unsignedPayload(env, pay(target, bob, XRP(1)), targetTicketSeq);
        };

        auto reject = [&](json::Value const& proposedTx, TER expected) {
            env(proposal::create(alice, proposedTx, expiration), Ter(expected));
            env.close();
            BEAST_EXPECT(!proposal::entry(env, target, targetTicketSeq));
            BEAST_EXPECT(ownerCount(env, alice) == 0);
        };

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
            env(proposal::create(alice, payload(), 0), Ter(temBAD_EXPIRATION));
            env.close();
            BEAST_EXPECT(!proposal::entry(env, target, targetTicketSeq));
            BEAST_EXPECT(ownerCount(env, alice) == 0);
        }
    }

    // A proposal is stored in unsigned canonical form: an empty SigningPubKey
    // and no signature field whatsoever. Signatures may only ever arrive
    // through TransactionProposalSign, so a payload is rejected for carrying a
    // signature container at all — whatever that container happens to hold.
    // Each container below is therefore filled every way it could be,
    // including combinations that could never verify: an empty container, a
    // key with no signature, a signature with no key, and a signature next to
    // the empty SigningPubKey the canonical form requires.
    void
    testRejectedSignatureFields(FeatureBitset features)
    {
        testcase("reject payload carrying a signature");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        std::uint32_t const targetTicketSeq = proposal::createTicket(env, target);

        std::uint32_t const expiration = proposal::expiration(env, 100s);

        std::string const key = strHex(bob.pk().slice());
        std::string const sig = "DEADBEEF";

        // The payloads every case starts from, each accepted as-is. Two cases
        // below would otherwise build the same payload, and the same proposal
        // cannot be submitted twice — the second is turned away as a duplicate
        // rather than judged again — so each call pays a different amount.
        // Nothing here turns on the amount.
        std::uint32_t paid = 0;
        auto payment = [&]() {
            return proposal::unsignedPayload(env, pay(target, bob, drops(++paid)), targetTicketSeq);
        };
        auto sponsoredPayment = [&]() {
            json::Value tx = pay(target, bob, drops(++paid));
            tx[sfSponsor.getJsonName()] = alice.human();
            tx[sfSponsorFlags.getJsonName()] = spfSponsorFee;
            return proposal::unsignedPayload(env, tx, targetTicketSeq);
        };
        auto loanSet = [&]() {
            json::Value tx = loan::set(target, uint256{1}, 1'000 + ++paid);
            tx[sfCounterparty.getJsonName()] = bob.human();
            return proposal::unsignedPayload(env, tx, targetTicketSeq);
        };
        auto batchTx = [&]() {
            return proposal::unsignedBatch(
                env,
                target,
                targetTicketSeq,
                tfAllOrNothing,
                {proposal::innerTx(pay(target, bob, drops(++paid)), env.seq(target)),
                 proposal::innerTx(pay(target, bob, drops(++paid)), env.seq(target) + 1)});
        };

        auto reject = [&](json::Value const& proposedTx) {
            env(proposal::create(alice, proposedTx, expiration), Ter(temBAD_SIGNER));
            env.close();
            BEAST_EXPECT(!proposal::entry(env, target, targetTicketSeq));
            BEAST_EXPECT(ownerCount(env, alice) == 0);
        };

        // Every way of filling in a signature. The payload's own signature
        // fields and a co-signature object hold the same three members, so the
        // same fills apply to both.
        std::vector<std::function<void(json::Value&)>> const fills{
            [&](json::Value& o) { o[jss::SigningPubKey] = key; },
            [&](json::Value& o) { o[sfTxnSignature.getJsonName()] = sig; },
            [&](json::Value& o) {
                o[jss::SigningPubKey] = "";
                o[sfTxnSignature.getJsonName()] = sig;
            },
            // Signed the ordinary way, which is the likeliest way one of these
            // arrives here.
            [&](json::Value& o) {
                o[jss::SigningPubKey] = key;
                o[sfTxnSignature.getJsonName()] = sig;
            },
            // Multi-signed: the signer's own key is empty and the signatures
            // sit in a nested Signers array. Each entry needs all three of
            // Account, SigningPubKey and TxnSignature to parse at all, so only
            // their values can vary.
            [&](json::Value& o) {
                o[jss::SigningPubKey] = "";
                auto& signer = o[sfSigners.getJsonName()][0u][sfSigner.getJsonName()];
                signer[jss::Account] = bob.human();
                signer[jss::SigningPubKey] = key;
                signer[sfTxnSignature.getJsonName()] = sig;
            },
            [&](json::Value& o) {
                o[jss::SigningPubKey] = "";
                auto& signer = o[sfSigners.getJsonName()][0u][sfSigner.getJsonName()];
                signer[jss::Account] = bob.human();
                signer[jss::SigningPubKey] = "";
                signer[sfTxnSignature.getJsonName()] = sig;
            },
        };

        // Every place a signature could sit, on a payload of a type that
        // carries it: a Counterparty's signature belongs to a LoanSet and
        // BatchSigners to a Batch, while a Sponsor's signature and the
        // payload's own signature fields sit on any transaction. A signature
        // is no more storable for being a field its transaction type expects
        // (On-Chain Cosigner spec §6.1, §6.6.3).
        struct Place
        {
            std::function<json::Value()> payload;
            std::function<json::Value&(json::Value&)> at;
        };

        std::vector<Place> const places{
            {.payload = payment, .at = [](json::Value& tx) -> json::Value& { return tx; }},
            {.payload = loanSet,
             .at = [](json::Value& tx) -> json::Value& {
                 auto& o = tx[sfCounterpartySignature.getJsonName()];
                 o = json::Value{json::ValueType::Object};
                 return o;
             }},
            {.payload = sponsoredPayment,
             .at = [](json::Value& tx) -> json::Value& {
                 auto& o = tx[sfSponsorSignature.getJsonName()];
                 o = json::Value{json::ValueType::Object};
                 return o;
             }},
            // A BatchSigners entry names the account it speaks for; the other
            // two co-signatures are fixed by the transaction they belong to and
            // do not.
            {.payload = batchTx,
             .at = [&](json::Value& tx) -> json::Value& {
                 auto& o = tx[sfBatchSigners.getJsonName()][0u][sfBatchSigner.getJsonName()];
                 o[jss::Account] = bob.human();
                 return o;
             }},
        };

        for (auto const& place : places)
        {
            for (auto const& fill : fills)
            {
                json::Value tx = place.payload();
                fill(place.at(tx));
                reject(tx);
            }
        }

        // Every place but the payload itself: a co-signature object is
        // disqualifying by its presence alone, so each is rejected left empty
        // too. The payload's own fields have no such case — left alone they are
        // the canonical form.
        for (std::size_t i = 1; i < places.size(); ++i)
        {
            json::Value tx = places[i].payload();
            places[i].at(tx);
            reject(tx);
        }

        // Nor does the payload have a counterpart for an absent SigningPubKey:
        // in a co-signature object an absent member is just an unfilled one,
        // but at the top level it is not the same as an empty one, with or
        // without a signature beside it.
        {
            json::Value tx = payment();
            tx.removeMember(jss::SigningPubKey);
            reject(tx);
        }
        {
            json::Value tx = payment();
            tx.removeMember(jss::SigningPubKey);
            tx[sfTxnSignature.getJsonName()] = sig;
            reject(tx);
        }
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

        std::uint32_t const firstTicketSeq = proposal::createTicket(env, target, 3);
        proposal::authorizeProposer(env, target, alice);

        std::uint32_t const expiration = proposal::expiration(env, 100s);

        auto payload = [&](std::uint32_t ticketSeq) {
            return proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq);
        };

        // The proposal's own expiration has already passed.
        {
            env(proposal::create(alice, payload(firstTicketSeq), proposal::expiration(env, 0s)),
                Ter(tecEXPIRED));
            env.close();
            BEAST_EXPECT(!proposal::entry(env, target, firstTicketSeq));
            BEAST_EXPECT(ownerCount(env, alice) == 0);
        }

        // The proposed transaction's own ledger bound has passed: the ordinary
        // path would reject it with tefMAX_LEDGER, so it can never complete.
        {
            json::Value tx = payload(firstTicketSeq);
            tx[sfLastLedgerSequence.getJsonName()] = env.current()->seq() - 1;
            env(proposal::create(alice, tx, expiration), Ter(tecEXPIRED));
            env.close();
            BEAST_EXPECT(!proposal::entry(env, target, firstTicketSeq));
            BEAST_EXPECT(ownerCount(env, alice) == 0);
        }

        // A LastLedgerSequence equal to the current ledger leaves no window to
        // collect signatures before the proposed transaction's own bound
        // passes, so it is rejected the same as one already in the past
        // (On-Chain Cosigner spec §5.3.2.2).
        {
            json::Value tx = payload(firstTicketSeq);
            tx[sfLastLedgerSequence.getJsonName()] = env.current()->seq();
            env(proposal::create(alice, tx, expiration), Ter(tecEXPIRED));
            env.close();
            BEAST_EXPECT(!proposal::entry(env, target, firstTicketSeq));
            BEAST_EXPECT(ownerCount(env, alice) == 0);
        }

        // With no ledger bound on the proposed transaction, the proposal is
        // created normally.
        {
            env(proposal::create(alice, payload(firstTicketSeq), expiration));
            env.close();
            BEAST_EXPECT(proposal::entry(env, target, firstTicketSeq));
            BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);
        }

        // The target and ticket already carry a proposal.
        {
            env(proposal::create(alice, payload(firstTicketSeq), expiration), Ter(tecDUPLICATE));
            env.close();
            BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);
        }

        // A different ticket of the same target is a different proposal.
        {
            env(proposal::create(alice, payload(firstTicketSeq + 1), expiration));
            env.close();
            BEAST_EXPECT(proposal::entry(env, target, firstTicketSeq + 1));
            BEAST_EXPECT(ownerCount(env, alice) == 2 * proposal::kProposalOwnerCount);
        }

        // The target account does not exist, so it can never sign.
        {
            env(proposal::create(
                    alice, proposal::unsignedPayload(env, pay(carol, bob, XRP(1)), 1), expiration),
                Ter(tecNO_TARGET));
            env.close();
            BEAST_EXPECT(!proposal::entry(env, carol, 1));
            BEAST_EXPECT(ownerCount(env, alice) == 2 * proposal::kProposalOwnerCount);
        }
    }

    // Only the target account itself, or an account on its SignerList, may
    // create a proposal against it. Otherwise any unrelated account could
    // spam or squat the target's Tickets with unwanted proposals (On-Chain
    // Cosigner V1 authorization scope).
    void
    testProposerAuthorization(FeatureBitset features)
    {
        testcase("reject proposal from an unauthorized proposer");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const target{"target"};
        Account const signer{"signer"};
        Account const stranger{"stranger"};
        Account const bob{"bob"};
        env.fund(XRP(10000), target, signer, stranger, bob);
        env.close();

        env(signers(target, 1, {{signer, 1}}));
        env.close();

        auto payload = [&](std::uint32_t ticketSeq) {
            return proposal::unsignedPayload(env, pay(target, bob, XRP(1)), ticketSeq);
        };

        // The target account itself needs no SignerList entry.
        {
            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            env(proposal::create(target, payload(ticketSeq), proposal::expiration(env, 100s)));
            env.close();
            BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
        }

        // An account on the target's SignerList may propose for it.
        {
            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            env(proposal::create(signer, payload(ticketSeq), proposal::expiration(env, 100s)));
            env.close();
            BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
        }

        // An account that is neither the target nor on its SignerList may not.
        {
            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            env(proposal::create(stranger, payload(ticketSeq), proposal::expiration(env, 100s)),
                Ter(tecNO_PERMISSION));
            env.close();
            BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
            BEAST_EXPECT(ownerCount(env, stranger) == 0);
        }

        // A target with no SignerList at all may only be proposed for by
        // itself.
        {
            Account const bare{"bare"};
            env.fund(XRP(10000), bare);
            env.close();

            std::uint32_t const ticketSeq = proposal::createTicket(env, bare);
            env(proposal::create(
                    stranger,
                    proposal::unsignedPayload(env, pay(bare, bob, XRP(1)), ticketSeq),
                    proposal::expiration(env, 100s)),
                Ter(tecNO_PERMISSION));
            env.close();
            BEAST_EXPECT(!proposal::entry(env, bare, ticketSeq));
        }
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
        json::Value tx = pay(alice, bob, XRP(1));
        tx[jss::Account] = toBase58(amm.ammAccount());
        json::Value const proposedTx = proposal::unsignedPayload(env, tx, 1);

        env(proposal::create(proposer, proposedTx, proposal::expiration(env, 100s)),
            Ter(tecNO_PERMISSION));
        env.close();
        BEAST_EXPECT(!proposal::entry(env, amm.ammAccount(), 1));
        BEAST_EXPECT(ownerCount(env, proposer) == 0);
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

        std::uint32_t const targetTicketSeq = proposal::createTicket(env, target);
        proposal::authorizeProposer(env, target, alice);

        // The proposed transaction is stored unsigned: no signature fields and
        // an empty SigningPubKey. It is ticket-based so unrelated target account
        // activity cannot invalidate it while signatures are collected.
        json::Value const proposedTx =
            proposal::unsignedPayload(env, pay(target, bob, XRP(1)), targetTicketSeq);

        std::uint32_t const expiration = proposal::expiration(env, 100s);

        env(proposal::create(alice, proposedTx, expiration));
        env.close();

        auto const sle = proposal::entry(env, target, targetTicketSeq);
        if (!BEAST_EXPECT(sle))
            return;

        BEAST_EXPECT(sle->getAccountID(sfOwner) == alice.id());
        BEAST_EXPECT(sle->getFieldU32(sfExpiration) == expiration);

        auto const stored = sle->getFieldObject(sfProposedTransaction);
        BEAST_EXPECT(stored.getAccountID(sfAccount) == target.id());
        BEAST_EXPECT(stored.getFieldU32(sfSequence) == 0);
        BEAST_EXPECT(stored.getFieldU32(sfTicketSequence) == targetTicketSeq);
        BEAST_EXPECT(stored.getFieldVL(sfSigningPubKey).empty());

        // The proposal reserves several owner increments against the proposer.
        // The target owns the Ticket used by the proposed transaction, plus
        // the SignerList that authorizes alice to propose for it.
        BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);
        BEAST_EXPECT(ownerCount(env, target) == 2);
    }

    // A proposal carries a transaction of any type: what the proposal requires
    // of the payload — unsigned, ticket-based, fee fixed — is independent of
    // the transaction being proposed, so anything a target account's signer
    // list could authorize can be proposed for it.
    void
    testOtherTransactionTypes(FeatureBitset features)
    {
        testcase("proposals for other transaction types");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};    // the proposer
        Account const target{"target"};  // the account the proposals are for
        Account const bob{"bob"};
        Account const gw{"gw"};
        // NOLINTNEXTLINE(readability-identifier-naming)
        auto const USD = gw["USD"];
        env.fund(XRP(10000), alice, target, bob, gw);
        env.close();
        proposal::authorizeProposer(env, target, alice);

        // One payload per transaction type, each straight from the generator
        // the ordinary tests for that type use.
        std::vector<json::Value> const payloads{
            noop(target),                    // AccountSet
            offer(target, USD(1), XRP(1)),   // OfferCreate
            trust(target, USD(1000)),        // TrustSet
            signers(target, 1, {{bob, 1}}),  // SignerListSet
            deposit::auth(target, bob),      // DepositPreauth
            token::mint(target, 0),          // NFTokenMint
        };

        // A proposal is keyed by target and ticket, so each payload needs its
        // own ticket.
        std::uint32_t const firstTicketSeq =
            proposal::createTicket(env, target, static_cast<std::uint32_t>(payloads.size()));
        std::uint32_t const expiration = proposal::expiration(env, 100s);

        for (std::size_t i = 0; i < payloads.size(); ++i)
        {
            std::uint32_t const ticketSeq = firstTicketSeq + static_cast<std::uint32_t>(i);
            env(proposal::create(
                alice, proposal::unsignedPayload(env, payloads[i], ticketSeq), expiration));
            env.close();
            BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
        }

        BEAST_EXPECT(ownerCount(env, alice) == payloads.size() * proposal::kProposalOwnerCount);
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
        proposal::authorizeProposer(env, borrower, alice);

        std::uint32_t const expiration = proposal::expiration(env, 100s);

        // LoanSet: the Counterparty's signature is collected later; it must
        // not be required up front.
        {
            std::uint32_t const ticketSeq = proposal::createTicket(env, borrower);

            json::Value const tx =
                proposal::unsignedPayload(env, loan::set(borrower, uint256{1}, 1'000), ticketSeq);

            env(proposal::create(alice, tx, expiration));
            env.close();
            BEAST_EXPECT(proposal::entry(env, borrower, ticketSeq));
        }

        // SponsorshipTransfer (account-level reserve sponsorship): the
        // Sponsor's signature is likewise collected later.
        {
            std::uint32_t const ticketSeq = proposal::createTicket(env, borrower);

            json::Value tx = sponsor::transfer(borrower, tfSponsorshipCreate);
            tx[sfSponsor.getJsonName()] = alice.human();
            tx[sfSponsorFlags.getJsonName()] = spfSponsorReserve;

            env(proposal::create(alice, proposal::unsignedPayload(env, tx, ticketSeq), expiration));
            env.close();
            BEAST_EXPECT(proposal::entry(env, borrower, ticketSeq));
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
        proposal::authorizeProposer(env, target, alice);

        std::uint32_t const targetTicketSeq = proposal::createTicket(env, target);

        // Fund alice just short of the reserve the proposal requires.
        env.fund(
            env.current()->fees().accountReserve(proposal::kProposalOwnerCount, 1) - drops(1),
            alice);
        env.close();

        std::uint32_t const expiration = proposal::expiration(env, 100s);
        json::Value const proposedTx =
            proposal::unsignedPayload(env, pay(target, bob, XRP(1)), targetTicketSeq);

        env(proposal::create(alice, proposedTx, expiration), Ter(tecINSUFFICIENT_RESERVE));
        env.close();
        BEAST_EXPECT(!proposal::entry(env, target, targetTicketSeq));
        BEAST_EXPECT(ownerCount(env, alice) == 0);

        env(pay(bob, alice, XRP(10)));
        env.close();

        env(proposal::create(alice, proposedTx, expiration));
        env.close();
        BEAST_EXPECT(proposal::entry(env, target, targetTicketSeq));
        BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);
    }

    // The proposal's reserve can instead be sponsored: the reserve is charged
    // to the sponsor's account, and the ledger object records the sponsor, the
    // same as any other reserve-sponsorable object (TransactionProposalCreate
    // is on the reserve-sponsorship allow-list).
    void
    testSponsoredReserve(FeatureBitset features)
    {
        testcase("proposer reserve sponsored");

        using namespace jtx;
        using namespace std::chrono_literals;

        // Reserve sponsorship requires the Sponsor amendment, independent of
        // Cosign: with Cosign enabled but Sponsor disabled, a proposal that
        // tries to attach a sponsor is rejected before it ever reaches the
        // reserve-sponsorship allow-list.
        {
            Env env{*this, features - featureSponsor};

            Account const alice{"alice"};
            Account const target{"target"};
            Account const bob{"bob"};
            Account const backer{"backer"};
            env.fund(XRP(10000), alice, target, bob, backer);
            env.close();
            proposal::authorizeProposer(env, target, alice);

            std::uint32_t const targetTicketSeq = proposal::createTicket(env, target);
            json::Value const proposedTx =
                proposal::unsignedPayload(env, pay(target, bob, XRP(1)), targetTicketSeq);

            env(proposal::create(alice, proposedTx, proposal::expiration(env, 100s)),
                sponsor::As(backer, spfSponsorReserve),
                Sig(sfSponsorSignature, backer),
                Ter(temDISABLED));
            env.close();
            BEAST_EXPECT(!proposal::entry(env, target, targetTicketSeq));
        }

        Env env{*this, features};

        Account const alice{"alice"};    // the proposer
        Account const target{"target"};  // the account the proposal is for
        Account const bob{"bob"};
        Account const backer{"backer"};  // sponsors alice's proposal reserve

        env.fund(XRP(10000), alice, target, bob, backer);
        env.close();
        proposal::authorizeProposer(env, target, alice);

        std::uint32_t const targetTicketSeq = proposal::createTicket(env, target);
        json::Value const proposedTx =
            proposal::unsignedPayload(env, pay(target, bob, XRP(1)), targetTicketSeq);

        env(proposal::create(alice, proposedTx, proposal::expiration(env, 100s)),
            sponsor::As(backer, spfSponsorReserve),
            Sig(sfSponsorSignature, backer));
        env.close();

        auto const sle = proposal::entry(env, target, targetTicketSeq);
        if (!BEAST_EXPECT(sle))
            return;

        BEAST_EXPECT(sle->isFieldPresent(sfSponsor));
        BEAST_EXPECT(sle->getAccountID(sfSponsor) == backer.id());

        // alice still owns the proposal — her OwnerCount reflects that, same
        // as an unsponsored proposal. What moves to the sponsor is the
        // reserve requirement itself, tracked separately: alice's owner count
        // is covered by backer's sponsorship rather than her own balance.
        BEAST_EXPECT(ownerCount(env, alice) == proposal::kProposalOwnerCount);
        BEAST_EXPECT(ownerCount(env, backer) == 0);
        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == proposal::kProposalOwnerCount);
        BEAST_EXPECT(sponsoringOwnerCount(env, backer) == proposal::kProposalOwnerCount);
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
        proposal::authorizeProposer(env, target, alice);

        std::uint32_t const targetTicketSeq = proposal::createTicket(env, target);

        // Both inner transactions are the outer account's own, so no further
        // signatures will be collected for them.
        json::Value const proposedTx = proposal::unsignedBatch(
            env,
            target,
            targetTicketSeq,
            tfAllOrNothing,
            {proposal::innerTx(pay(target, bob, XRP(1)), env.seq(target)),
             proposal::innerTx(pay(target, bob, XRP(1)), env.seq(target) + 1)});

        env(proposal::create(alice, proposedTx, proposal::expiration(env, 100s)));
        env.close();

        BEAST_EXPECT(proposal::entry(env, target, targetTicketSeq));
        BEAST_EXPECT(ownerCount(env, alice) == proposal::kBatchProposalOwnerCount);
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
        proposal::authorizeProposer(env, target, alice);

        std::uint32_t const targetTicketSeq = proposal::createTicket(env, target);

        // One inner from the outer account, one from bob: bob is a required
        // signer, so a direct submission would need his BatchSigners entry.
        json::Value const proposedTx = proposal::unsignedBatch(
            env,
            target,
            targetTicketSeq,
            tfAllOrNothing,
            {proposal::innerTx(pay(target, bob, XRP(1)), env.seq(target)),
             proposal::innerTx(pay(bob, target, XRP(1)), env.seq(bob))});

        env(proposal::create(alice, proposedTx, proposal::expiration(env, 100s)));
        env.close();

        auto const sle = proposal::entry(env, target, targetTicketSeq);
        if (!BEAST_EXPECT(sle))
            return;

        // The proposal is stored without any BatchSigners: the participants'
        // signatures are collected later through TransactionProposalSign.
        auto const stored = sle->getFieldObject(sfProposedTransaction);
        BEAST_EXPECT(!stored.isFieldPresent(sfBatchSigners));
        BEAST_EXPECT(ownerCount(env, alice) == proposal::kBatchProposalOwnerCount);
    }

    void
    run() override
    {
        using namespace jtx;

        FeatureBitset const all{testableAmendments()};

        testReserveCounts();

        // Preflight
        testDisabled(all);
        testRejectedPayload(all);
        testRejectedSignatureFields(all);

        // Preclaim
        testPreclaim(all);
        testProposerAuthorization(all);
        testPseudoTarget(all);

        // Apply
        testCreate(all);
        testOtherTransactionTypes(all);
        testAuxiliaryCoSignatureTypes(all);
        testReserve(all);
        testSponsoredReserve(all);
        testBatchReserve(all);
        testMultiAccountBatch(all);
    }
};

BEAST_DEFINE_TESTSUITE(TransactionProposalCreate, app, xrpl);

}  // namespace xrpl::test
