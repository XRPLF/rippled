#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/batch.h>
#include <test/jtx/flags.h>
#include <test/jtx/multisign.h>
#include <test/jtx/pay.h>
#include <test/jtx/regkey.h>
#include <test/jtx/sig.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/transactors/proposal/ProposalHelpers.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace xrpl::test {

struct TransactionProposalRPC_test : public beast::unit_test::Suite
{
    static constexpr auto kSigned = "signed";

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
        jtx::Env& env,
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

    // Parse a TransactionProposalCreate through the transaction machinery
    // (without submitting it) and extract the proposed transaction as the
    // typed STObject the ledger would store.
    static STObject
    parsedPayload(jtx::Env& env, jtx::Account const& proposer, json::Value const& proposedTx)
    {
        auto const jt =
            env.jt(proposalCreate(proposer, proposedTx, (env.now() + std::chrono::seconds(1000)).time_since_epoch().count()));
        return jt.stx->getFieldObject(sfProposedTransaction);
    }

    // A TransactionProposal ledger entry as TransactionProposalSign would
    // leave it after appending signatures: the evaluator only reads
    // ProposedTransaction and Expiration, everything else is boilerplate.
    static std::shared_ptr<SLE>
    makeProposalSLE(
        jtx::Account const& owner,
        STObject const& proposedTx,
        std::uint32_t expiration)
    {
        auto const target = proposedTx.getAccountID(sfAccount);
        auto const ticketSeq = proposedTx.getFieldU32(sfTicketSequence);
        auto sle = std::make_shared<SLE>(keylet::txProposal(target, ticketSeq));
        sle->setAccountID(sfOwner, owner.id());
        sle->setFieldObject(sfProposedTransaction, proposedTx);
        sle->setFieldU32(sfExpiration, expiration);
        sle->setFieldU64(sfOwnerNode, 0);
        sle->setFieldH256(sfPreviousTxnID, uint256{});
        sle->setFieldU32(sfPreviousTxnLgrSeq, 0);
        return sle;
    }

    static std::uint32_t
    farFuture(jtx::Env& env)
    {
        return (env.now() + std::chrono::seconds(1000)).time_since_epoch().count();
    }

    // Single-signature material: the crypto was verified when the signature
    // was appended on-ledger, so the evaluator only inspects the public key.
    static void
    singleSign(STObject& obj, PublicKey const& pk)
    {
        obj.setFieldVL(sfSigningPubKey, pk.slice());
        obj.setFieldVL(sfTxnSignature, Blob{0xDE, 0xAD, 0xBE, 0xEF});
    }

    static STObject
    makeSignerEntry(jtx::Account const& acct)
    {
        STObject obj(sfSigner);
        obj.setAccountID(sfAccount, acct.id());
        obj.setFieldVL(sfSigningPubKey, acct.pk().slice());
        obj.setFieldVL(sfTxnSignature, Blob{0xDE, 0xAD, 0xBE, 0xEF});
        return obj;
    }

    // Multi-signature material: Signers sorted by account ID, as the ledger
    // stores them.
    static void
    multiSign(STObject& obj, std::vector<jtx::Account> accounts)
    {
        std::sort(accounts.begin(), accounts.end(), [](auto const& a, auto const& b) {
            return a.id() < b.id();
        });
        STArray signers(sfSigners);
        for (auto const& acct : accounts)
            signers.push_back(makeSignerEntry(acct));
        obj.setFieldArray(sfSigners, signers);
    }

    static STObject
    makeBatchSigner(jtx::Account const& acct)
    {
        STObject obj(sfBatchSigner);
        obj.setAccountID(sfAccount, acct.id());
        obj.setFieldVL(sfSigningPubKey, acct.pk().slice());
        obj.setFieldVL(sfTxnSignature, Blob{0xDE, 0xAD, 0xBE, 0xEF});
        return obj;
    }

    static proposal::ProposalStatus
    evaluate(jtx::Env& env, std::shared_ptr<SLE> const& sle)
    {
        return proposal::evaluateProposal(*env.current(), *sle, env.journal);
    }

    static proposal::SignerStatus const*
    findSigner(proposal::ProposalStatus const& status, AccountID const& id)
    {
        for (auto const& signer : status.signers)
            if (signer.account == id)
                return &signer;
        return nullptr;
    }

    void
    testMalformedRequests(FeatureBitset features)
    {
        testcase("malformed requests");

        using namespace jtx;
        Env env{*this, features};

        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        auto const rpc = [&](json::Value const& params) {
            return env.rpc("json", "transaction_proposal", to_string(params))[jss::result];
        };

        // No addressing fields at all.
        {
            json::Value params{json::ValueType::Object};
            auto const jrr = rpc(params);
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
        }

        // proposal_id is not hex.
        {
            json::Value params{json::ValueType::Object};
            params[jss::proposal_id] = "not-hex";
            auto const jrr = rpc(params);
            BEAST_EXPECT(jrr[jss::error] == "malformedRequest");
        }

        // proposal_id combined with account/ticket_seq.
        {
            json::Value params{json::ValueType::Object};
            params[jss::proposal_id] = to_string(uint256{1});
            params[jss::account] = alice.human();
            params[jss::ticket_seq] = 1;
            auto const jrr = rpc(params);
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
        }

        // account without ticket_seq.
        {
            json::Value params{json::ValueType::Object};
            params[jss::account] = alice.human();
            auto const jrr = rpc(params);
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
        }

        // Malformed account.
        {
            json::Value params{json::ValueType::Object};
            params[jss::account] = "rNotAnAccount!!!";
            params[jss::ticket_seq] = 1;
            auto const jrr = rpc(params);
            BEAST_EXPECT(jrr[jss::error] == "malformedAddress");
        }

        // account of a wrong JSON type must be a parameter error, not an
        // internal one.
        {
            json::Value params{json::ValueType::Object};
            params[jss::account] = 42;
            params[jss::ticket_seq] = 1;
            auto const jrr = rpc(params);
            BEAST_EXPECT(jrr[jss::error] == "malformedAddress");
        }

        // ticket_seq that does not parse as a number. (Numeric strings are
        // accepted, matching ledger_entry's transaction_proposal addressing.)
        {
            json::Value params{json::ValueType::Object};
            params[jss::account] = alice.human();
            params[jss::ticket_seq] = "one";
            auto const jrr = rpc(params);
            BEAST_EXPECT(jrr[jss::error] == "malformedRequest");
        }

        // Well-formed but nonexistent.
        {
            json::Value params{json::ValueType::Object};
            params[jss::proposal_id] = to_string(uint256{42});
            auto const jrr = rpc(params);
            BEAST_EXPECT(jrr[jss::error] == "entryNotFound");
        }

        // An index that names a different ledger entry type: reads as absent,
        // not as a proposal.
        {
            json::Value params{json::ValueType::Object};
            params[jss::proposal_id] = to_string(keylet::account(alice.id()).key);
            auto const jrr = rpc(params);
            BEAST_EXPECT(jrr[jss::error] == "entryNotFound");
        }
    }

    void
    testPendingUnsigned(FeatureBitset features)
    {
        testcase("pending unsigned proposal via RPC");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        std::uint32_t const ticketSeq = env.seq(target) + 1;
        env(ticket::create(target, 1));
        env.close();

        std::uint32_t const expiration = (env.now() + 100s).time_since_epoch().count();
        env(proposalCreate(alice, unsignedPayload(env, target, bob, ticketSeq), expiration));
        env.close();

        auto const proposalKey = keylet::txProposal(target.id(), ticketSeq).key;

        auto const check = [&](json::Value const& jrr) {
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            BEAST_EXPECT(jrr[jss::proposal_id] == to_string(proposalKey));
            BEAST_EXPECT(
                jrr[jss::proposal][sfOwner.getJsonName()] == alice.human());
            auto const& signers = jrr[jss::signing_status];
            BEAST_EXPECT(signers.isArray() && signers.size() == 1);
            BEAST_EXPECT(signers[0u][jss::account] == target.human());
            BEAST_EXPECT(signers[0u][jss::role] == "account");
            BEAST_EXPECT(signers[0u][kSigned] == false);
            // No SignerList: no quorum to report.
            BEAST_EXPECT(!signers[0u].isMember(jss::quorum));
        };

        // By proposal_id.
        {
            json::Value params{json::ValueType::Object};
            params[jss::proposal_id] = to_string(proposalKey);
            check(env.rpc("json", "transaction_proposal", to_string(params))[jss::result]);
        }

        // By account + ticket_seq.
        {
            json::Value params{json::ValueType::Object};
            params[jss::account] = target.human();
            params[jss::ticket_seq] = ticketSeq;
            check(env.rpc("json", "transaction_proposal", to_string(params))[jss::result]);
        }

        // Once the target sets a SignerList, its live quorum is reported.
        env(signers(target, 2, {{bob, 1}, {alice, 1}}));
        env.close();
        {
            json::Value params{json::ValueType::Object};
            params[jss::proposal_id] = to_string(proposalKey);
            auto const jrr = env.rpc("json", "transaction_proposal", to_string(params))[jss::result];
            BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            auto const& signerStatus = jrr[jss::signing_status];
            BEAST_EXPECT(signerStatus[0u][jss::quorum] == 2);
        }
    }

    void
    testExpiredStates(FeatureBitset features)
    {
        testcase("expired proposal");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, target, bob);
        env.close();

        // Expiration reached: terminal even though nothing else changed.
        {
            std::uint32_t const ticketSeq = env.seq(target) + 1;
            env(ticket::create(target, 1));
            env.close();

            std::uint32_t const expiration = (env.now() + 60s).time_since_epoch().count();
            env(proposalCreate(alice, unsignedPayload(env, target, bob, ticketSeq), expiration));
            env.close();

            // Pass the expiration time.
            env.close(env.now() + 120s);

            json::Value params{json::ValueType::Object};
            params[jss::account] = target.human();
            params[jss::ticket_seq] = ticketSeq;
            auto const jrr = env.rpc("json", "transaction_proposal", to_string(params))[jss::result];
            BEAST_EXPECT(jrr[jss::proposal_status] == "expired");
        }

        // The proposed transaction's own LastLedgerSequence has passed: the
        // transaction can never enter a ledger, so the proposal is terminal
        // even though its Expiration is still far away.
        {
            std::uint32_t const ticketSeq = env.seq(target) + 1;
            env(ticket::create(target, 1));
            env.close();

            json::Value payload = unsignedPayload(env, target, bob, ticketSeq);
            std::uint32_t const lastLedgerSeq = env.current()->seq() + 2;
            payload[jss::LastLedgerSequence] = lastLedgerSeq;

            std::uint32_t const expiration = (env.now() + 1000s).time_since_epoch().count();
            env(proposalCreate(alice, payload, expiration));
            env.close();
            env.close();

            json::Value params{json::ValueType::Object};
            params[jss::account] = target.human();
            params[jss::ticket_seq] = ticketSeq;

            // Boundary: the RPC's default ledger is the open ledger, which
            // the proposed transaction can still enter when its
            // LastLedgerSequence equals that ledger's sequence.
            BEAST_EXPECT(env.current()->seq() == lastLedgerSeq);
            {
                auto const jrr =
                    env.rpc("json", "transaction_proposal", to_string(params))[jss::result];
                BEAST_EXPECT(jrr[jss::proposal_status] == "pending");
            }

            // One ledger later the bound has passed for good.
            env.close();
            {
                auto const jrr =
                    env.rpc("json", "transaction_proposal", to_string(params))[jss::result];
                BEAST_EXPECT(jrr[jss::proposal_status] == "expired");
            }
        }
    }

    void
    testSingleSignAuthorization(FeatureBitset features)
    {
        testcase("single-signature authorization currency");

        using namespace jtx;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        Account const regular{"regular"};
        env.fund(XRP(10000), alice, target, bob);
        // The regular key account never exists on ledger; jtx only needs to
        // know its keys to sign with them once the RegularKey is set.
        env.memoize(regular);
        env.close();

        std::uint32_t const ticketSeq = env.seq(target) + 1;
        env(ticket::create(target, 1));
        env.close();

        auto const base = parsedPayload(env, alice, unsignedPayload(env, target, bob, ticketSeq));

        // Unsigned: pending.
        {
            auto const status = evaluate(env, makeProposalSLE(alice, base, farFuture(env)));
            BEAST_EXPECT(status.state == proposal::ProposalState::pending);
            BEAST_EXPECT(status.signers.size() == 1);
            BEAST_EXPECT(!status.signers[0].satisfied);
        }

        // Master-key signed: complete.
        {
            STObject payload = base;
            singleSign(payload, target.pk());
            auto const status = evaluate(env, makeProposalSLE(alice, payload, farFuture(env)));
            BEAST_EXPECT(status.state == proposal::ProposalState::complete);
        }

        // A key unrelated to the target signs: not authorized.
        {
            STObject payload = base;
            singleSign(payload, bob.pk());
            auto const status = evaluate(env, makeProposalSLE(alice, payload, farFuture(env)));
            BEAST_EXPECT(status.state == proposal::ProposalState::pending);
        }

        // Regular-key signed: complete once the key is set...
        env(regkey(target, regular));
        env.close();
        {
            STObject payload = base;
            singleSign(payload, regular.pk());
            auto const status = evaluate(env, makeProposalSLE(alice, payload, farFuture(env)));
            BEAST_EXPECT(status.state == proposal::ProposalState::complete);
        }

        // ...and the master key still works while enabled...
        {
            STObject payload = base;
            singleSign(payload, target.pk());
            auto const status = evaluate(env, makeProposalSLE(alice, payload, farFuture(env)));
            BEAST_EXPECT(status.state == proposal::ProposalState::complete);
        }

        // ...but a master signature collected earlier no longer authorizes
        // once the master key is disabled: authorization is re-checked
        // against live state.
        // Disabling the master key must itself be signed with the master key
        // (jtx would otherwise sign with the regular key set above).
        env(fset(target, asfDisableMaster), Sig(target));
        env.close();
        {
            STObject payload = base;
            singleSign(payload, target.pk());
            auto const status = evaluate(env, makeProposalSLE(alice, payload, farFuture(env)));
            BEAST_EXPECT(status.state == proposal::ProposalState::pending);
        }

        // Terminal-first: a fully signed proposal past its expiration reports
        // expired, not complete.
        {
            STObject payload = base;
            singleSign(payload, regular.pk());
            std::uint32_t const past = (env.now() - std::chrono::seconds(10)).time_since_epoch().count();
            auto const status = evaluate(env, makeProposalSLE(alice, payload, past));
            BEAST_EXPECT(status.state == proposal::ProposalState::expired);
        }
    }

    void
    testMultiSignAuthorization(FeatureBitset features)
    {
        testcase("multi-signature quorum against the live SignerList");

        using namespace jtx;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const dest{"dest"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const dave{"dave"};
        Account const outsider{"outsider"};
        env.fund(XRP(10000), alice, target, dest, bob, carol, dave, outsider);
        env.close();

        std::uint32_t const ticketSeq = env.seq(target) + 1;
        env(ticket::create(target, 1));
        env.close();

        auto const base = parsedPayload(env, alice, unsignedPayload(env, target, dest, ticketSeq));

        // No SignerList yet: collected Signers cannot authorize anything.
        {
            STObject payload = base;
            multiSign(payload, {bob, carol});
            auto const status = evaluate(env, makeProposalSLE(alice, payload, farFuture(env)));
            BEAST_EXPECT(status.state == proposal::ProposalState::pending);
            BEAST_EXPECT(!status.signers[0].quorum.has_value());
            BEAST_EXPECT(status.signers[0].signedWeight == 0);
        }

        env(signers(target, 2, {{bob, 1}, {carol, 1}, {dave, 1}}));
        env.close();

        // One of three signers: quorum not met; progress is reported.
        {
            STObject payload = base;
            multiSign(payload, {bob});
            auto const status = evaluate(env, makeProposalSLE(alice, payload, farFuture(env)));
            BEAST_EXPECT(status.state == proposal::ProposalState::pending);
            BEAST_EXPECT(status.signers[0].signedWeight == 1);
            BEAST_EXPECT(status.signers[0].quorum == 2);
        }

        // Two of three: quorum met.
        {
            STObject payload = base;
            multiSign(payload, {bob, carol});
            auto const status = evaluate(env, makeProposalSLE(alice, payload, farFuture(env)));
            BEAST_EXPECT(status.state == proposal::ProposalState::complete);
            BEAST_EXPECT(status.signers[0].signedWeight == 2);
        }

        // A signer that is not on the live list poisons the whole set (the
        // ordinary submission path rejects it wholesale), and contributes no
        // weight.
        {
            STObject payload = base;
            multiSign(payload, {bob, carol, outsider});
            auto const status = evaluate(env, makeProposalSLE(alice, payload, farFuture(env)));
            BEAST_EXPECT(status.state == proposal::ProposalState::pending);
            BEAST_EXPECT(status.signers[0].signedWeight == 2);
        }

        // The list changed after signatures were collected: only the weight
        // still on the live list counts.
        env(signers(target, 2, {{bob, 1}, {dave, 1}}));
        env.close();
        {
            STObject payload = base;
            multiSign(payload, {bob, carol});
            auto const status = evaluate(env, makeProposalSLE(alice, payload, farFuture(env)));
            BEAST_EXPECT(status.state == proposal::ProposalState::pending);
            BEAST_EXPECT(status.signers[0].signedWeight == 1);
        }
    }

    void
    testDelegateAuthorization(FeatureBitset features)
    {
        testcase("delegated proposed transaction");

        using namespace jtx;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const delegate{"delegate"};
        Account const dest{"dest"};
        env.fund(XRP(10000), alice, target, delegate, dest);
        env.close();

        std::uint32_t const ticketSeq = env.seq(target) + 1;
        env(ticket::create(target, 1));
        env.close();

        json::Value payload = unsignedPayload(env, target, dest, ticketSeq);
        payload[sfDelegate.getJsonName()] = delegate.human();
        auto const base = parsedPayload(env, alice, payload);

        // The delegate, not the target, is the required signer.
        {
            auto const status = evaluate(env, makeProposalSLE(alice, base, farFuture(env)));
            BEAST_EXPECT(status.signers.size() == 1);
            BEAST_EXPECT(status.signers[0].account == delegate.id());
            BEAST_EXPECT(status.state == proposal::ProposalState::pending);
        }

        // The delegate's own signature satisfies it; the target's does not.
        {
            STObject signedPayload = base;
            singleSign(signedPayload, delegate.pk());
            auto const status = evaluate(env, makeProposalSLE(alice, signedPayload, farFuture(env)));
            BEAST_EXPECT(status.state == proposal::ProposalState::complete);
        }
        {
            STObject signedPayload = base;
            singleSign(signedPayload, target.pk());
            auto const status = evaluate(env, makeProposalSLE(alice, signedPayload, farFuture(env)));
            BEAST_EXPECT(status.state == proposal::ProposalState::pending);
        }
    }

    void
    testCounterpartyAuthorization(FeatureBitset features)
    {
        testcase("counterparty co-signature");

        using namespace jtx;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const borrower{"borrower"};
        Account const lender{"lender"};
        env.fund(XRP(10000), alice, borrower, lender);
        env.close();

        auto const makeLoanSet = [&](jtx::Account const& counterparty) {
            std::uint32_t const ticketSeq = env.seq(borrower) + 1;
            env(ticket::create(borrower, 1));
            env.close();

            json::Value tx = loan::set(borrower, uint256{1}, 1'000);
            tx[sfCounterparty.getJsonName()] = counterparty.human();
            tx[jss::Sequence] = 0;
            tx[sfTicketSequence.getJsonName()] = ticketSeq;
            tx[jss::Fee] = std::to_string(env.current()->fees().base.drops());
            tx[jss::SigningPubKey] = "";
            return parsedPayload(env, alice, tx);
        };

        // Explicit Counterparty: a distinct required signer row.
        {
            auto const base = makeLoanSet(lender);
            {
                auto const status = evaluate(env, makeProposalSLE(alice, base, farFuture(env)));
                BEAST_EXPECT(status.state == proposal::ProposalState::pending);
                BEAST_EXPECT(status.signers.size() == 2);
                auto const* row = findSigner(status, lender.id());
                BEAST_EXPECT(row && row->role == proposal::SignerRole::counterparty);
                BEAST_EXPECT(!row->satisfied);
            }

            // The lender co-signs through CounterpartySignature.
            STObject counterSigned = base;
            {
                STObject sig(sfCounterpartySignature);
                sig.setFieldVL(sfSigningPubKey, lender.pk().slice());
                sig.setFieldVL(sfTxnSignature, Blob{0xDE, 0xAD, 0xBE, 0xEF});
                counterSigned.setFieldObject(sfCounterpartySignature, sig);
            }
            {
                auto const status =
                    evaluate(env, makeProposalSLE(alice, counterSigned, farFuture(env)));
                BEAST_EXPECT(status.state == proposal::ProposalState::pending);
                BEAST_EXPECT(findSigner(status, lender.id())->satisfied);
                BEAST_EXPECT(!findSigner(status, borrower.id())->satisfied);
            }

            // Both authorizations present: complete.
            {
                STObject payload = counterSigned;
                singleSign(payload, borrower.pk());
                auto const status = evaluate(env, makeProposalSLE(alice, payload, farFuture(env)));
                BEAST_EXPECT(status.state == proposal::ProposalState::complete);
            }
        }

        // The implicit-counterparty rule is LoanSet's alone. Other types that
        // carry sfLoanBrokerID (here LoanBrokerCoverDeposit) require no
        // counterparty: exactly one signer row, and the initiator's own
        // signature completes it.
        {
            std::uint32_t const ticketSeq = env.seq(borrower) + 1;
            env(ticket::create(borrower, 1));
            env.close();

            json::Value tx = loanBroker::coverDeposit(borrower, uint256{1}, XRP(100).value());
            tx[jss::Sequence] = 0;
            tx[sfTicketSequence.getJsonName()] = ticketSeq;
            tx[jss::Fee] = std::to_string(env.current()->fees().base.drops());
            tx[jss::SigningPubKey] = "";

            STObject payload = parsedPayload(env, alice, tx);
            singleSign(payload, borrower.pk());
            auto const status = evaluate(env, makeProposalSLE(alice, payload, farFuture(env)));
            BEAST_EXPECT(status.signers.size() == 1);
            BEAST_EXPECT(status.state == proposal::ProposalState::complete);
        }
    }

    void
    testSponsorAuthorization(FeatureBitset features)
    {
        testcase("sponsor co-signature");

        using namespace jtx;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};
        Account const bob{"bob"};
        Account const patron{"patron"};  // the fee sponsor
        env.fund(XRP(10000), alice, target, bob, patron);
        env.close();

        std::uint32_t const ticketSeq = env.seq(target) + 1;
        env(ticket::create(target, 1));
        env.close();

        json::Value tx = unsignedPayload(env, target, bob, ticketSeq);
        tx[sfSponsor.getJsonName()] = patron.human();
        tx[sfSponsorFlags.getJsonName()] = spfSponsorFee;
        auto const base = parsedPayload(env, alice, tx);

        // The sponsor is a required signer; with no SponsorSignature and no
        // pre-authorizing Sponsorship entry it is unsatisfied even when the
        // target has signed.
        {
            STObject payload = base;
            singleSign(payload, target.pk());
            auto const status = evaluate(env, makeProposalSLE(alice, payload, farFuture(env)));
            BEAST_EXPECT(status.state == proposal::ProposalState::pending);
            auto const* row = findSigner(status, patron.id());
            BEAST_EXPECT(row && row->role == proposal::SignerRole::sponsor && !row->satisfied);
        }

        // A bare SponsorSignature placeholder must not fall back to the
        // Sponsorship-entry exemption: once the field exists, submission
        // validates it unconditionally.
        {
            STObject payload = base;
            singleSign(payload, target.pk());
            payload.setFieldObject(sfSponsorSignature, STObject(sfSponsorSignature));
            auto const status = evaluate(env, makeProposalSLE(alice, payload, farFuture(env)));
            BEAST_EXPECT(status.state == proposal::ProposalState::pending);
            BEAST_EXPECT(!findSigner(status, patron.id())->satisfied);
        }

        // The sponsor's own signature completes it.
        {
            STObject payload = base;
            singleSign(payload, target.pk());
            STObject sig(sfSponsorSignature);
            sig.setFieldVL(sfSigningPubKey, patron.pk().slice());
            sig.setFieldVL(sfTxnSignature, Blob{0xDE, 0xAD, 0xBE, 0xEF});
            payload.setFieldObject(sfSponsorSignature, sig);
            auto const status = evaluate(env, makeProposalSLE(alice, payload, farFuture(env)));
            BEAST_EXPECT(findSigner(status, patron.id())->satisfied);
            BEAST_EXPECT(status.state == proposal::ProposalState::complete);
        }
    }

    void
    testBatchAuthorization(FeatureBitset features)
    {
        testcase("proposed multi-account batch");

        using namespace jtx;

        Env env{*this, features};

        Account const alice{"alice"};
        Account const target{"target"};  // outer account of the batch
        Account const bob{"bob"};        // a distinct inner participant
        Account const carol{"carol"};
        Account const dave{"dave"};
        env.fund(XRP(10000), alice, target, bob, carol, dave);
        env.close();

        std::uint32_t const ticketSeq = env.seq(target) + 1;
        env(ticket::create(target, 1));
        env.close();

        auto const inner = [&](Account const& from, Account const& to, std::uint32_t seq) {
            json::Value tx = pay(from, to, XRP(1));
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
        proposedTx[sfTicketSequence.getJsonName()] = ticketSeq;
        proposedTx[jss::Fee] = std::to_string(batch::calcBatchFee(env, 1, 2).drops());
        proposedTx[jss::SigningPubKey] = "";
        proposedTx[jss::RawTransactions][0u][jss::RawTransaction] =
            inner(target, bob, env.seq(target));
        proposedTx[jss::RawTransactions][1u][jss::RawTransaction] = inner(bob, target, env.seq(bob));

        auto const base = parsedPayload(env, alice, proposedTx);

        // Unsigned: the outer account and the distinct participant are both
        // required; the inner from the outer account adds no extra row.
        {
            auto const status = evaluate(env, makeProposalSLE(alice, base, farFuture(env)));
            BEAST_EXPECT(status.state == proposal::ProposalState::pending);
            BEAST_EXPECT(status.signers.size() == 2);
            auto const* outer = findSigner(status, target.id());
            auto const* participant = findSigner(status, bob.id());
            BEAST_EXPECT(outer && outer->role == proposal::SignerRole::account && !outer->satisfied);
            BEAST_EXPECT(
                participant && participant->role == proposal::SignerRole::batchParticipant &&
                !participant->satisfied);
        }

        // The outer account signs the batch itself; bob is still missing.
        STObject outerSigned = base;
        singleSign(outerSigned, target.pk());
        {
            auto const status = evaluate(env, makeProposalSLE(alice, outerSigned, farFuture(env)));
            BEAST_EXPECT(status.state == proposal::ProposalState::pending);
            BEAST_EXPECT(findSigner(status, target.id())->satisfied);
            BEAST_EXPECT(!findSigner(status, bob.id())->satisfied);
        }

        // Bob's single-signed BatchSigners entry completes the proposal.
        {
            STObject payload = outerSigned;
            STArray batchSigners(sfBatchSigners);
            batchSigners.push_back(makeBatchSigner(bob));
            payload.setFieldArray(sfBatchSigners, batchSigners);
            auto const status = evaluate(env, makeProposalSLE(alice, payload, farFuture(env)));
            BEAST_EXPECT(status.state == proposal::ProposalState::complete);
        }

        // Bob authorizes through his own SignerList inside his BatchSigners
        // entry: quorum computed per participant.
        env(signers(bob, 2, {{carol, 1}, {dave, 1}}));
        env.close();
        {
            STObject payload = outerSigned;

            STObject bobSigner(sfBatchSigner);
            bobSigner.setAccountID(sfAccount, bob.id());
            // Multi-signing canonical form: SigningPubKey present and empty.
            bobSigner.setFieldVL(sfSigningPubKey, Blob{});
            {
                STObject multi(sfBatchSigner);  // temp holder for the array
                multiSign(multi, {carol});
                bobSigner.setFieldArray(sfSigners, multi.getFieldArray(sfSigners));
            }
            STArray batchSigners(sfBatchSigners);
            batchSigners.push_back(bobSigner);
            payload.setFieldArray(sfBatchSigners, batchSigners);

            auto const status = evaluate(env, makeProposalSLE(alice, payload, farFuture(env)));
            BEAST_EXPECT(status.state == proposal::ProposalState::pending);
            auto const* participant = findSigner(status, bob.id());
            BEAST_EXPECT(participant->signedWeight == 1 && participant->quorum == 2);
        }

        // An inner from an account that does not exist yet (an earlier inner
        // could create it) may be authorized by its own master key.
        {
            Account const phantom{"phantom"};  // never funded

            json::Value withPhantom = proposedTx;
            withPhantom[jss::RawTransactions][2u][jss::RawTransaction] =
                inner(phantom, target, 1);
            auto const phantomBase = parsedPayload(env, alice, withPhantom);

            STObject payload = phantomBase;
            singleSign(payload, target.pk());
            STArray batchSigners(sfBatchSigners);
            std::vector<jtx::Account> entries{bob, phantom};
            std::sort(entries.begin(), entries.end(), [](auto const& a, auto const& b) {
                return a.id() < b.id();
            });
            for (auto const& acct : entries)
                batchSigners.push_back(makeBatchSigner(acct));
            payload.setFieldArray(sfBatchSigners, batchSigners);

            auto const status = evaluate(env, makeProposalSLE(alice, payload, farFuture(env)));
            BEAST_EXPECT(findSigner(status, phantom.id())->satisfied);
            BEAST_EXPECT(status.state == proposal::ProposalState::complete);
        }
    }

    void
    run() override
    {
        using namespace jtx;
        auto const features = testableAmendments();
        testMalformedRequests(features);
        testPendingUnsigned(features);
        testExpiredStates(features);
        testSingleSignAuthorization(features);
        testMultiSignAuthorization(features);
        testDelegateAuthorization(features);
        testCounterpartyAuthorization(features);
        testSponsorAuthorization(features);
        testBatchAuthorization(features);
    }
};

BEAST_DEFINE_TESTSUITE(TransactionProposalRPC, rpc, xrpl);

}  // namespace xrpl::test
