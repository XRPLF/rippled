#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/applySteps.h>

#include <array>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <source_location>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace xrpl::test {

class InvariantsMPT_test : public InvariantsBase
{
    FeatureBitset const all_{test::jtx::testableAmendments()};

    void
    testMPT()
    {
        using namespace test::jtx;
        testcase << "MPT";

        MPTIssue const nonCanonicalMPTIssue{makeMptID(1, AccountID(0x4985601))};
        auto const nonCanonicalMPTAmount = [&](SField const& field) {
            return STAmount{
                field,
                nonCanonicalMPTIssue,
                kMaxMpTokenAmount + std::uint64_t{1},
                0,
                false,
                STAmount::Unchecked{}};
        };
        auto const negativeMPTAmount = [&](SField const& field) {
            return STAmount{field, nonCanonicalMPTIssue, 2, 0, true, STAmount::Unchecked{}};
        };
        auto const nonCanonicalMPTPayment = [&]() {
            return STTx{ttPAYMENT, [&](STObject& tx) {
                            tx.setFieldAmount(sfAmount, nonCanonicalMPTAmount(sfAmount));
                        }};
        };

        doInvariantCheck(
            makeEnv(all_ - fixCleanup3_2_0),
            {},
            [](Account const&, Account const&, ApplyContext&) { return true; },
            XRPAmount{},
            nonCanonicalMPTPayment(),
            {tesSUCCESS, tesSUCCESS});

        doInvariantCheck(
            {{"ledger entry contains non-canonical MPT or XRP amount"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;

                auto sleNew = std::make_shared<SLE>(
                    keylet::check(a1.id(), SeqProxy::rawSequence((*sle)[sfSequence])));
                sleNew->setAccountID(sfAccount, a1.id());
                sleNew->setAccountID(sfDestination, a2.id());
                sleNew->setFieldAmount(sfSendMax, nonCanonicalMPTAmount(sfSendMax));
                ac.view().insert(sleNew);
                return true;
            });

        doInvariantCheck(
            {{"ledger entry contains non-canonical MPT or XRP amount"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;

                auto sleNew = std::make_shared<SLE>(
                    keylet::check(a1.id(), SeqProxy::rawSequence((*sle)[sfSequence])));
                sleNew->setAccountID(sfAccount, a1.id());
                sleNew->setAccountID(sfDestination, a2.id());
                sleNew->setFieldAmount(sfSendMax, negativeMPTAmount(sfSendMax));
                ac.view().insert(sleNew);
                return true;
            });

        // MPT OutstandingAmount > MaximumAmount
        doInvariantCheck(
            {{"OutstandingAmount overflow"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                // mptissuance outstanding is negative
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;

                MPTIssue const mpt{makeMptID(sle->getFieldU32(sfSequence), a1)};
                auto sleNew = std::make_shared<SLE>(keylet::mptokenIssuance(mpt.getMptID()));
                sleNew->setFieldU64(sfOutstandingAmount, 110);
                sleNew->setFieldU64(sfMaximumAmount, 100);
                ac.view().insert(sleNew);
                return true;
            });

        // MPTToken amount doesn't add up to OutstandingAmount
        doInvariantCheck(
            {{"invalid OutstandingAmount balance"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                // mptissuance outstanding is negative
                auto const sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;

                MPTIssue const mpt{makeMptID(sle->getFieldU32(sfSequence), a1)};
                auto sleNew = std::make_shared<SLE>(keylet::mptokenIssuance(mpt.getMptID()));
                sleNew->setFieldU64(sfOutstandingAmount, 100);
                sleNew->setFieldU64(sfMaximumAmount, 100);
                ac.view().insert(sleNew);

                sleNew = std::make_shared<SLE>(keylet::mptoken(mpt.getMptID(), a2));
                sleNew->setFieldU64(sfMPTAmount, 90);
                ac.view().insert(sleNew);

                return true;
            });

        // Overflow/Invalid balance on payment
        auto testPayment = [&](std::string const& log, auto&& update) {
            MPTID id;
            doInvariantCheck(
                {{log}},
                [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                    return update(id, ac, a1);
                },
                XRPAmount{},
                STTx{ttPAYMENT, [](STObject& tx) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                [&](Account const& a1, Account const& a2, Env& env) {
                    Account const gw("gw");
                    env.fund(XRP(1'000), gw);
                    MPTTester const mpt(
                        {.env = env, .issuer = gw, .holders = {a1}, .pay = 100, .maxAmt = 100});
                    id = mpt.issuanceID();
                    return true;
                });
        };
        testPayment(
            "invalid OutstandingAmount balance",
            [&](MPTID const& id, ApplyContext& ac, Account const& a1) {
                auto sle = ac.view().peek(keylet::mptoken(id, a1));
                if (!sle)
                    return false;
                sle->setFieldU64(sfMPTAmount, 101);
                ac.view().update(sle);
                return true;
            });
        testPayment(
            "OutstandingAmount overflow", [&](MPTID const& id, ApplyContext& ac, Account const&) {
                auto sle = ac.view().peek(keylet::mptokenIssuance(id));
                if (!sle)
                    return false;
                sle->setFieldU64(sfOutstandingAmount, 101);
                ac.view().update(sle);
                return true;
            });

        // The on-failure MPT checks (OutstandingAmount balance / transfer) apply
        // to every non-tesSUCCESS result, with no per-result exemption: on a tec
        // the transactor discards the view and re-applies only offer, trust
        // line, NFT offer and credential deletions, so an MPT change reaching
        // the invariant is a bug whatever the code. Seeded via initialResult.
        {
            MPTID id;
            // preclose: gw issues an MPT held by A1 and A2.
            auto const setup = [&](Account const& a1, Account const& a2, Env& env) {
                Account const gw("gw");
                env.fund(XRP(1'000), gw);
                MPTTester const mpt(
                    {.env = env, .issuer = gw, .holders = {a1, a2}, .pay = 50, .maxAmt = 1'000});
                id = mpt.issuanceID();
                return true;
            };

            // Consistent mint: OutstandingAmount and A1's balance both grow by
            // 10, so conservation holds and only the on-failure check fires.
            Precheck const mint = [&](Account const& a1, Account const&, ApplyContext& ac) {
                auto sleIss = ac.view().peek(keylet::mptokenIssuance(id));
                auto sleTok = ac.view().peek(keylet::mptoken(id, a1.id()));
                if (!sleIss || !sleTok)
                    return false;
                (*sleIss)[sfOutstandingAmount] = (*sleIss)[sfOutstandingAmount] + 10;
                (*sleTok)[sfMPTAmount] = (*sleTok)[sfMPTAmount] + 10;
                ac.view().update(sleIss);
                ac.view().update(sleTok);
                return true;
            };

            // Holder-to-holder transfer (A1 -> A2 by 10). OutstandingAmount is
            // unchanged, and CanTransfer keeps the ordinary transfer check
            // quiet, so only the on-failure check fires.
            Precheck const transfer = [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto sleIss = ac.view().peek(keylet::mptokenIssuance(id));
                auto sleA = ac.view().peek(keylet::mptoken(id, a1.id()));
                auto sleB = ac.view().peek(keylet::mptoken(id, a2.id()));
                if (!sleIss || !sleA || !sleB)
                    return false;
                (*sleIss)[sfFlags] = (*sleIss)[sfFlags] | lsfMPTCanTransfer;
                (*sleA)[sfMPTAmount] = (*sleA)[sfMPTAmount] - 10;
                (*sleB)[sfMPTAmount] = (*sleB)[sfMPTAmount] + 10;
                ac.view().update(sleIss);
                ac.view().update(sleA);
                ac.view().update(sleB);
                return true;
            };

            STTx const payment{ttPAYMENT, [](STObject&) {}};

            // Negative controls: nothing fires on tesSUCCESS. Without these, the
            // cases below would still pass if the result guard were dropped.
            doInvariantCheck({}, mint, XRPAmount{}, payment, {tesSUCCESS, tesSUCCESS}, setup);
            doInvariantCheck({}, transfer, XRPAmount{}, payment, {tesSUCCESS, tesSUCCESS}, setup);

            // tecKILLED and tecINCOMPLETE are not special: an MPT change paired
            // with either fires, as with any other failure.
            doInvariantCheck(
                {{"OutstandingAmount balance changed on failure"}},
                mint,
                XRPAmount{},
                payment,
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                setup,
                TxAccount::None,
                std::source_location::current(),
                tecKILLED);
            doInvariantCheck(
                {{"OutstandingAmount balance changed on failure"}},
                mint,
                XRPAmount{},
                payment,
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                setup,
                TxAccount::None,
                std::source_location::current(),
                tecINCOMPLETE);
            doInvariantCheck(
                {{"MPToken balance changed on failure"}},
                transfer,
                XRPAmount{},
                payment,
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                setup,
                TxAccount::None,
                std::source_location::current(),
                tecKILLED);
            doInvariantCheck(
                {{"MPToken balance changed on failure"}},
                transfer,
                XRPAmount{},
                payment,
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                setup,
                TxAccount::None,
                std::source_location::current(),
                tecINCOMPLETE);
            // The same change under a third failure result: the check keys off
            // "not tesSUCCESS", nothing finer.
            doInvariantCheck(
                {{"OutstandingAmount balance changed on failure"}},
                mint,
                XRPAmount{},
                payment,
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                setup,
                TxAccount::None,
                std::source_location::current(),
                tecEXPIRED);
            doInvariantCheck(
                {{"MPToken balance changed on failure"}},
                transfer,
                XRPAmount{},
                payment,
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                setup,
                TxAccount::None,
                std::source_location::current(),
                tecEXPIRED);

            // A lock moves value within one holder, so it is not a two-sided
            // transfer and the `senders || receivers` form is what catches it.
            // OutstandingAmount and the holder total are unchanged, so the
            // balance check stays quiet.
            Precheck const lock = [&](Account const& a1, Account const&, ApplyContext& ac) {
                auto sleTok = ac.view().peek(keylet::mptoken(id, a1.id()));
                if (!sleTok || (*sleTok)[sfMPTAmount] < 10)
                    return false;
                // A fresh MPToken has no locked amount, so set it directly.
                (*sleTok)[sfMPTAmount] = (*sleTok)[sfMPTAmount] - 10;
                sleTok->setFieldU64(sfLockedAmount, 10);
                ac.view().update(sleTok);
                return true;
            };
            // Negative control: a lock is legitimate on tesSUCCESS.
            doInvariantCheck({}, lock, XRPAmount{}, payment, {tesSUCCESS, tesSUCCESS}, setup);
            doInvariantCheck(
                {{"MPToken balance changed on failure"}},
                lock,
                XRPAmount{},
                payment,
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                setup,
                TxAccount::None,
                std::source_location::current(),
                tecKILLED);
            // The lock is caught under any failure result.
            doInvariantCheck(
                {{"MPToken balance changed on failure"}},
                lock,
                XRPAmount{},
                payment,
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                setup,
                TxAccount::None,
                std::source_location::current(),
                tecEXPIRED);

            // A deleted MPToken has no amtAfter, so the sender/receiver counts
            // skip it and only the deletedAuthorized_ term can catch it. That
            // needs holders authorized but never paid, so the MPToken can be
            // erased with a zero balance and OutstandingAmount untouched --
            // otherwise the holder would register as a sender instead.
            MPTID emptyId;
            auto const setupEmpty = [&](Account const& a1, Account const& a2, Env& env) {
                Account const gw("gw");
                env.fund(XRP(1'000), gw);
                MPTTester const mpt({.env = env, .issuer = gw, .holders = {a1, a2}, .maxAmt = 100});
                emptyId = mpt.issuanceID();
                return true;
            };
            Precheck const eraseToken = [&](Account const& a1, Account const&, ApplyContext& ac) {
                auto sleTok = ac.view().peek(keylet::mptoken(emptyId, a1.id()));
                if (!sleTok || (*sleTok)[sfMPTAmount] != 0)
                    return false;
                ac.view().erase(sleTok);
                return true;
            };
            // ValidMPTIssuance also reports the deletion, so assert on
            // ValidMPTTransfer's message, which only the new check can produce.
            doInvariantCheck(
                {{"MPToken deleted on failure"}},
                eraseToken,
                XRPAmount{},
                payment,
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                setupEmpty,
                TxAccount::None,
                std::source_location::current(),
                tecEXPIRED);
        }

        // Invalid IOU clawback delta must fail once MPTokensV2 enforces before/after validation.
        {
            Env env(*this, all_);
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            Account const other{"other"};
            env.fund(XRP(1'000), issuer, holder, other);
            auto const usd = issuer["USD"];
            env.trust(usd(100), holder);
            env(pay(issuer, holder, usd(100)));
            env.close();

            doInvariantCheck(
                std::move(env),
                holder,
                other,
                {{"Invariant failed: trustline clawback balance change is invalid"}},
                [issuer, usd](Account const& holder, Account const&, ApplyContext& ac) {
                    auto sle =
                        ac.view().peek(keylet::trustLine(holder.id(), issuer.id(), usd.currency));
                    if (!sle)
                        return false;

                    STAmount balance{Issue{usd.currency, issuer.id()}, 80};
                    if (holder.id() > issuer.id())
                        balance.negate();
                    sle->setFieldAmount(sfBalance, balance);
                    ac.view().update(sle);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttCLAWBACK,
                    [&](STObject& tx) {
                        tx[sfAccount] = issuer.id();
                        tx[sfAmount] = STAmount{Issue{usd.currency, holder.id()}, 10};
                    }},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
        }

        // Full IOU clawback may delete the trustline; missing after-SLE represents zero balance.
        {
            Env env(*this, all_);
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            Account const other{"other"};
            env.fund(XRP(1'000), issuer, holder, other);
            auto const usd = issuer["USD"];
            env.trust(usd(100), holder);
            env(pay(issuer, holder, usd(100)));
            env.close();

            doInvariantCheck(
                std::move(env),
                holder,
                other,
                {},
                [issuer, usd](Account const& holder, Account const&, ApplyContext& ac) {
                    auto const sle =
                        ac.view().peek(keylet::trustLine(holder.id(), issuer.id(), usd.currency));
                    if (!sle)
                        return false;

                    ac.view().erase(sle);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttCLAWBACK,
                    [&](STObject& tx) {
                        tx[sfAccount] = issuer.id();
                        tx[sfAmount] = STAmount{Issue{usd.currency, holder.id()}, 100};
                    }},
                {tesSUCCESS, tesSUCCESS});
        }

        // Pre-MPTokensV2 invalid IOU clawback delta logs but remains non-enforcing.
        {
            Env env(*this, all_ - featureMPTokensV2);
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            Account const other{"other"};
            env.fund(XRP(1'000), issuer, holder, other);
            auto const usd = issuer["USD"];
            env.trust(usd(100), holder);
            env(pay(issuer, holder, usd(100)));
            env.close();

            doInvariantCheck(
                std::move(env),
                holder,
                other,
                {{"Invariant failed: trustline clawback balance change is invalid"}},
                [issuer, usd](Account const& holder, Account const&, ApplyContext& ac) {
                    auto sle =
                        ac.view().peek(keylet::trustLine(holder.id(), issuer.id(), usd.currency));
                    if (!sle)
                        return false;

                    STAmount balance{Issue{usd.currency, issuer.id()}, 80};
                    if (holder.id() > issuer.id())
                        balance.negate();
                    sle->setFieldAmount(sfBalance, balance);
                    ac.view().update(sle);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttCLAWBACK,
                    [&](STObject& tx) {
                        tx[sfAccount] = issuer.id();
                        tx[sfAmount] = STAmount{Issue{usd.currency, holder.id()}, 10};
                    }},
                {tesSUCCESS, tesSUCCESS});
        }

        // Invalid MPT clawback delta must fail when raw MPToken debit mismatches sfAmount.
        {
            Env env(*this, all_);
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            Account const other{"other"};
            env.fund(XRP(1'000), issuer, holder, other);
            MPTTester const mpt(
                {.env = env, .issuer = issuer, .holders = {holder}, .pay = 100, .maxAmt = 100});
            auto const id = mpt.issuanceID();

            doInvariantCheck(
                std::move(env),
                holder,
                other,
                {{"Invariant failed: MPT clawback balance change is invalid"}},
                [id](Account const& holder, Account const&, ApplyContext& ac) {
                    auto const sleToken = ac.view().peek(keylet::mptoken(id, holder));
                    auto const sleIssuance = ac.view().peek(keylet::mptokenIssuance(id));
                    if (!sleToken || !sleIssuance)
                        return false;

                    sleToken->setFieldU64(sfMPTAmount, 80);
                    sleIssuance->setFieldU64(sfOutstandingAmount, 80);
                    ac.view().update(sleToken);
                    ac.view().update(sleIssuance);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttCLAWBACK,
                    [&](STObject& tx) {
                        tx[sfAccount] = issuer.id();
                        tx[sfHolder] = holder.id();
                        tx[sfAmount] = STAmount{MPTIssue{id}, 10};
                    }},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
        }

        // A clawback that mutates both IOU and MPT entries must fail under MPTokensV2.
        {
            Env env(*this, all_);
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            Account const other{"other"};
            env.fund(XRP(1'000), issuer, holder, other);
            auto const usd = issuer["USD"];
            env.trust(usd(100), holder);
            env(pay(issuer, holder, usd(100)));
            MPTTester const mpt(
                {.env = env, .issuer = issuer, .holders = {holder}, .pay = 100, .maxAmt = 100});
            auto const id = mpt.issuanceID();

            doInvariantCheck(
                std::move(env),
                holder,
                other,
                {{"Invariant failed: trustline and MPToken both changed"}},
                [issuer, usd, id](Account const& holder, Account const&, ApplyContext& ac) {
                    auto const sleLine =
                        ac.view().peek(keylet::trustLine(holder.id(), issuer.id(), usd.currency));
                    auto const sleToken = ac.view().peek(keylet::mptoken(id, holder.id()));
                    auto const sleIssuance = ac.view().peek(keylet::mptokenIssuance(id));
                    if (!sleLine || !sleToken || !sleIssuance)
                        return false;

                    STAmount balance{Issue{usd.currency, issuer.id()}, 90};
                    if (holder.id() > issuer.id())
                        balance.negate();
                    sleLine->setFieldAmount(sfBalance, balance);
                    sleToken->setFieldU64(sfMPTAmount, 90);
                    sleIssuance->setFieldU64(sfOutstandingAmount, 90);
                    ac.view().update(sleLine);
                    ac.view().update(sleToken);
                    ac.view().update(sleIssuance);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttCLAWBACK,
                    [&](STObject& tx) {
                        tx[sfAccount] = issuer.id();
                        tx[sfHolder] = holder.id();
                        tx[sfAmount] = STAmount{MPTIssue{id}, 10};
                    }},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
        }

        // Clawback that modifies a trustline other than the one implied by the
        // tx amount: clawbackTrustLineBalanceInHolderTerms returns nullopt for
        // the mismatched line.
        {
            Env env(*this, all_);
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            Account const other{"other"};
            env.fund(XRP(1'000), issuer, holder, other);
            auto const usd = issuer["USD"];
            auto const eur = issuer["EUR"];
            env.trust(eur(100), holder);
            env(pay(issuer, holder, eur(100)));
            env.close();

            doInvariantCheck(
                std::move(env),
                holder,
                other,
                {{"Invariant failed: trustline clawback changed the wrong line"}},
                [issuer, eur](Account const& holder, Account const&, ApplyContext& ac) {
                    auto sle =
                        ac.view().peek(keylet::trustLine(holder.id(), issuer.id(), eur.currency));
                    if (!sle)
                        return false;
                    STAmount balance{Issue{eur.currency, issuer.id()}, 90};
                    if (holder.id() > issuer.id())
                        balance.negate();
                    sle->setFieldAmount(sfBalance, balance);
                    ac.view().update(sle);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttCLAWBACK,
                    [&](STObject& tx) {
                        tx[sfAccount] = issuer.id();
                        tx[sfAmount] = STAmount{Issue{usd.currency, holder.id()}, 10};
                    }},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
        }

        // Clawback leaving the holder's balance negative.
        {
            Env env(*this, all_);
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            Account const other{"other"};
            env.fund(XRP(1'000), issuer, holder, other);
            auto const usd = issuer["USD"];
            env.trust(usd(100), holder);
            env(pay(issuer, holder, usd(100)));
            env.close();

            doInvariantCheck(
                std::move(env),
                holder,
                other,
                {{"Invariant failed: trustline or MPT balance is negative"}},
                [issuer, usd](Account const& holder, Account const&, ApplyContext& ac) {
                    auto sle =
                        ac.view().peek(keylet::trustLine(holder.id(), issuer.id(), usd.currency));
                    if (!sle)
                        return false;
                    // Make the holder's balance negative from their perspective.
                    STAmount balance{Issue{usd.currency, issuer.id()}, 80};
                    if (holder.id() < issuer.id())
                        balance.negate();
                    sle->setFieldAmount(sfBalance, balance);
                    ac.view().update(sle);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttCLAWBACK,
                    [&](STObject& tx) {
                        tx[sfAccount] = issuer.id();
                        tx[sfAmount] = STAmount{Issue{usd.currency, holder.id()}, 10};
                    }},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
        }

        // IOU-amount clawback while only an MPToken changed: no trustline was
        // recorded, so iou_.before is empty.
        {
            Env env(*this, all_);
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            Account const other{"other"};
            env.fund(XRP(1'000), issuer, holder, other);
            auto const usd = issuer["USD"];
            MPTTester const mpt(
                {.env = env, .issuer = issuer, .holders = {holder}, .pay = 100, .maxAmt = 100});
            auto const id = mpt.issuanceID();

            doInvariantCheck(
                std::move(env),
                holder,
                other,
                {{"Invariant failed: trustline clawback changed the wrong line"}},
                [id](Account const& holder, Account const&, ApplyContext& ac) {
                    auto const sleToken = ac.view().peek(keylet::mptoken(id, holder));
                    auto const sleIssuance = ac.view().peek(keylet::mptokenIssuance(id));
                    if (!sleToken || !sleIssuance)
                        return false;
                    sleToken->setFieldU64(sfMPTAmount, 90);
                    sleIssuance->setFieldU64(sfOutstandingAmount, 90);
                    ac.view().update(sleToken);
                    ac.view().update(sleIssuance);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttCLAWBACK,
                    [&](STObject& tx) {
                        tx[sfAccount] = issuer.id();
                        tx[sfAmount] = STAmount{Issue{usd.currency, holder.id()}, 10};
                    }},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
        }

        // Valid trustline change but a zero clawback amount.
        {
            Env env(*this, all_);
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            Account const other{"other"};
            env.fund(XRP(1'000), issuer, holder, other);
            auto const usd = issuer["USD"];
            env.trust(usd(100), holder);
            env(pay(issuer, holder, usd(100)));
            env.close();

            doInvariantCheck(
                std::move(env),
                holder,
                other,
                {{"Invariant failed: trustline clawback amount is invalid"}},
                [issuer, usd](Account const& holder, Account const&, ApplyContext& ac) {
                    auto sle =
                        ac.view().peek(keylet::trustLine(holder.id(), issuer.id(), usd.currency));
                    if (!sle)
                        return false;
                    STAmount balance{Issue{usd.currency, issuer.id()}, 90};
                    if (holder.id() > issuer.id())
                        balance.negate();
                    sle->setFieldAmount(sfBalance, balance);
                    ac.view().update(sle);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttCLAWBACK,
                    [&](STObject& tx) {
                        tx[sfAccount] = issuer.id();
                        tx[sfAmount] = STAmount{Issue{usd.currency, holder.id()}, 0};
                    }},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
        }

        // MPT clawback tx missing the Holder field.
        {
            Env env(*this, all_);
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            Account const other{"other"};
            env.fund(XRP(1'000), issuer, holder, other);
            MPTTester const mpt(
                {.env = env, .issuer = issuer, .holders = {holder}, .pay = 100, .maxAmt = 100});
            auto const id = mpt.issuanceID();

            doInvariantCheck(
                std::move(env),
                holder,
                other,
                {{"Invariant failed: MPT clawback missing holder"}},
                [id](Account const& holder, Account const&, ApplyContext& ac) {
                    auto const sleToken = ac.view().peek(keylet::mptoken(id, holder));
                    auto const sleIssuance = ac.view().peek(keylet::mptokenIssuance(id));
                    if (!sleToken || !sleIssuance)
                        return false;
                    sleToken->setFieldU64(sfMPTAmount, 90);
                    sleIssuance->setFieldU64(sfOutstandingAmount, 90);
                    ac.view().update(sleToken);
                    ac.view().update(sleIssuance);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttCLAWBACK,
                    [&](STObject& tx) {
                        tx[sfAccount] = issuer.id();
                        tx[sfAmount] = STAmount{MPTIssue{id}, 10};
                    }},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
        }

        // MPT clawback where the holder's MPToken was deleted (after is empty).
        {
            Env env(*this, all_);
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            Account const other{"other"};
            env.fund(XRP(1'000), issuer, holder, other);
            MPTTester const mpt(
                {.env = env, .issuer = issuer, .holders = {holder}, .pay = 100, .maxAmt = 100});
            auto const id = mpt.issuanceID();

            doInvariantCheck(
                std::move(env),
                holder,
                other,
                {{"Invariant failed: MPT clawback token is missing"}},
                [id](Account const& holder, Account const&, ApplyContext& ac) {
                    auto const sleToken = ac.view().peek(keylet::mptoken(id, holder));
                    auto const sleIssuance = ac.view().peek(keylet::mptokenIssuance(id));
                    if (!sleToken || !sleIssuance)
                        return false;
                    // Keep the issuance consistent after removing the token.
                    sleIssuance->setFieldU64(sfOutstandingAmount, 0);
                    ac.view().update(sleIssuance);
                    ac.view().erase(sleToken);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttCLAWBACK,
                    [&](STObject& tx) {
                        tx[sfAccount] = issuer.id();
                        tx[sfHolder] = holder.id();
                        tx[sfAmount] = STAmount{MPTIssue{id}, 10};
                    }},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
        }

        // MPT clawback that changed a different holder's MPToken.
        {
            Env env(*this, all_);
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            Account const other{"other"};
            env.fund(XRP(1'000), issuer, holder, other);
            MPTTester const mpt(
                {.env = env,
                 .issuer = issuer,
                 .holders = {holder, other},
                 .pay = 100,
                 .maxAmt = 200});
            auto const id = mpt.issuanceID();

            doInvariantCheck(
                std::move(env),
                holder,
                other,
                {{"Invariant failed: MPT clawback changed the wrong token"}},
                [id](Account const&, Account const& other, ApplyContext& ac) {
                    auto const sleToken = ac.view().peek(keylet::mptoken(id, other));
                    auto const sleIssuance = ac.view().peek(keylet::mptokenIssuance(id));
                    if (!sleToken || !sleIssuance)
                        return false;
                    sleToken->setFieldU64(sfMPTAmount, 90);
                    sleIssuance->setFieldU64(sfOutstandingAmount, 190);
                    ac.view().update(sleToken);
                    ac.view().update(sleIssuance);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttCLAWBACK,
                    [&](STObject& tx) {
                        tx[sfAccount] = issuer.id();
                        tx[sfHolder] = holder.id();
                        tx[sfAmount] = STAmount{MPTIssue{id}, 10};
                    }},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
        }

        // Valid MPToken change but a zero MPT clawback amount.
        {
            Env env(*this, all_);
            Account const issuer{"issuer"};
            Account const holder{"holder"};
            Account const other{"other"};
            env.fund(XRP(1'000), issuer, holder, other);
            MPTTester const mpt(
                {.env = env, .issuer = issuer, .holders = {holder}, .pay = 100, .maxAmt = 100});
            auto const id = mpt.issuanceID();

            doInvariantCheck(
                std::move(env),
                holder,
                other,
                {{"Invariant failed: MPT clawback amount is invalid"}},
                [id](Account const& holder, Account const&, ApplyContext& ac) {
                    auto const sleToken = ac.view().peek(keylet::mptoken(id, holder));
                    auto const sleIssuance = ac.view().peek(keylet::mptokenIssuance(id));
                    if (!sleToken || !sleIssuance)
                        return false;
                    sleToken->setFieldU64(sfMPTAmount, 90);
                    sleIssuance->setFieldU64(sfOutstandingAmount, 90);
                    ac.view().update(sleToken);
                    ac.view().update(sleIssuance);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttCLAWBACK,
                    [&](STObject& tx) {
                        tx[sfAccount] = issuer.id();
                        tx[sfHolder] = holder.id();
                        tx[sfAmount] = STAmount{MPTIssue{id}, 0};
                    }},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
        }

        // More MPTokens created than expected
        std::array<std::pair<xrpl::TxType, std::uint8_t>, 4> const tests = {
            std::make_pair(ttAMM_WITHDRAW, 2),
            std::make_pair(ttAMM_CLAWBACK, 2),
            std::make_pair(ttAMM_CREATE, 3),
            std::make_pair(ttCHECK_CASH, 2)};
        for (auto const& [tx, nTokens] : tests)
        {
            doInvariantCheck(
                {{std::string("MPToken created for the MPT issuer")}},
                [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                    auto const sle = ac.view().peek(keylet::account(a1.id()));
                    if (!sle)
                        return false;

                    auto seq = sle->getFieldU32(sfSequence);
                    for (int i = 0; i < nTokens; ++i)
                    {
                        MPTIssue const mpt{makeMptID(seq + i, a1)};
                        auto sleNew =
                            std::make_shared<SLE>(keylet::mptokenIssuance(mpt.getMptID()));
                        ac.view().insert(sleNew);

                        sleNew = std::make_shared<SLE>(keylet::mptoken(mpt.getMptID(), a2));
                        ac.view().insert(sleNew);
                    }

                    return true;
                },
                XRPAmount{},
                STTx{tx, [](STObject& tx) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
        }

        // More MPTokens deleted than expected
        for (auto const& tx : {ttAMM_WITHDRAW, ttAMM_CLAWBACK})
        {
            MPTID id;
            Account const a3("A3");
            doInvariantCheck(
                {{"MPT authorize  succeeded but created/deleted bad number of mptokens"}},
                [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                    for (auto const& a : {a1, a2, a3})
                    {
                        auto sle = ac.view().peek(keylet::mptoken(id, a));
                        if (!sle)
                            return false;
                        ac.view().erase(sle);
                    }
                    return true;
                },
                XRPAmount{},
                STTx{tx, [](STObject& tx) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                [&](Account const& a1, Account const& a2, Env& env) {
                    Account const gw("gw");
                    env.fund(XRP(1'000), gw, a3);
                    MPTTester const mpt({.env = env, .issuer = gw, .holders = {a1, a2, a3}});
                    id = mpt.issuanceID();
                    return true;
                });
        }

        // sfReferenceHolding can only be set on creation by VaultCreate. A
        // non-VaultCreate transaction that creates an MPTokenIssuance with
        // sfReferenceHolding present must trip the invariant.
        doInvariantCheck(
            {{"sfReferenceHolding set on a new MPTokenIssuance by a "
              "non-VaultCreate transaction"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                auto const sleAcct = ac.view().peek(keylet::account(a1.id()));
                if (!sleAcct)
                    return false;
                MPTIssue const mpt{makeMptID(sleAcct->getFieldU32(sfSequence), a1)};
                auto sleNew = std::make_shared<SLE>(keylet::mptokenIssuance(mpt.getMptID()));
                sleNew->setFieldH256(sfReferenceHolding, uint256{1});
                ac.view().insert(sleNew);
                return true;
            },
            XRPAmount{},
            STTx{ttACCOUNT_SET, [](STObject&) {}});

        // sfReferenceHolding is immutable: changing the field on an
        // existing MPTokenIssuance must trip the invariant. Set up a real
        // vault via preclose (so the share issuance carries
        // sfReferenceHolding), then mutate it in precheck to produce a
        // before/after pair.
        {
            uint256 vaultKey;
            doInvariantCheck(
                {{"sfReferenceHolding was modified on an existing "
                  "MPTokenIssuance"}},
                [&](Account const&, Account const&, ApplyContext& ac) {
                    auto const sleVault = ac.view().peek(keylet::vault(vaultKey));
                    if (!sleVault)
                        return false;
                    auto sleIssuance =
                        ac.view().peek(keylet::mptokenIssuance(sleVault->at(sfShareMPTID)));
                    if (!sleIssuance)
                        return false;
                    sleIssuance->setFieldH256(sfReferenceHolding, uint256{2});
                    ac.view().update(sleIssuance);
                    return true;
                },
                XRPAmount{},
                STTx{ttACCOUNT_SET, [](STObject&) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                [&](Account const& a1, Account const&, Env& env) {
                    Account const issuer{"issuer"};
                    env.fund(XRP(10'000), issuer);
                    env.close();
                    MPTTester mptt{env, issuer, kMptInitNoFund};
                    mptt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
                    PrettyAsset const asset = mptt.issuanceID();
                    mptt.authorize({.account = a1});
                    env.close();

                    Vault const vault{env};
                    auto [tx, keylet] = vault.create({.owner = a1, .asset = asset});
                    env(tx);
                    env.close();
                    vaultKey = keylet.key;
                    return true;
                });
        }

        // Issuance flags other than lsfMPTLocked are fixed at creation or
        // set-once via MPTokenIssuanceSet; clearing one must trip the
        // invariant. Create the MPT in preclose, then strip
        // lsfMPTCanTransfer in precheck. lsfMPTLocked stays exempt: the
        // regular lock/unlock tests exercise its clear path under the same
        // amendments.
        {
            MPTID id;
            doInvariantCheck(
                {{"immutable MPTokenIssuance flag cleared"}},
                [&](Account const&, Account const&, ApplyContext& ac) {
                    auto sleIssuance = ac.view().peek(keylet::mptokenIssuance(id));
                    if (!sleIssuance)
                        return false;
                    sleIssuance->setFieldU32(
                        sfFlags, sleIssuance->getFieldU32(sfFlags) & ~lsfMPTCanTransfer);
                    ac.view().update(sleIssuance);
                    return true;
                },
                XRPAmount{},
                STTx{ttACCOUNT_SET, [](STObject&) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                [&](Account const&, Account const&, Env& env) {
                    Account const issuer{"issuer"};
                    env.fund(XRP(10'000), issuer);
                    env.close();
                    MPTTester mptt{env, issuer, kMptInitNoFund};
                    mptt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
                    id = mptt.issuanceID();
                    env.close();
                    return true;
                });
        }

        // A vault pseudo-account's MPToken cannot be deleted by anything
        // other than a VaultDelete transaction. Set up a vault, then have
        // an arbitrary tx erase the pseudo's MPToken in precheck.
        {
            uint256 vaultKey;
            doInvariantCheck(
                {{"vault pseudo-account holding deleted by a "
                  "non-VaultDelete transaction"}},
                [&](Account const&, Account const&, ApplyContext& ac) {
                    auto const sleVault = ac.view().peek(keylet::vault(vaultKey));
                    if (!sleVault)
                        return false;
                    auto const sleIssuance =
                        ac.view().peek(keylet::mptokenIssuance(sleVault->at(sfShareMPTID)));
                    if (!sleIssuance || !sleIssuance->isFieldPresent(sfReferenceHolding))
                        return false;
                    auto sleHolding = ac.view().peek(
                        keylet::unchecked(sleIssuance->getFieldH256(sfReferenceHolding)));
                    if (!sleHolding)
                        return false;
                    ac.view().erase(sleHolding);
                    return true;
                },
                XRPAmount{},
                STTx{ttACCOUNT_SET, [](STObject&) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                [&](Account const& a1, Account const&, Env& env) {
                    Account const issuer{"issuer"};
                    env.fund(XRP(10'000), issuer);
                    env.close();
                    MPTTester mptt{env, issuer, kMptInitNoFund};
                    mptt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
                    PrettyAsset const asset = mptt.issuanceID();
                    mptt.authorize({.account = a1});
                    env.close();

                    Vault const vault{env};
                    auto [tx, keylet] = vault.create({.owner = a1, .asset = asset});
                    env(tx);
                    env.close();
                    vaultKey = keylet.key;
                    return true;
                });
        }

        // Invalid transfer
        std::array<std::pair<TxType, bool>, 3> const invalidTransferTests = {
            std::make_pair(ttAMM_WITHDRAW, false),
            std::make_pair(ttPAYMENT, false),
            std::make_pair(ttPAYMENT, true)};
        // The two amendments that gate enforcement, in all four combinations.
        FeatureBitset const gatesEnabled{featureMPTokensV2, fixCleanup3_4_0};
        for (auto const gates :
             {gatesEnabled,
              gatesEnabled - featureMPTokensV2,
              gatesEnabled - fixCleanup3_4_0,
              FeatureBitset{}})
        {
            for (auto const& [tx, crossCurrencyPayment] : invalidTransferTests)
            {
                for (auto const flag :
                     {static_cast<std::uint32_t>(lsfMPTLocked),
                      ~lsfMPTCanTransfer,
                      ~lsfMPTCanTrade,
                      0u})
                {
                    MPTID id{};
                    // Issuance flags cannot be cleared after creation (and
                    // ValidMPTIssuance now rejects it), so the CanTransfer /
                    // CanTrade rows create the issuance without the bit
                    // instead of stripping it in precheck.
                    std::uint32_t const createFlags =
                        (flag == 0u || flag == lsfMPTLocked) ? kMptDexFlags : (kMptDexFlags & flag);
                    auto const isSuccess = !gates.any() || flag == 0 ||
                        (tx == ttPAYMENT && !crossCurrencyPayment && (flag == ~lsfMPTCanTrade)) ||
                        (tx == ttAMM_WITHDRAW &&
                         (flag == ~lsfMPTCanTrade || flag == ~lsfMPTCanTransfer));
                    std::pair<TER, TER> const error = isSuccess
                        ? std::make_pair(TER(tesSUCCESS), TER(tesSUCCESS))
                        : std::make_pair(TER(tecINVARIANT_FAILED), TER(tefINVARIANT_FAILED));
                    doInvariantCheck(
                        {{isSuccess ? "" : "invalid MPToken transfer between holders"}},
                        [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                            auto update = [&](AccountID const& a, std::uint64_t v) {
                                auto sle = ac.view().peek(keylet::mptoken(id, a));
                                if (!sle)
                                    return false;
                                sle->at(sfMPTAmount) = v;
                                ac.view().update(sle);
                                return true;
                            };
                            auto issuanceSle = ac.view().peek(keylet::mptokenIssuance(id));
                            if (!issuanceSle)
                                return false;
                            if (flag == lsfMPTLocked)
                            {
                                issuanceSle->at(sfFlags) = issuanceSle->at(sfFlags) | lsfMPTLocked;
                            }
                            issuanceSle->at(sfOutstandingAmount) = 200;
                            ac.view().update(issuanceSle);
                            return update(a1, 101) && update(a2, 99);
                        },
                        XRPAmount{},
                        STTx{
                            tx,
                            [&](STObject& tx) {
                                if (crossCurrencyPayment)
                                {
                                    tx.setFieldAmount(
                                        sfSendMax, STAmount(MPTAmount{100}, MPTIssue{id}));
                                }
                            }},
                        {error.first, error.second},
                        [&](Account const& a1, Account const& a2, Env& env) {
                            Account const gw("gw");
                            env.fund(XRP(1'000), gw);
                            MPTTester const usd(
                                {.env = env,
                                 .issuer = gw,
                                 .holders = {a1, a2},
                                 .pay = 100,
                                 .flags = createFlags});
                            id = usd.issuanceID();
                            // Either gate enforces, so both must be off to stay
                            // advisory. Disable after setting up the MPT; the
                            // next env.close() is what makes it take effect.
                            if (!gates[featureMPTokensV2])
                                env.disableFeature(featureMPTokensV2);
                            if (!gates[fixCleanup3_4_0])
                                env.disableFeature(fixCleanup3_4_0);
                            return true;
                        });
                }
            }
        }

        // An orphan has a zero balance, so only deletion is legitimate (see
        // "Skipping Deleted MPTs" in testConfidentialMPTTransfer).
        {
            MPTID orphanID;
            auto const setupOrphan = [&](Account const& a1, Account const& a2, Env& env) {
                MPTTester mpt(env, a1, {.holders = {a2}, .fund = false});
                mpt.create({.flags = tfMPTCanTransfer});
                orphanID = mpt.issuanceID();
                // A2 is authorized but never paid, so its balance is zero and
                // the issuance can be destroyed while its MPToken lives on.
                mpt.authorize({.account = a2});
                mpt.destroy();
                return true;
            };
            // ValidMPTBalanceChanges also reports this, so assert on the
            // orphan message, which only the missing-issuance branch produces.
            doInvariantCheck(
                {{"orphaned MPToken balance changed"}},
                [&](Account const&, Account const& a2, ApplyContext& ac) {
                    auto sleTok = ac.view().peek(keylet::mptoken(orphanID, a2.id()));
                    if (!sleTok || (*sleTok)[sfMPTAmount] != 0)
                        return false;
                    (*sleTok)[sfMPTAmount] = (*sleTok)[sfMPTAmount] + 10;
                    ac.view().update(sleTok);
                    return true;
                },
                XRPAmount{},
                STTx{ttMPTOKEN_AUTHORIZE, [](STObject&) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                setupOrphan);
            // Negative control: erasing the orphan is how it gets cleaned up.
            doInvariantCheck(
                {},
                [&](Account const&, Account const& a2, ApplyContext& ac) {
                    auto sleTok = ac.view().peek(keylet::mptoken(orphanID, a2.id()));
                    if (!sleTok)
                        return false;
                    ac.view().erase(sleTok);
                    return true;
                },
                XRPAmount{},
                STTx{ttMPTOKEN_AUTHORIZE, [](STObject&) {}},
                {tesSUCCESS, tesSUCCESS},
                setupOrphan);
            // The same erase on a failure. The orphan branch continues, so only
            // the pre-loop deletion check can report this one.
            doInvariantCheck(
                {{"MPToken deleted on failure"}},
                [&](Account const&, Account const& a2, ApplyContext& ac) {
                    auto sleTok = ac.view().peek(keylet::mptoken(orphanID, a2.id()));
                    if (!sleTok)
                        return false;
                    ac.view().erase(sleTok);
                    return true;
                },
                XRPAmount{},
                STTx{ttMPTOKEN_AUTHORIZE, [](STObject&) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                setupOrphan,
                TxAccount::None,
                std::source_location::current(),
                tecEXPIRED);
        }

        // Vault-share freeze invariant: isVaultPseudoAccountFrozen descends
        // through sfReferenceHolding to test the vault's underlying asset for
        // each changed holder.
        {
            Account const gw{"gw"};
            MPTID shareID{};

            // Vault setup: a1 and a2 both deposit IOU and hold vault shares.
            auto const setupVault = [&](Account const& a1,
                                        Account const& a2,
                                        Env& env) -> std::tuple<MPTID, AccountID> {
                env.fund(XRP(1'000), gw);
                env.trust(gw["IOU"](10'000), a1);
                env.trust(gw["IOU"](10'000), a2);
                env.close();
                env(pay(gw, a1, gw["IOU"](500)));
                env(pay(gw, a2, gw["IOU"](500)));
                env.close();

                Vault const vault{env};
                auto [createTx, vaultKeylet] = vault.create({.owner = a1, .asset = gw["IOU"]});
                env(createTx);
                env.close();
                env(vault.deposit(
                    {.depositor = a1, .id = vaultKeylet.key, .amount = gw["IOU"](100)}));
                env(vault.deposit(
                    {.depositor = a2, .id = vaultKeylet.key, .amount = gw["IOU"](100)}));
                env.close();

                return {env.le(vaultKeylet)->at(sfShareMPTID), env.le(vaultKeylet)->at(sfAccount)};
            };

            // Simulate a vault-share transfer: a1 sends 10 shares to a2.
            auto const precheck =
                [&](Account const& a1, Account const& a2, ApplyContext& ac) -> bool {
                auto sle1 = ac.view().peek(keylet::mptoken(shareID, a1.id()));
                auto sle2 = ac.view().peek(keylet::mptoken(shareID, a2.id()));
                if (!sle1 || !sle2)
                    return false;
                (*sle1)[sfMPTAmount] -= 10;
                (*sle2)[sfMPTAmount] += 10;
                ac.view().update(sle1);
                ac.view().update(sle2);
                return true;
            };

            // Case: vault pseudo-account's IOU trustline is frozen.
            {
                auto const preclose = [&](Account const& a1, Account const& a2, Env& env) -> bool {
                    auto [sid, vid] = setupVault(a1, a2, env);
                    shareID = sid;
                    env(trust(gw, gw["IOU"](0), Account{"vaultPseudo", vid}, tfSetFreeze));
                    env.close();
                    return true;
                };

                doInvariantCheck(
                    Env{*this, all_},
                    {{"invalid MPToken transfer between holders"}},
                    precheck,
                    XRPAmount{},
                    STTx{ttPAYMENT, [](STObject&) {}},
                    {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                    preclose);
            }

            // Case: receiver's (a2's) IOU trustline is frozen.
            {
                auto const preclose = [&](Account const& a1, Account const& a2, Env& env) -> bool {
                    auto [sid, vid] = setupVault(a1, a2, env);
                    shareID = sid;
                    env(trust(gw, gw["IOU"](0), a2, tfSetFreeze));
                    env.close();
                    return true;
                };

                doInvariantCheck(
                    Env{*this, all_},
                    {{"invalid MPToken transfer between holders"}},
                    precheck,
                    XRPAmount{},
                    STTx{ttPAYMENT, [](STObject&) {}},
                    {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                    preclose);
            }
        }
    }

    void
    testConfidentialMPTTransfer()
    {
        using namespace test::jtx;
        testcase << "ValidConfidentialMPToken";

        MPTID mptID;

        // Generate an MPT with privacy, issue 100 tokens to A2.
        // Perform a confidential conversion to populate encrypted state.
        auto const precloseConfidential =
            [&mptID](Account const& a1, Account const& a2, Env& env) -> bool {
            MPTTester mpt(env, a1, {.holders = {a2}, .fund = false});
            mpt.create({.flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance});
            mptID = mpt.issuanceID();

            mpt.authorize({.account = a2});
            mpt.pay(a1, a2, 100);

            mpt.generateKeyPair(a1);
            mpt.set({.account = a1, .issuerPubKey = mpt.getPubKey(a1)});

            mpt.generateKeyPair(a2);
            mpt.convert({
                .account = a2,
                .amt = 100,
                .holderPubKey = mpt.getPubKey(a2),
            });
            return true;
        };

        // badDelete
        doInvariantCheck(
            {"MPToken deleted with encrypted fields while COA > 0"},
            [&mptID](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto sleToken = ac.view().peek(keylet::mptoken(mptID, a2.id()));
                if (!sleToken)
                    return false;
                // Force an erase of the object while the COA remains 100
                ac.view().erase(sleToken);
                return true;
            },
            XRPAmount{},
            STTx{ttMPTOKEN_AUTHORIZE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseConfidential);

        // badConsistency
        doInvariantCheck(
            {"MPToken encrypted field existence inconsistency"},
            [&mptID](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto sleToken = ac.view().peek(keylet::mptoken(mptID, a2.id()));
                if (!sleToken)
                    return false;
                // Remove one of the required encrypted fields to create a mismatch
                sleToken->makeFieldAbsent(sfIssuerEncryptedBalance);
                ac.view().update(sleToken);
                return true;
            },
            XRPAmount{},
            STTx{ttMPTOKEN_AUTHORIZE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseConfidential);

        doInvariantCheck(
            {"MPToken encrypted field existence inconsistency"},
            [&mptID](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto sleToken = ac.view().peek(keylet::mptoken(mptID, a2.id()));
                if (!sleToken)
                    return false;
                sleToken->makeFieldAbsent(sfIssuerEncryptedBalance);
                sleToken->makeFieldAbsent(sfConfidentialBalanceInbox);
                sleToken->makeFieldAbsent(sfConfidentialBalanceSpending);
                sleToken->setFieldVL(sfAuditorEncryptedBalance, Blob{0x00});
                ac.view().update(sleToken);
                return true;
            },
            XRPAmount{},
            STTx{ttMPTOKEN_AUTHORIZE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseConfidential);

        // requiresPrivacyFlag
        auto const precloseNoPrivacy = [&mptID](
                                           Account const& a1, Account const& a2, Env& env) -> bool {
            MPTTester mpt(env, a1, {.holders = {a2}, .fund = false});
            // completely omitted the tfMPTCanHoldConfidentialBalance flag here.
            mpt.create({.flags = tfMPTCanTransfer});
            mptID = mpt.issuanceID();
            mpt.authorize({.account = a2});
            mpt.pay(a1, a2, 100);
            return true;
        };

        doInvariantCheck(
            {"MPToken has encrypted fields but Issuance does not have "
             "lsfMPTCanHoldConfidentialBalance "
             "set"},
            [&mptID](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto sleToken = ac.view().peek(keylet::mptoken(mptID, a2.id()));
                if (!sleToken)
                    return false;
                // Inject all three encrypted fields consistently (inbox+spending+issuer must be
                // in sync or badConsistency fires first and masks requiresPrivacyFlag).
                sleToken->setFieldVL(sfConfidentialBalanceInbox, Blob{0x00});
                sleToken->setFieldVL(sfConfidentialBalanceSpending, Blob{0x00});
                sleToken->setFieldVL(sfIssuerEncryptedBalance, Blob{0x00});
                ac.view().update(sleToken);
                return true;
            },
            XRPAmount{},
            STTx{ttMPTOKEN_AUTHORIZE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseNoPrivacy);

        // badCOA
        doInvariantCheck(
            {"Confidential outstanding amount exceeds total outstanding amount"},
            [&mptID](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto sleIssuance = ac.view().peek(keylet::mptokenIssuance(mptID));
                if (!sleIssuance)
                    return false;
                // Total outstanding is natively 100; bloat the COA over 100
                sleIssuance->setFieldU64(sfConfidentialOutstandingAmount, 200);
                ac.view().update(sleIssuance);
                return true;
            },
            XRPAmount{},
            STTx{ttMPTOKEN_ISSUANCE_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseConfidential);

        // Conservation Violation
        doInvariantCheck(
            {"Token conservation violation for MPT"},
            [&mptID](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto sleIssuance = ac.view().peek(keylet::mptokenIssuance(mptID));
                if (!sleIssuance)
                    return false;

                sleIssuance->setFieldU64(
                    sfConfidentialOutstandingAmount,
                    sleIssuance->getFieldU64(sfConfidentialOutstandingAmount) - 10);
                ac.view().update(sleIssuance);

                return true;
            },
            XRPAmount{},
            STTx{ttMPTOKEN_AUTHORIZE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseConfidential);

        // Send/MergeInbox must not change OutstandingAmount (coaDelta == 0)
        doInvariantCheck(
            {"Invariant failed: OutstandingAmount changed "
             "by confidential transaction that should not "
             "modify it for MPT"},
            [&mptID](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto sleIssuance = ac.view().peek(keylet::mptokenIssuance(mptID));
                if (!sleIssuance)
                    return false;
                sleIssuance->setFieldU64(
                    sfOutstandingAmount, sleIssuance->getFieldU64(sfOutstandingAmount) + 1);
                ac.view().update(sleIssuance);
                return true;
            },
            XRPAmount{},
            STTx{ttCONFIDENTIAL_MPT_SEND, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseConfidential);

        // Send/MergeInbox and zero-COA-delta confidential transactions must not
        // change public holder MPTAmount.
        doInvariantCheck(
            {"Invariant failed: MPTAmount changed by confidential "
             "transaction that should not modify this field."},
            [&mptID](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto sleToken = ac.view().peek(keylet::mptoken(mptID, a2.id()));
                if (!sleToken)
                    return false;
                sleToken->setFieldU64(sfMPTAmount, sleToken->getFieldU64(sfMPTAmount) + 1);
                ac.view().update(sleToken);
                return true;
            },
            XRPAmount{},
            STTx{ttCONFIDENTIAL_MPT_SEND, [](STObject&) {}},
            // Second pass is tef: the bumped MPTAmount also trips
            // ValidMPTTransfer's on-failure check, which escalates the tec.
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            precloseConfidential);

        // badVersion
        doInvariantCheck(
            {"MPToken sfConfidentialBalanceVersion not updated when sfConfidentialBalanceSpending "
             "changed"},
            [&mptID](Account const& a1, Account const& a2, ApplyContext& ac) {
                Blob const kChangedConfidentialSpending = {0xBA, 0xDD};
                auto sleToken = ac.view().peek(keylet::mptoken(mptID, a2.id()));
                if (!sleToken)
                    return false;
                sleToken->setFieldVL(sfConfidentialBalanceSpending, kChangedConfidentialSpending);

                // DO NOT update sfConfidentialBalanceVersion
                ac.view().update(sleToken);
                return true;
            },
            XRPAmount{},
            STTx{ttMPTOKEN_AUTHORIZE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            precloseConfidential);

        // Skipping Deleted MPTs (Issuance deleted)
        auto const precloseOrphan = [&mptID](
                                        Account const& a1, Account const& a2, Env& env) -> bool {
            MPTTester mpt(env, a1, {.holders = {a2}, .fund = false});
            mpt.create({.flags = tfMPTCanTransfer | tfMPTCanHoldConfidentialBalance});
            mptID = mpt.issuanceID();
            mpt.authorize({.account = a2});

            // Generate privacy keys and convert 0 amount so Bob has the encrypted fields
            mpt.generateKeyPair(a1);
            mpt.set({.account = a1, .issuerPubKey = mpt.getPubKey(a1)});
            mpt.generateKeyPair(a2);
            mpt.convert({
                .account = a2,
                .amt = 0,
                .holderPubKey = mpt.getPubKey(a2),
            });

            // Immediately destroy the issuance. A2's empty, encrypted token object lives on.
            mpt.destroy();
            return true;
        };

        doInvariantCheck(
            {},
            [&mptID](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto sleToken = ac.view().peek(keylet::mptoken(mptID, a2.id()));
                if (!sleToken)
                    return false;
                // Safely able to erase the deleted token.
                ac.view().erase(sleToken);
                return true;
            },
            XRPAmount{},
            STTx{ttMPTOKEN_AUTHORIZE, [](STObject&) {}},
            {tesSUCCESS, tesSUCCESS},
            precloseOrphan);
    }

public:
    void
    run() override
    {
        testConfidentialMPTTransfer();
        testMPT();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsMPT, app, xrpl);

}  // namespace xrpl::test
