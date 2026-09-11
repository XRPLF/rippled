#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/delegate.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/multisign.h>
#include <test/jtx/pay.h>
#include <test/jtx/proposal.h>
#include <test/jtx/regkey.h>
#include <test/jtx/sig.h>
#include <test/jtx/sponsor.h>
#include <test/jtx/tags.h>
#include <test/jtx/ter.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/helpers/ProposalHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <optional>
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

    // Build a TransactionProposalSign where ProposalSignature.Account and the
    // key that produces SigningPubKey/TxnSignature come from different
    // accounts. proposal::sign always uses the same account for both, so any
    // test exercising the "signer account vs. presenting key" split — a
    // regular key, a phantom multi-signer, a plain key mismatch — needs this.
    json::Value
    signAs(
        jtx::Env const& env,
        jtx::Account const& submitter,
        jtx::Account const& target,
        std::uint32_t ticketSeq,
        jtx::Account const& signingFor,
        jtx::Account const& signerAccount,
        jtx::Account const& signingKey)
    {
        auto const sle = jtx::proposal::entry(env, target, ticketSeq);
        if (!BEAST_EXPECT(sle))
            return {};
        STObject const proposedTx = sle->getFieldObject(sfProposedTransaction);
        // No callers use signAs with a LoanSet payload — this shortcut path
        // doesn't need to resolve an implicit LoanBroker.Owner. If one shows
        // up later, mirror the resolution jtx::proposal::sign does.
        auto const data = xrpl::proposal::signingData(
            proposedTx,
            signingFor.id(),
            signerAccount.id(),
            signingKey.pk().slice(),
            env.current()->rules());
        if (!BEAST_EXPECT(data))
            return {};
        auto const sig = xrpl::sign(signingKey.pk(), signingKey.sk(), data->slice());

        json::Value jv;
        jv[jss::TransactionType] = "TransactionProposalSign";
        jv[jss::Account] = submitter.human();
        jv[sfProposalID.jsonName] = to_string(jtx::proposal::id(target, ticketSeq));
        jv[sfSigningFor.jsonName] = signingFor.human();
        auto& ps = jv[sfProposalSignature.jsonName];
        ps[jss::Account] = signerAccount.human();
        ps[jss::SigningPubKey] = strHex(signingKey.pk().slice());
        ps[jss::TxnSignature] = strHex(Slice{sig.data(), sig.size()});
        return jv;
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
            // Non-empty but unparseable SigningPubKey — rejected by
            // preflight's stateless key-format check before any ledger
            // fetch happens.
            json::Value jv = proposal::sign(env, ceo, target, ticketSeq, target, ceo);
            jv[sfProposalSignature.jsonName][jss::SigningPubKey] = "00";
            env(jv, Ter(temMALFORMED));
            env.close();
        }

        {
            // A broken signature reaches preclaim (preflight only checks
            // fields are non-empty and the key parses) and fails there
            // with the claimed-fee code the rest of preclaim uses.
            json::Value jv = proposal::sign(env, ceo, target, ticketSeq, target, ceo);
            jv[sfProposalSignature.jsonName][jss::TxnSignature] = "00";
            env(jv, Ter(tecNO_PERMISSION));
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
    testDelegatedPayloadRejectsAccountSigningFor(FeatureBitset features)
    {
        testcase("delegated payload rejects SigningFor = Account");

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

        // The Delegate authorizes this payload; the Account does not. Its
        // contribution must be refused rather than written into the payload's
        // own signature slot, where it would lock the Delegate out for good.
        env(proposal::sign(env, target, target, ticketSeq, target, target), Ter(tecNO_PERMISSION));
        env.close();

        {
            auto const sle = proposal::entry(env, target, ticketSeq);
            if (!BEAST_EXPECT(sle))
                return;
            auto const stored = sle->getFieldObject(sfProposedTransaction);
            BEAST_EXPECT(stored.getFieldVL(sfSigningPubKey).empty());
            BEAST_EXPECT(!stored.isFieldPresent(sfTxnSignature));
        }

        // The Delegate's signer is still able to contribute and complete it.
        env(proposal::sign(env, ds1, target, ticketSeq, delegateAcct, ds1));
        env.close();

        env(proposedJson(env, target, ticketSeq), Sig(kNone));
        env.close();

        BEAST_EXPECT(!proposal::entry(env, target, ticketSeq));
        BEAST_EXPECT(env.balance(dest) == XRP(10000) + XRP(1));
    }

    // A LoanSet payload with an explicit sfCounterparty: the Counterparty's
    // contribution lands in the proposed transaction's sfCounterpartySignature
    // slot, not in outer Signers. Covers auxiliaryRole's explicit-counterparty
    // recognition and recordContribution's outer-aux routing.
    void
    testOuterCounterpartyExplicit(FeatureBitset features)
    {
        testcase("outer counterparty single- and multi-sign, explicit sfCounterparty");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const borrower{"borrower"};    // proposed LoanSet's Account
        Account const lender{"lender"};        // the explicit sfCounterparty
        Account const lenderCFO{"lenderCFO"};  // multi-signer for lender
        env.fund(XRP(10000), borrower, lender, lenderCFO);
        env.close();

        // LoanSet's LoanBroker doesn't need to exist for a proposal that
        // carries an explicit sfCounterparty; auxiliaryRole matches the
        // field directly and never consults implicitCounterparty. A single
        // proposal with two tickets exercises both signature modes.
        std::uint32_t const ticketSeq = proposal::createTicket(env, borrower, 2);

        auto makeLoanSet = [&](std::uint32_t ts, std::uint32_t extraSigners) {
            json::Value tx = loan::set(borrower, uint256{1}, 1'000);
            tx[sfCounterparty.jsonName] = lender.human();
            return proposal::unsignedPayload(env, tx, ts, extraSigners);
        };

        // Case 1: single-sign the Counterparty as itself. SigningFor = lender,
        // signer = lender (single-sign). Payload uses HashPrefix::CounterpartyTxSign
        // once fixCleanup3_4_0 is enabled; the signature lands in
        // sfCounterpartySignature.{SigningPubKey,TxnSignature}. The outer
        // top-level SigningPubKey stays empty.
        {
            env(proposal::create(
                borrower,
                makeLoanSet(ticketSeq, /*extraSigners=*/0),
                proposal::expiration(env, 100s)));
            env.close();

            env(proposal::sign(env, lender, borrower, ticketSeq, lender, lender));
            env.close();

            auto const sle = proposal::entry(env, borrower, ticketSeq);
            if (!BEAST_EXPECT(sle))
                return;
            auto const stored = sle->getFieldObject(sfProposedTransaction);

            // No mutation of the outer Signers / top-level SigningPubKey:
            // this is not a Transaction-role contribution.
            BEAST_EXPECT(!stored.isFieldPresent(sfSigners));
            BEAST_EXPECT(stored.isFieldPresent(sfSigningPubKey));
            BEAST_EXPECT(stored.getFieldVL(sfSigningPubKey).empty());
            BEAST_EXPECT(!stored.isFieldPresent(sfTxnSignature));

            // The Counterparty slot is filled in single-sign mode: presenter
            // key + signature at the top of sfCounterpartySignature, no
            // nested Signers array.
            if (!BEAST_EXPECT(stored.isFieldPresent(sfCounterpartySignature)))
                return;
            auto const cs = stored.getFieldObject(sfCounterpartySignature);
            BEAST_EXPECT(cs.isFieldPresent(sfSigningPubKey));
            BEAST_EXPECT(
                cs.getFieldVL(sfSigningPubKey) == Blob(lender.pk().begin(), lender.pk().end()));
            BEAST_EXPECT(cs.isFieldPresent(sfTxnSignature));
            BEAST_EXPECT(!cs.isFieldPresent(sfSigners));
        }

        // Case 2: multi-sign the Counterparty via its own SignerList.
        // Payload uses HashPrefix::CounterpartyTxMultiSign; the presenter's
        // Signer entry lands inside sfCounterpartySignature.Signers.
        env(signers(lender, 1, {{lenderCFO, 1}}));
        env.close();

        std::uint32_t const ticketSeq2 = ticketSeq + 1;
        {
            env(proposal::create(
                borrower,
                makeLoanSet(ticketSeq2, /*extraSigners=*/1),
                proposal::expiration(env, 100s)));
            env.close();

            env(proposal::sign(env, borrower, borrower, ticketSeq2, lender, lenderCFO));
            env.close();

            auto const sle = proposal::entry(env, borrower, ticketSeq2);
            if (!BEAST_EXPECT(sle))
                return;
            auto const stored = sle->getFieldObject(sfProposedTransaction);

            if (!BEAST_EXPECT(stored.isFieldPresent(sfCounterpartySignature)))
                return;
            auto const cs = stored.getFieldObject(sfCounterpartySignature);

            // Multi-sign mode: top of the aux slot has an empty SigningPubKey
            // placeholder, no top-level TxnSignature, and a Signers array
            // with the presenter's entry.
            BEAST_EXPECT(cs.isFieldPresent(sfSigningPubKey));
            BEAST_EXPECT(cs.getFieldVL(sfSigningPubKey).empty());
            BEAST_EXPECT(!cs.isFieldPresent(sfTxnSignature));
            if (!BEAST_EXPECT(cs.isFieldPresent(sfSigners)))
                return;
            auto const& innerSigners = cs.getFieldArray(sfSigners);
            BEAST_EXPECT(innerSigners.size() == 1);
            BEAST_EXPECT(innerSigners[0].getAccountID(sfAccount) == lenderCFO.id());
        }
    }

    // A LoanSet payload without sfCounterparty defaults to LoanBroker.Owner
    // (XLS-66 §3.8), which TransactionProposalSign::preclaim resolves at
    // Sign time by reading the LoanBroker off the ledger. The Broker's
    // owner then becomes the effective Counterparty, and its signature is
    // routed the same as the explicit case. Two shapes are tested: broker
    // exists (positive), and broker missing (negative — no Counterparty
    // can be identified, so the contribution is not required).
    void
    testOuterCounterpartyImplicit(FeatureBitset features)
    {
        testcase("outer counterparty implicit LoanBroker.Owner fallback");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const lender{"lender"};      // LoanBroker.Owner (implicit CP)
        Account const borrower{"borrower"};  // proposed LoanSet's Account
        env.fund(XRP(10'000), lender, borrower);
        env.close();

        // Build a closed-ended vault the lender owns, deposit into it while
        // still in the Subscription phase, then advance the ledger past
        // SubscriptionDate so LoanBrokerSet::preclaim accepts it. Mirrors
        // the setup LendingHelpers_test uses.
        Vault const vault{env};
        auto [vaultTx, vaultKeylet, subscriptionDate] =
            vault.createClosedEnded({.owner = lender, .asset = xrpIssue()});
        env(vaultTx);
        env.close();
        env(vault.deposit({.depositor = lender, .id = vaultKeylet.key, .amount = XRP(1'000)}));
        env.close();
        vault.closePastSubscription(subscriptionDate);

        auto const brokerKeylet =
            keylet::loanBroker(lender.id(), SeqProxy::rawSequence(env.seq(lender)));
        env(loan_broker::set(lender, vaultKeylet.key));
        env.close();
        if (!BEAST_EXPECT(env.le(brokerKeylet)))
            return;

        // Positive path: propose a LoanSet against the real broker with no
        // sfCounterparty. Sign as lender (the broker's owner); preclaim
        // resolves that fallback and routes the signature into the
        // Counterparty slot.
        {
            std::uint32_t const ticketSeq = proposal::createTicket(env, borrower);

            json::Value const tx = proposal::unsignedPayload(
                env, loan::set(borrower, brokerKeylet.key, 1'000), ticketSeq);
            env(proposal::create(borrower, tx, proposal::expiration(env, 100s)));
            env.close();

            env(proposal::sign(env, lender, borrower, ticketSeq, lender, lender));
            env.close();

            auto const sle = proposal::entry(env, borrower, ticketSeq);
            if (!BEAST_EXPECT(sle))
                return;
            auto const stored = sle->getFieldObject(sfProposedTransaction);
            if (!BEAST_EXPECT(stored.isFieldPresent(sfCounterpartySignature)))
                return;
            auto const cs = stored.getFieldObject(sfCounterpartySignature);
            BEAST_EXPECT(
                cs.getFieldVL(sfSigningPubKey) == Blob(lender.pk().begin(), lender.pk().end()));
            BEAST_EXPECT(cs.isFieldPresent(sfTxnSignature));
        }

        // Negative path: a LoanSet whose sfLoanBrokerID refers to no ledger
        // entry has no resolvable Counterparty. lender is not the initiator
        // and not an explicit sfCounterparty, so the Sign is rejected as
        // "SigningFor is not required by the proposed transaction" — the
        // same tec code as any other unrelated signer.
        {
            std::uint32_t const ticketSeq = proposal::createTicket(env, borrower);

            json::Value const tx =
                proposal::unsignedPayload(env, loan::set(borrower, uint256{1}, 1'000), ticketSeq);
            env(proposal::create(borrower, tx, proposal::expiration(env, 100s)));
            env.close();

            env(proposal::sign(env, lender, borrower, ticketSeq, lender, lender),
                Ter(tecNO_PERMISSION));
            env.close();

            auto const sle = proposal::entry(env, borrower, ticketSeq);
            if (!BEAST_EXPECT(sle))
                return;
            auto const stored = sle->getFieldObject(sfProposedTransaction);
            BEAST_EXPECT(!stored.isFieldPresent(sfCounterpartySignature));
        }
    }

    // A sponsored payload (sfSponsor + sfSponsorFlags): the Sponsor's
    // contribution lands in sfSponsorSignature, symmetric to the
    // Counterparty case. Covers auxiliaryRole's Sponsor recognition and
    // the multi-sign path against the sponsor's own SignerList.
    void
    testOuterSponsor(FeatureBitset features)
    {
        testcase("outer sponsor single- and multi-sign");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const target{"target"};
        Account const dest{"dest"};
        Account const backer{"backer"};        // the sfSponsor account
        Account const backerCFO{"backerCFO"};  // multi-signer for backer
        env.fund(XRP(10000), target, dest, backer, backerCFO);
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target, 2);

        auto makeSponsoredPayment = [&](std::uint32_t ts, std::uint32_t extraSigners) {
            json::Value tx = pay(target, dest, XRP(1));
            tx[sfSponsor.jsonName] = backer.human();
            tx[sfSponsorFlags.jsonName] = spfSponsorFee;
            return proposal::unsignedPayload(env, tx, ts, extraSigners);
        };

        // Case 1: sponsor single-signs.
        {
            env(proposal::create(
                target,
                makeSponsoredPayment(ticketSeq, /*extraSigners=*/0),
                proposal::expiration(env, 100s)));
            env.close();

            env(proposal::sign(env, backer, target, ticketSeq, backer, backer));
            env.close();

            auto const sle = proposal::entry(env, target, ticketSeq);
            if (!BEAST_EXPECT(sle))
                return;
            auto const stored = sle->getFieldObject(sfProposedTransaction);
            if (!BEAST_EXPECT(stored.isFieldPresent(sfSponsorSignature)))
                return;
            auto const ss = stored.getFieldObject(sfSponsorSignature);
            BEAST_EXPECT(
                ss.getFieldVL(sfSigningPubKey) == Blob(backer.pk().begin(), backer.pk().end()));
            BEAST_EXPECT(ss.isFieldPresent(sfTxnSignature));
            BEAST_EXPECT(!ss.isFieldPresent(sfSigners));
            // The Counterparty slot is unrelated.
            BEAST_EXPECT(!stored.isFieldPresent(sfCounterpartySignature));
            // No initiator contribution here — the outer Signers / top-level
            // sfTxnSignature remain unfilled.
            BEAST_EXPECT(!stored.isFieldPresent(sfSigners));
            BEAST_EXPECT(!stored.isFieldPresent(sfTxnSignature));
        }

        // Case 2: sponsor multi-signs via its own SignerList.
        env(signers(backer, 1, {{backerCFO, 1}}));
        env.close();

        std::uint32_t const ticketSeq2 = ticketSeq + 1;
        {
            env(proposal::create(
                target,
                makeSponsoredPayment(ticketSeq2, /*extraSigners=*/1),
                proposal::expiration(env, 100s)));
            env.close();

            env(proposal::sign(env, target, target, ticketSeq2, backer, backerCFO));
            env.close();

            auto const sle = proposal::entry(env, target, ticketSeq2);
            if (!BEAST_EXPECT(sle))
                return;
            auto const stored = sle->getFieldObject(sfProposedTransaction);
            if (!BEAST_EXPECT(stored.isFieldPresent(sfSponsorSignature)))
                return;
            auto const ss = stored.getFieldObject(sfSponsorSignature);
            if (!BEAST_EXPECT(ss.isFieldPresent(sfSigners)))
                return;
            auto const& innerSigners = ss.getFieldArray(sfSigners);
            BEAST_EXPECT(innerSigners.size() == 1);
            BEAST_EXPECT(innerSigners[0].getAccountID(sfAccount) == backerCFO.id());
        }
    }

    // SigningFor plays two outer roles at once: Counterparty and Sponsor.
    // Under fixCleanup3_4_0 the two slots require signatures over distinct
    // payloads, so a single contribution cannot satisfy both; preclaim
    // rejects the ambiguous case with tecNO_PERMISSION.
    void
    testMultiRoleAmbiguity(FeatureBitset features)
    {
        testcase("outer multi-role ambiguity rejected");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const borrower{"borrower"};
        Account const bothRoles{"bothRoles"};  // Counterparty AND Sponsor
        env.fund(XRP(10000), borrower, bothRoles);
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, borrower);

        // A LoanSet whose lender both supplies the counterparty signature and
        // sponsors the transaction's fee. Syntactically valid; every field
        // Cosigner uses for role dispatch matches the same account.
        json::Value tx = loan::set(borrower, uint256{1}, 1'000);
        tx[sfCounterparty.jsonName] = bothRoles.human();
        tx[sfSponsor.jsonName] = bothRoles.human();
        tx[sfSponsorFlags.jsonName] = spfSponsorFee;
        env(proposal::create(
            borrower,
            proposal::unsignedPayload(env, tx, ticketSeq),
            proposal::expiration(env, 100s)));
        env.close();

        env(proposal::sign(env, bothRoles, borrower, ticketSeq, bothRoles, bothRoles),
            Ter(tecNO_PERMISSION));
        env.close();

        // Neither slot is populated — the whole contribution is rejected,
        // not silently routed to one slot.
        auto const sle = proposal::entry(env, borrower, ticketSeq);
        if (!BEAST_EXPECT(sle))
            return;
        auto const stored = sle->getFieldObject(sfProposedTransaction);
        BEAST_EXPECT(!stored.isFieldPresent(sfCounterpartySignature));
        BEAST_EXPECT(!stored.isFieldPresent(sfSponsorSignature));
    }

    // Companion to testMultiRoleAmbiguity for the initiator + aux combo.
    // preflight1Sponsor rejects sfSponsor == sfAccount but does *not* reject
    // sfSponsor == sfDelegate — a Delegate authorized to submit on behalf of
    // the target may still coincide with the transaction's Sponsor. In that
    // case the same account plays both the initiator role (via Delegate) and
    // the Sponsor role at Sign time; hasAmbiguousOuterRole catches it and
    // returns tecNO_PERMISSION rather than silently routing to one slot.
    void
    testInitiatorAuxAmbiguity(FeatureBitset features)
    {
        testcase("Delegate + Sponsor same account rejected");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const target{"target"};
        Account const dest{"dest"};
        Account const dualRole{"dualRole"};  // Delegate AND Sponsor
        env.fund(XRP(10000), target, dest, dualRole);
        env.close();

        // Grant dualRole permission to submit Payment on target's behalf.
        env(delegate::set(target, dualRole, {"Payment"}));
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);

        // A Payment where the same account is both the Delegate that would
        // submit on target's behalf and the fee sponsor. Legal ledger-wise
        // — preflight1Sponsor's Account==Sponsor guard doesn't apply to a
        // Delegate — but ambiguous at Sign time.
        json::Value tx = pay(target, dest, XRP(1));
        tx[sfDelegate.jsonName] = dualRole.human();
        tx[sfSponsor.jsonName] = dualRole.human();
        tx[sfSponsorFlags.jsonName] = static_cast<std::uint32_t>(spfSponsorFee);
        env(proposal::create(
            target,
            proposal::unsignedPayload(env, tx, ticketSeq),
            proposal::expiration(env, 100s)));
        env.close();

        // SigningFor = dualRole matches both the initiator slot (as Delegate)
        // and the Sponsor slot; hasAmbiguousOuterRole rejects.
        env(proposal::sign(env, dualRole, target, ticketSeq, dualRole, dualRole),
            Ter(tecNO_PERMISSION));
        env.close();

        // Neither slot mutated.
        auto const sle = proposal::entry(env, target, ticketSeq);
        if (!BEAST_EXPECT(sle))
            return;
        auto const stored = sle->getFieldObject(sfProposedTransaction);
        BEAST_EXPECT(!stored.isFieldPresent(sfSigners));
        BEAST_EXPECT(
            !stored.isFieldPresent(sfSigningPubKey) || stored.getFieldVL(sfSigningPubKey).empty());
        BEAST_EXPECT(!stored.isFieldPresent(sfTxnSignature));
        BEAST_EXPECT(!stored.isFieldPresent(sfSponsorSignature));
    }

    // A signature computed with the Transaction-role prefix cannot be
    // accepted for a Sponsor SigningFor (and vice versa) once
    // fixCleanup3_4_0 is enabled: verify() rejects the payload and preclaim
    // returns tecNO_PERMISSION. This is the property #8162 was written to
    // defend, projected onto the on-chain cosigner path.
    void
    testRolePrefixEnforced(FeatureBitset features)
    {
        testcase("role prefix cross-swap rejected");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const target{"target"};
        Account const dest{"dest"};
        Account const backer{"backer"};
        env.fund(XRP(10000), target, dest, backer);
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);

        json::Value payload = pay(target, dest, XRP(1));
        payload[sfSponsor.jsonName] = backer.human();
        payload[sfSponsorFlags.jsonName] = spfSponsorFee;
        env(proposal::create(
            target,
            proposal::unsignedPayload(env, payload, ticketSeq),
            proposal::expiration(env, 100s)));
        env.close();

        // Hand-roll a Sign whose TxnSignature was made with the Transaction
        // prefix (HashPrefix::TxSign) — as if the signer had used its own
        // account key on the standard single-sign payload — but submit it
        // with SigningFor = backer, which routes to the Sponsor slot. The
        // verify() call in preclaim rebuilds the payload under the Sponsor
        // prefix and rejects the signature.
        auto const sle = proposal::entry(env, target, ticketSeq);
        if (!BEAST_EXPECT(sle))
            return;
        auto const stored = sle->getFieldObject(sfProposedTransaction);

        STTx stx{STObject{stored}};
        stx.setFieldVL(sfSigningPubKey, backer.pk().slice());
        Serializer wrongPayload;
        wrongPayload.add32(HashPrefix::TxSign);
        stx.addWithoutSigningFields(wrongPayload);
        auto const wrongSig = xrpl::sign(backer.pk(), backer.sk(), wrongPayload.slice());

        json::Value jv;
        jv[jss::TransactionType] = "TransactionProposalSign";
        jv[jss::Account] = backer.human();
        jv[sfProposalID.jsonName] = to_string(proposal::id(target, ticketSeq));
        jv[sfSigningFor.jsonName] = backer.human();
        auto& ps = jv[sfProposalSignature.jsonName];
        ps[jss::Account] = backer.human();
        ps[jss::SigningPubKey] = strHex(backer.pk().slice());
        ps[jss::TxnSignature] = strHex(Slice{wrongSig.data(), wrongSig.size()});
        env(jv, Ter(tecNO_PERMISSION));
        env.close();

        // The Sponsor slot remains empty; the wrong-prefix signature was not
        // recorded.
        auto const sleAfter = proposal::entry(env, target, ticketSeq);
        if (!BEAST_EXPECT(sleAfter))
            return;
        auto const storedAfter = sleAfter->getFieldObject(sfProposedTransaction);
        BEAST_EXPECT(!storedAfter.isFieldPresent(sfSponsorSignature));
    }

    // The duplicate / mode-conflict checks that already guard the outer
    // Signers / BatchSigners paths apply to the aux slots the same way:
    // recordIntoSigners is called against the aux STObject, so the same
    // tec codes come back. Exercised on the Sponsor slot (Counterparty is
    // symmetric; one is enough).
    void
    testAuxSlotDuplicateAndModeConflict(FeatureBitset features)
    {
        testcase("aux slot duplicate and single-then-multi conflict");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const target{"target"};
        Account const dest{"dest"};
        Account const backer{"backer"};
        Account const backerCFO{"backerCFO"};
        env.fund(XRP(10000), target, dest, backer, backerCFO);
        env.close();

        env(signers(backer, 1, {{backerCFO, 1}}));
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);

        json::Value payload = pay(target, dest, XRP(1));
        payload[sfSponsor.jsonName] = backer.human();
        payload[sfSponsorFlags.jsonName] = spfSponsorFee;
        env(proposal::create(
            target,
            proposal::unsignedPayload(env, payload, ticketSeq, /*extraSigners=*/1),
            proposal::expiration(env, 100s)));
        env.close();

        // Populate the Sponsor slot with a single-sign contribution first.
        env(proposal::sign(env, backer, target, ticketSeq, backer, backer));
        env.close();

        // A second single-sign of the same key on the same slot is a
        // duplicate.
        env(proposal::sign(env, backer, target, ticketSeq, backer, backer), Ter(tecDUPLICATE));
        env.close();

        // A multi-sign contribution against the same slot is now a mode
        // conflict: single-sign and multi-sign cannot coexist in one slot.
        env(proposal::sign(env, target, target, ticketSeq, backer, backerCFO),
            Ter(tecNO_PERMISSION));
        env.close();
    }

    // The TransactionProposalSign transaction itself may be fee-sponsored:
    // the outer sfSponsor / sfSponsorFlags annotation is handled by the
    // standard Transactor path (checkFee / getFeePayer), so cosign accepts
    // the contribution normally and the fee is charged to the sponsor's
    // pre-funded fee balance instead of the submitter's account. Verifies
    // no cross-talk between the Sponsor field on the outer Sign and the
    // signature-routing on the proposal.
    void
    testSignBeingSponsored(FeatureBitset features)
    {
        testcase("outer TransactionProposalSign carrying its own sfSponsor");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const target{"target"};
        Account const dest{"dest"};
        Account const backer{"backer"};  // fee-sponsor of the Sign tx
        env.fund(XRP(10000), target, dest, backer);
        env.close();

        // Pre-funded fee sponsorship: backer will absorb target's tx fees
        // up to the funded amount. No lsfSponsorshipRequireSignForFee, so
        // the outer Sign does not need a co-signing sfSponsorSignature.
        env(sponsor::set_fee(backer, 0, XRP(1)), sponsor::SponseeAcc(target), Fee(XRP(1)));
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        env(proposal::create(
            target,
            proposal::unsignedPayload(env, pay(target, dest, XRP(1)), ticketSeq),
            proposal::expiration(env, 100s)));
        env.close();

        auto const targetBefore = env.balance(target);

        // Build the Sign, annotate it as fee-sponsored by backer, and let env
        // recompute the outer signature over the augmented payload. The
        // ProposalSignature (inner signature over the proposed transaction)
        // was already sealed by proposal::sign against the current rules and
        // stays intact through the outer re-sign.
        json::Value signJson = proposal::sign(env, target, target, ticketSeq, target, target);
        signJson[sfSponsor.jsonName] = backer.human();
        signJson[sfSponsorFlags.jsonName] = static_cast<std::uint32_t>(spfSponsorFee);
        signJson.removeMember(jss::TxnSignature);
        signJson.removeMember(jss::SigningPubKey);
        env(signJson);
        env.close();

        // Fee did not come out of target — sponsor absorbed it.
        BEAST_EXPECT(env.balance(target) == targetBefore);

        // Signature was recorded on the proposal exactly as if no sponsor
        // were involved.
        auto const sle = proposal::entry(env, target, ticketSeq);
        if (!BEAST_EXPECT(sle))
            return;
        auto const stored = sle->getFieldObject(sfProposedTransaction);
        BEAST_EXPECT(stored.isFieldPresent(sfTxnSignature));
        BEAST_EXPECT(!stored.getFieldVL(sfSigningPubKey).empty());
    }

    // Two TransactionProposalSign transactions for the same proposal applied
    // in one ledger. The second Sign runs against the open view already
    // mutated by the first, so it sees the first's BatchSigners / Signers
    // entry and appends without conflict; both signatures accumulate.
    void
    testMultipleSignsSameLedger(FeatureBitset features)
    {
        testcase("multiple signs for one proposal in a single ledger");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const target{"target"};
        Account const dest{"dest"};
        Account const ceo{"ceo"};
        Account const cfo{"cfo"};
        env.fund(XRP(10000), target, dest, ceo, cfo);
        env.close();

        // target's applicable SignerList has two members; both must sign to
        // meet quorum. Each contributes one share.
        env(signers(target, 2, {{ceo, 1}, {cfo, 1}}));
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        env(proposal::create(
            target,
            proposal::unsignedPayload(
                env,
                pay(target, dest, XRP(1)),
                ticketSeq,
                /*extraSigners=*/2),
            proposal::expiration(env, 100s)));
        env.close();

        // Submit both Signs without an intervening env.close(). They apply
        // sequentially against the open view: Sign #2 sees the mutation from
        // Sign #1 (a Signers array with ceo's entry) and appends cfo's entry
        // to it, rather than tripping the single-vs-multi mode conflict.
        env(proposal::sign(env, ceo, target, ticketSeq, target, ceo));
        env(proposal::sign(env, cfo, target, ticketSeq, target, cfo));
        env.close();

        auto const sle = proposal::entry(env, target, ticketSeq);
        if (!BEAST_EXPECT(sle))
            return;
        auto const stored = sle->getFieldObject(sfProposedTransaction);
        if (!BEAST_EXPECT(stored.isFieldPresent(sfSigners)))
            return;
        auto const& outerSigners = stored.getFieldArray(sfSigners);
        BEAST_EXPECT(outerSigners.size() == 2);
        // Kept sorted ascending by Account — the invariant Batch and the
        // ordinary multi-sign validator both rely on.
        BEAST_EXPECT(
            outerSigners[0].getAccountID(sfAccount) < outerSigners[1].getAccountID(sfAccount));
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
        // otherwise be rejected with tecNO_PERMISSION still triggers cleanup
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
            env(jv, Ter(tecNO_PERMISSION));
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
    testKeyBindings(FeatureBitset features)
    {
        testcase("signer key bindings: master, regular key, mismatch");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const target{"target"};
        Account const dest{"dest"};
        Account const ceo{"ceo"};
        Account const ceoRegKey{"ceoRegKey"};  // ceo's regular key
        Account const other{"other"};          // key not tied to ceo
        Account const relay{"relay"};          // relays each Sign so ceo's
                                               // outer key status is irrelevant
        env.fund(XRP(10000), target, dest, ceo, ceoRegKey, other, relay);
        env.close();

        env(signers(target, 1, {{ceo, 1}}));
        env.close();

        // Wrong presenting key: ProposalSignature.Account=ceo but
        // SigningPubKey/TxnSignature come from `other`. The signature verifies
        // cryptographically (verify() only checks the key/signature pair), so
        // preclaim advances into checkSignerKey where the presenting key
        // resolves to `other` — neither ceo's master nor its (still-unset)
        // regular key — and the contribution is refused.
        {
            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            env(proposal::create(
                target,
                proposal::unsignedPayload(env, pay(target, dest, XRP(1)), ticketSeq),
                proposal::expiration(env, 100s)));
            env.close();

            env(signAs(env, relay, target, ticketSeq, target, ceo, other), Ter(tecNO_PERMISSION));
            env.close();
            BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
        }

        // Give ceo a regular key, then disable master.
        env(regkey(ceo, ceoRegKey));
        env(fset(ceo, asfDisableMaster), Sig(ceo));
        env.close();

        // Disabled master: ceo's master key still derives to ceo's own
        // account id (fromKey == signerAccount), but asfDisableMaster on the
        // account root refuses it.
        {
            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            env(proposal::create(
                target,
                proposal::unsignedPayload(env, pay(target, dest, XRP(1)), ticketSeq),
                proposal::expiration(env, 100s)));
            env.close();

            env(signAs(env, relay, target, ticketSeq, target, ceo, ceo), Ter(tecNO_PERMISSION));
            env.close();
            BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
        }

        // Regular key: same ceo contribution, but presented with ceoRegKey.
        // checkSignerKey sees fromKey != signerAccount, walks to the regular-
        // key branch, and matches the ceoRegKey stored on ceo's account root.
        {
            std::uint32_t const ticketSeq = proposal::createTicket(env, target);
            env(proposal::create(
                target,
                proposal::unsignedPayload(env, pay(target, dest, XRP(1)), ticketSeq),
                proposal::expiration(env, 100s)));
            env.close();

            env(signAs(env, relay, target, ticketSeq, target, ceo, ceoRegKey));
            env.close();

            auto const sle = proposal::entry(env, target, ticketSeq);
            if (BEAST_EXPECT(sle))
            {
                auto const stored = sle->getFieldObject(sfProposedTransaction);
                BEAST_EXPECT(stored.isFieldPresent(sfSigners));
                BEAST_EXPECT(stored.getFieldArray(sfSigners).size() == 1);
                BEAST_EXPECT(
                    stored.getFieldArray(sfSigners)[0].getAccountID(sfAccount) == ceo.id());
            }
        }
    }

    void
    testMultiSignNoSignerList(FeatureBitset features)
    {
        testcase("multi-sign against a SigningFor that has no SignerList");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const target{"target"};
        Account const dest{"dest"};
        Account const stranger{"stranger"};
        env.fund(XRP(10000), target, dest, stranger);
        env.close();
        // Note: target has *no* SignerListSet.

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        env(proposal::create(
            target,
            proposal::unsignedPayload(env, pay(target, dest, XRP(1)), ticketSeq),
            proposal::expiration(env, 100s)));
        env.close();

        // signer != SigningFor puts checkAuthorized on the multi-sign path,
        // where it looks up the SignerList on target and finds nothing.
        env(proposal::sign(env, stranger, target, ticketSeq, target, stranger),
            Ter(tecNO_PERMISSION));
        env.close();
        BEAST_EXPECT(proposal::entry(env, target, ticketSeq));
    }

    void
    testPhantomMultiSigner(FeatureBitset features)
    {
        testcase("phantom multi-signer: SignerList entry with no account root");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const target{"target"};
        Account const dest{"dest"};
        Account const submitter{"submitter"};  // pays fee to relay the Sign
        Account const phantom{"phantom"};      // never funded — pure key pair
        env.fund(XRP(10000), target, dest, submitter);
        env.close();

        // Phantom multi-signer: a SignerList may list an accountID that has
        // no on-ledger account root. Its master key still authorizes for it
        // (checkSignerKey's permitPhantom branch).
        env(signers(target, 1, {{phantom, 1}}));
        env.close();
        BEAST_EXPECT(!env.le(keylet::account(phantom.id())));

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        env(proposal::create(
            target,
            proposal::unsignedPayload(env, pay(target, dest, XRP(1)), ticketSeq),
            proposal::expiration(env, 100s)));
        env.close();

        env(proposal::sign(env, submitter, target, ticketSeq, target, phantom));
        env.close();

        auto const sle = proposal::entry(env, target, ticketSeq);
        if (BEAST_EXPECT(sle))
        {
            auto const stored = sle->getFieldObject(sfProposedTransaction);
            BEAST_EXPECT(stored.isFieldPresent(sfSigners));
            BEAST_EXPECT(stored.getFieldArray(sfSigners).size() == 1);
            BEAST_EXPECT(
                stored.getFieldArray(sfSigners)[0].getAccountID(sfAccount) == phantom.id());
        }
    }

    void
    testSingleSignModeConflicts(FeatureBitset features)
    {
        testcase("single-sign duplicate and single-then-multi mode conflict");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const target{"target"};
        Account const dest{"dest"};
        Account const ceo{"ceo"};
        env.fund(XRP(10000), target, dest, ceo);
        env.close();

        // target keeps master, and also has a SignerList — either mode could
        // in principle sign the completed transaction.
        env(signers(target, 1, {{ceo, 1}}));
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, target);
        env(proposal::create(
            target,
            proposal::unsignedPayload(env, pay(target, dest, XRP(1)), ticketSeq),
            proposal::expiration(env, 100s)));
        env.close();

        // target single-signs its own proposal.
        env(proposal::sign(env, target, target, ticketSeq, target, target));
        env.close();

        // Duplicate single-sign: outer slot already carries a single-sign,
        // recordIntoSigners's singleSign path hits hasSingle -> tecDUPLICATE.
        env(proposal::sign(env, target, target, ticketSeq, target, target), Ter(tecDUPLICATE));
        env.close();

        // Mode conflict: with a single-sign already recorded, a multi-sign
        // contribution from a SignerList member is refused — recordIntoSigners
        // hits hasSingle on the multi-sign path -> tecNO_PERMISSION.
        env(proposal::sign(env, ceo, target, ticketSeq, target, ceo), Ter(tecNO_PERMISSION));
        env.close();
    }

    // A batch proposal whose inner Payment carries an unsigned reserve
    // sponsorship (sfSponsor + spfSponsorReserve, no sfSponsorSignature).
    // The inner sponsor is not a required batch participant: Batch's own
    // requiredSigners assembly gates the sponsor on
    // rb.isFieldPresent(sfSponsorSignature), which is never true for a
    // proposal-form inner, so cosign must not collect a signature for the
    // sponsor role either. In v1 an inner reserve sponsorship is pre-funded
    // (a sponsorship SLE established via SponsorshipTransfer ahead of time)
    // and the completed batch relies on that SLE at submit; co-signed inner
    // sponsorship is out of scope for v1 cosign.
    void
    testBatchInnerSponsorNotRequired(FeatureBitset features)
    {
        testcase("batch inner sponsor is not a required batch participant");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const outer{"outer"};
        Account const dest{"dest"};
        Account const backer{"backer"};  // reserve-only sponsor of an inner
        env.fund(XRP(10000), outer, dest, backer);
        env.close();

        // Inner Payment sponsored (reserve-only) by backer. Proposal form
        // carries no sfSponsorSignature — the sponsor's authorization would
        // come from a pre-existing sponsorship SLE at submit time, not from
        // cosigner collection.
        json::Value innerSponsoredPay = pay(outer, dest, XRP(1));
        innerSponsoredPay[sfSponsor.jsonName] = backer.human();
        innerSponsoredPay[sfSponsorFlags.jsonName] = static_cast<std::uint32_t>(spfSponsorReserve);

        std::uint32_t const ticketSeq = proposal::createTicket(env, outer);
        json::Value const proposedTx = proposal::unsignedBatch(
            env,
            outer,
            ticketSeq,
            tfAllOrNothing,
            {proposal::innerTx(innerSponsoredPay, env.seq(outer)),
             proposal::innerTx(pay(outer, dest, XRP(1)), env.seq(outer) + 1)},
            /*numSigners=*/0);

        env(proposal::create(outer, proposedTx, proposal::expiration(env, 100s)));
        env.close();

        // The sponsor plays no outer role (the outer is a Batch and has no
        // sfSponsor field itself) and no inner-participant role recognized
        // by cosign (backer is neither an inner initiator nor an inner
        // counterparty). Preclaim rejects the contribution with
        // tecNO_PERMISSION, matching Batch's own submit-time
        // requiredSigners gate.
        env(proposal::sign(env, outer, outer, ticketSeq, backer, backer), Ter(tecNO_PERMISSION));
        env.close();

        // Nothing was recorded: no BatchSigners entry, no mutation of the
        // sponsored inner's sponsor slot.
        auto const sle = proposal::entry(env, outer, ticketSeq);
        if (!BEAST_EXPECT(sle))
            return;
        auto const stored = sle->getFieldObject(sfProposedTransaction);
        BEAST_EXPECT(!stored.isFieldPresent(sfBatchSigners));
        auto const& inners = stored.getFieldArray(sfRawTransactions);
        if (!BEAST_EXPECT(inners.size() == 2))
            return;
        // Inner txs may be stored either directly or wrapped under
        // sfRawTransaction (mirrors ProposalHelpers::innerTxn).
        auto const& innerWrap = inners[0];
        auto const inner0 = innerWrap.isFieldPresent(sfTransactionType)
            ? innerWrap
            : innerWrap.getFieldObject(sfRawTransaction);
        BEAST_EXPECT(
            inner0.isFieldPresent(sfSponsor) && inner0.getAccountID(sfSponsor) == backer.id());
        BEAST_EXPECT(!inner0.isFieldPresent(sfSponsorSignature));
    }

    void
    testBatchInnerMultiSignAccumulate(FeatureBitset features)
    {
        testcase("batch inner multi-sign accumulates onto existing batch signer");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const outer{"outer"};
        Account const bob{"bob"};
        Account const dest{"dest"};
        Account const outerKey{"outerKey"};
        Account const bobKey1{"bobKey1"};
        Account const bobKey2{"bobKey2"};
        env.fund(XRP(10000), outer, bob, dest, outerKey, bobKey1, bobKey2);
        env.close();

        env(signers(outer, 1, {{outerKey, 1}}));
        // bob's inner requires two multi-sign shares — the second one falls
        // on the existing BatchSigner entry rather than creating a new one,
        // exercising recordContribution's findBatchSigner-hit path.
        env(signers(bob, 2, {{bobKey1, 1}, {bobKey2, 1}}));
        env.close();

        std::uint32_t const ticketSeq = proposal::createTicket(env, outer);
        json::Value const proposedTx = proposal::unsignedBatch(
            env,
            outer,
            ticketSeq,
            tfAllOrNothing,
            {proposal::innerTx(pay(outer, dest, XRP(1)), env.seq(outer)),
             proposal::innerTx(pay(bob, dest, XRP(1)), env.seq(bob))},
            /*numSigners=*/3);

        env(proposal::create(outerKey, proposedTx, proposal::expiration(env, 100s)));
        env.close();

        env(proposal::sign(env, outerKey, outer, ticketSeq, outer, outerKey));
        env.close();

        // First bob contribution creates the BatchSigner entry.
        env(proposal::sign(env, outerKey, outer, ticketSeq, bob, bobKey1));
        env.close();

        // Duplicate: findBatchSigner returns the existing entry, but
        // recordIntoSigners then rejects the duplicate signer inside its
        // Signers array — the tecDUPLICATE bubbles up through the
        // findBatchSigner-hit branch's error return.
        env(proposal::sign(env, outerKey, outer, ticketSeq, bob, bobKey1), Ter(tecDUPLICATE));
        env.close();

        // Second distinct bob contribution finds the existing BatchSigner
        // entry and accumulates onto its Signers array
        // (recordContribution's findBatchSigner-hit branch).
        env(proposal::sign(env, outerKey, outer, ticketSeq, bob, bobKey2));
        env.close();

        auto const sle = proposal::entry(env, outer, ticketSeq);
        if (!BEAST_EXPECT(sle))
            return;
        auto const stored = sle->getFieldObject(sfProposedTransaction);
        auto const& batchSigners = stored.getFieldArray(sfBatchSigners);
        BEAST_EXPECT(batchSigners.size() == 1);
        BEAST_EXPECT(batchSigners[0].getAccountID(sfAccount) == bob.id());
        auto const& bobShares = batchSigners[0].getFieldArray(sfSigners);
        BEAST_EXPECT(bobShares.size() == 2);
        BEAST_EXPECT(bobShares[0].getAccountID(sfAccount) < bobShares[1].getAccountID(sfAccount));

        // Submit the completed Batch.
        env(proposedJson(env, outer, ticketSeq), Sig(kNone));
        env.close();

        BEAST_EXPECT(!proposal::entry(env, outer, ticketSeq));
        BEAST_EXPECT(env.balance(dest) == XRP(10000) + XRP(2));
    }

    void
    testBatchInnerFromUncreatedAccount(FeatureBitset features)
    {
        testcase("batch inner from an account an earlier inner creates");

        using namespace jtx;
        using namespace std::chrono_literals;

        Env env{*this, features};

        Account const outer{"outer"};
        Account const dest{"dest"};
        Account const fresh{"fresh"};  // never funded: the batch creates it
        env.fund(XRP(10000), outer, dest);
        env.close();
        BEAST_EXPECT(!env.le(keylet::account(fresh.id())));

        std::uint32_t const ticketSeq = proposal::createTicket(env, outer);
        json::Value const proposedTx = proposal::unsignedBatch(
            env,
            outer,
            ticketSeq,
            tfAllOrNothing,
            {proposal::innerTx(pay(outer, fresh, XRP(1000)), env.seq(outer)),
             proposal::innerTx(pay(fresh, dest, XRP(1)), env.current()->seq())},
            /*numSigners=*/1);

        env(proposal::create(outer, proposedTx, proposal::expiration(env, 100s)));
        env.close();

        // Batch::checkBatchSign authorizes an inner from a not-yet-created
        // account with that account's own master key
        // (permitUncreatedAccount=true), so this is a contribution the
        // completed Batch will accept and it must be recordable here.
        env(proposal::sign(env, outer, outer, ticketSeq, fresh, fresh));
        env.close();

        auto const sle = proposal::entry(env, outer, ticketSeq);
        if (!BEAST_EXPECT(sle))
            return;
        auto const stored = sle->getFieldObject(sfProposedTransaction);
        BEAST_EXPECT(stored.isFieldPresent(sfBatchSigners));
        auto const& batchSigners = stored.getFieldArray(sfBatchSigners);
        BEAST_EXPECT(batchSigners.size() == 1);
        BEAST_EXPECT(batchSigners[0].getAccountID(sfAccount) == fresh.id());
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
        testDelegatedPayloadRejectsAccountSigningFor(all);
        testOuterCounterpartyExplicit(all);
        testOuterCounterpartyImplicit(all);
        testOuterSponsor(all);
        testMultiRoleAmbiguity(all);
        testInitiatorAuxAmbiguity(all);
        testRolePrefixEnforced(all);
        testAuxSlotDuplicateAndModeConflict(all);
        testSignBeingSponsored(all);
        testMultipleSignsSameLedger(all);
        testExpired(all);
        testExpiredWithBadSignature(all);
        testWrongSign(all);
        testDuplicateAndModeConflict(all);
        testBatch(all);
        testKeyBindings(all);
        testMultiSignNoSignerList(all);
        testPhantomMultiSigner(all);
        testSingleSignModeConflicts(all);
        testBatchInnerSponsorNotRequired(all);
        testBatchInnerMultiSignAccumulate(all);
        testBatchInnerFromUncreatedAccount(all);
    }
};

BEAST_DEFINE_TESTSUITE(TransactionProposalSign, app, xrpl);

}  // namespace xrpl::test
