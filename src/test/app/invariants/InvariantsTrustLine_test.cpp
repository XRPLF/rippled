#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/AMM.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/check.h>
#include <test/jtx/flags.h>
#include <test/jtx/offer.h>
#include <test/jtx/pay.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/ter.h>
#include <test/jtx/token.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>
#include <test/jtx/vault.h>

#include <xrpld/app/ledger/OpenLedger.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/applySteps.h>

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::test {

class InvariantsTrustLine_test : public InvariantsBase
{
    void
    testNoXRPTrustLine()
    {
        using namespace test::jtx;
        testcase << "trust lines with XRP not allowed";
        doInvariantCheck(
            {{"an XRP trust line was created"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                // create simple trust SLE with xrp currency
                auto const sleNew =
                    std::make_shared<SLE>(keylet::trustLine(a1, a2, xrpIssue().currency));
                ac.view().insert(sleNew);
                return true;
            });
    }

    void
    testNoDeepFreezeTrustLinesWithoutFreeze()
    {
        using namespace test::jtx;
        testcase << "trust lines with deep freeze flag without freeze "
                    "not allowed";
        doInvariantCheck(
            {{"a trust line with deep freeze flag without normal freeze was "
              "created"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sleNew =
                    std::make_shared<SLE>(keylet::trustLine(a1, a2, a1["USD"].currency));
                sleNew->setFieldAmount(sfLowLimit, a1["USD"](0));
                sleNew->setFieldAmount(sfHighLimit, a1["USD"](0));

                std::uint32_t uFlags = 0u;
                uFlags |= lsfLowDeepFreeze;
                sleNew->setFieldU32(sfFlags, uFlags);
                ac.view().insert(sleNew);
                return true;
            });

        doInvariantCheck(
            {{"a trust line with deep freeze flag without normal freeze was "
              "created"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sleNew =
                    std::make_shared<SLE>(keylet::trustLine(a1, a2, a1["USD"].currency));
                sleNew->setFieldAmount(sfLowLimit, a1["USD"](0));
                sleNew->setFieldAmount(sfHighLimit, a1["USD"](0));
                std::uint32_t uFlags = 0u;
                uFlags |= lsfHighDeepFreeze;
                sleNew->setFieldU32(sfFlags, uFlags);
                ac.view().insert(sleNew);
                return true;
            });

        doInvariantCheck(
            {{"a trust line with deep freeze flag without normal freeze was "
              "created"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sleNew =
                    std::make_shared<SLE>(keylet::trustLine(a1, a2, a1["USD"].currency));
                sleNew->setFieldAmount(sfLowLimit, a1["USD"](0));
                sleNew->setFieldAmount(sfHighLimit, a1["USD"](0));
                std::uint32_t uFlags = 0u;
                uFlags |= lsfLowDeepFreeze | lsfHighDeepFreeze;
                sleNew->setFieldU32(sfFlags, uFlags);
                ac.view().insert(sleNew);
                return true;
            });

        doInvariantCheck(
            {{"a trust line with deep freeze flag without normal freeze was "
              "created"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sleNew =
                    std::make_shared<SLE>(keylet::trustLine(a1, a2, a1["USD"].currency));
                sleNew->setFieldAmount(sfLowLimit, a1["USD"](0));
                sleNew->setFieldAmount(sfHighLimit, a1["USD"](0));
                std::uint32_t uFlags = 0u;
                uFlags |= lsfLowDeepFreeze | lsfHighFreeze;
                sleNew->setFieldU32(sfFlags, uFlags);
                ac.view().insert(sleNew);
                return true;
            });

        doInvariantCheck(
            {{"a trust line with deep freeze flag without normal freeze was "
              "created"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sleNew =
                    std::make_shared<SLE>(keylet::trustLine(a1, a2, a1["USD"].currency));
                sleNew->setFieldAmount(sfLowLimit, a1["USD"](0));
                sleNew->setFieldAmount(sfHighLimit, a1["USD"](0));
                std::uint32_t uFlags = 0u;
                uFlags |= lsfLowFreeze | lsfHighDeepFreeze;
                sleNew->setFieldU32(sfFlags, uFlags);
                ac.view().insert(sleNew);
                return true;
            });
    }

    void
    testTransfersNotFrozen()
    {
        using namespace test::jtx;
        testcase << "transfers when frozen";

        Account const g1{"G1"};
        // Helper function to establish the trustlines
        auto const createTrustlines = [&](Account const& a1, Account const& a2, Env& env) {
            // Preclose callback to establish trust lines with gateway
            env.fund(XRP(1000), g1);

            env.trust(g1["USD"](10000), a1);
            env.trust(g1["USD"](10000), a2);
            env.close();

            env(pay(g1, a1, g1["USD"](1000)));
            env(pay(g1, a2, g1["USD"](1000)));
            env.close();

            return true;
        };

        auto const a1FrozenByIssuer = [&](Account const& a1, Account const& a2, Env& env) {
            createTrustlines(a1, a2, env);
            env(trust(g1, a1["USD"](10000), tfSetFreeze));
            env.close();

            return true;
        };

        auto const a1DeepFrozenByIssuer = [&](Account const& a1, Account const& a2, Env& env) {
            a1FrozenByIssuer(a1, a2, env);
            env(trust(g1, a1["USD"](10000), tfSetDeepFreeze));
            env.close();

            return true;
        };

        auto const changeBalances = [&](Account const& a1,
                                        Account const& a2,
                                        ApplyContext& ac,
                                        int a1Balance,
                                        int a2Balance) {
            auto const sleA1 = ac.view().peek(keylet::trustLine(a1, g1["USD"]));
            auto const sleA2 = ac.view().peek(keylet::trustLine(a2, g1["USD"]));

            sleA1->setFieldAmount(sfBalance, g1["USD"](a1Balance));
            sleA2->setFieldAmount(sfBalance, g1["USD"](a2Balance));

            ac.view().update(sleA1);
            ac.view().update(sleA2);
        };

        // test: imitating frozen A1 making a payment to A2.
        doInvariantCheck(
            {{"Attempting to move frozen funds"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                changeBalances(a1, a2, ac, -900, -1100);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            a1FrozenByIssuer);

        // test: imitating deep frozen A1 making a payment to A2.
        doInvariantCheck(
            {{"Attempting to move frozen funds"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                changeBalances(a1, a2, ac, -900, -1100);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            a1DeepFrozenByIssuer);

        // test: imitating A2 making a payment to deep frozen A1.
        doInvariantCheck(
            {{"Attempting to move frozen funds"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                changeBalances(a1, a2, ac, -1100, -900);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            a1DeepFrozenByIssuer);
    }

    void
    testValidTrustLineAuth()
    {
        using namespace test::jtx;
        testcase << "unauthorized trust lines";

        Account const g1{"G1"};

        // Preclose: g1 requires authorization and a1 opens a line that g1
        // never authorizes. No valid transaction can give it a balance.
        auto const unauthorizedLine = [&](Account const& a1, Account const& a2, Env& env) {
            env.fund(XRP(1000), g1);
            env(fset(g1, asfRequireAuth));
            env.close();
            env.trust(g1["USD"](10000), a1);
            env.close();
            return true;
        };

        auto const authorizedLine = [&](Account const& a1, Account const& a2, Env& env) {
            unauthorizedLine(a1, a2, env);
            env(trust(g1, g1["USD"](0), a1, tfSetfAuth));
            env.close();
            return true;
        };

        // Overwrite the holder's balance of g1/USD on the a1--g1 line;
        // sfBalance is signed from the low account's perspective.
        auto const setHolderBalance = [&](Account const& holder, ApplyContext& ac, int amount) {
            auto const sle = ac.view().peek(keylet::trustLine(holder, g1["USD"]));
            if (!sle)
                return false;
            bool const holderIsLow = holder.id() < g1.id();
            sle->setFieldAmount(sfBalance, g1["USD"](holderIsLow ? amount : -amount));
            ac.view().update(sle);
            return true;
        };

        // test: an unauthorized line must not gain funds (post
        // fixCleanup3_4_0 the invariant enforces).
        doInvariantCheck(
            {{"an unauthorized trust line gained funds"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                return setHolderBalance(a1, ac, 100);
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            unauthorizedLine);

        // test: pre fixCleanup3_4_0 the same violation is logged but the
        // transaction is not failed, preserving legacy behavior on ledgers
        // without the amendment.
        doInvariantCheck(
            makeEnv(testableAmendments() - fixCleanup3_4_0),
            {{"an unauthorized trust line gained funds"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                return setHolderBalance(a1, ac, 100);
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject& tx) {}},
            {tesSUCCESS, tesSUCCESS},
            unauthorizedLine);

        // test: once the issuer grants authorization the line may gain funds.
        doInvariantCheck(
            {},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                return setHolderBalance(a1, ac, 100);
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject& tx) {}},
            {tesSUCCESS, tesSUCCESS},
            authorizedLine);

        // test: the authorization must predate the transaction. Stamping the
        // flag onto the line in the same transaction that credits it can
        // only be a buggy transactor -- a legitimate grant (TrustSet with
        // tfSetfAuth) never moves a balance.
        doInvariantCheck(
            {{"an unauthorized trust line gained funds"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const sle = ac.view().peek(keylet::trustLine(a1, g1["USD"]));
                if (!sle)
                    return false;
                bool const g1IsLow = g1.id() < a1.id();
                sle->setFieldU32(sfFlags, sle->getFlags() | (g1IsLow ? lsfLowAuth : lsfHighAuth));
                ac.view().update(sle);
                return setHolderBalance(a1, ac, 100);
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            unauthorizedLine);

        // test: a gain under a missing issuer account root -- possible only
        // through a buggy transactor -- leaves authorization unknowable, so
        // it fails closed with every exemption withheld.
        doInvariantCheck(
            {{"an unauthorized trust line gained funds (missing issuer account root)"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto const root = ac.view().peek(keylet::account(g1.id()));
                if (!root)
                    return false;
                ac.view().erase(root);
                return setHolderBalance(a1, ac, 100);
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            unauthorizedLine);

        // test: an issuer without lsfRequireAuth needs no authorization.
        doInvariantCheck(
            {},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                return setHolderBalance(a1, ac, 100);
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject& tx) {}},
            {tesSUCCESS, tesSUCCESS},
            [&](Account const& a1, Account const& a2, Env& env) {
                env.fund(XRP(1000), g1);
                env.trust(g1["USD"](10000), a1);
                env.close();
                return true;
            });

        // Unauthorized lines with a balance exist in ledger history -- they
        // predate today's authorization checks (XRPLF/rippled issue #5450).
        // No valid transaction can recreate that state, so seed it directly
        // into the open ledger, bypassing the transaction pipeline.
        auto const seedLegacyBalance = [&](Env& env, Account const& a1, Account const& a2) {
            env.fund(XRP(1000), a1, a2, g1);
            env(fset(g1, asfRequireAuth));
            env.close();
            env.trust(g1["USD"](10000), a1);
            env.trust(g1["USD"](10000), a2);
            env(trust(g1, g1["USD"](0), a2, tfSetfAuth));
            env.close();
            env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) {
                auto const sle =
                    std::make_shared<SLE>(*view.read(keylet::trustLine(a1, g1["USD"])));
                bool const holderIsLow = a1.id() < g1.id();
                sle->setFieldAmount(sfBalance, g1["USD"](holderIsLow ? 100 : -100));
                view.rawReplace(sle);
                return true;
            });
        };

        // test: draining a legacy unauthorized balance stays legal, so the
        // funds can still be redeemed or clawed back rather than stranded.
        {
            Account const a1{"A1"};
            Account const a2{"A2"};
            Env env = makeEnv(testableAmendments());
            seedLegacyBalance(env, a1, a2);
            doInvariantCheck(
                std::move(env),
                a1,
                a2,
                {},
                [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                    return setHolderBalance(a1, ac, 40);
                },
                XRPAmount{},
                STTx{ttPAYMENT, [](STObject& tx) {}},
                {tesSUCCESS, tesSUCCESS});
        }

        // test: a legacy unauthorized balance must not grow any further.
        {
            Account const a1{"A1"};
            Account const a2{"A2"};
            Env env = makeEnv(testableAmendments());
            seedLegacyBalance(env, a1, a2);
            doInvariantCheck(
                std::move(env),
                a1,
                a2,
                {{"an unauthorized trust line gained funds"}},
                [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                    return setHolderBalance(a1, ac, 150);
                },
                XRPAmount{},
                STTx{ttPAYMENT, [](STObject& tx) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
        }

        // test: a legacy unauthorized balance must not be spent to a third
        // party, not even an authorized one; only draining back to the
        // issuer is permitted (post fixCleanup3_4_0 the invariant enforces).
        {
            Account const a1{"A1"};
            Account const a2{"A2"};
            Env env = makeEnv(testableAmendments());
            seedLegacyBalance(env, a1, a2);
            doInvariantCheck(
                std::move(env),
                a1,
                a2,
                {{"an unauthorized trust line spent funds"}},
                [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                    return setHolderBalance(a1, ac, 40) && setHolderBalance(a2, ac, 60);
                },
                XRPAmount{},
                STTx{ttPAYMENT, [](STObject& tx) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
        }

        // test: pre fixCleanup3_4_0 the same spend is logged but the
        // transaction is not failed -- legacy behavior let an unauthorized
        // balance be spent to any party.
        {
            Account const a1{"A1"};
            Account const a2{"A2"};
            Env env = makeEnv(testableAmendments() - fixCleanup3_4_0);
            seedLegacyBalance(env, a1, a2);
            doInvariantCheck(
                std::move(env),
                a1,
                a2,
                {{"an unauthorized trust line spent funds"}},
                [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                    return setHolderBalance(a1, ac, 40) && setHolderBalance(a2, ac, 60);
                },
                XRPAmount{},
                STTx{ttPAYMENT, [](STObject& tx) {}},
                {tesSUCCESS, tesSUCCESS});
        }

        // test: cashing out a legacy unauthorized balance on the DEX is the
        // same spend, just initiated by the taker. Post fixCleanup3_4_0 the
        // unauthorized balance reads as zero in accountFunds, so the offer
        // cannot even be placed and the book stays clean of landmines; pre
        // amendment the offer places and the taker's crossing succeeds.
        for (bool const withCleanup : {true, false})
        {
            Account const a1{"A1"};
            Account const a2{"A2"};
            Env env = makeEnv(
                withCleanup ? testableAmendments() : testableAmendments() - fixCleanup3_4_0);
            seedLegacyBalance(env, a1, a2);

            // a1 offers the unauthorized USD for XRP.
            if (withCleanup)
            {
                env(offer(a1, XRP(50), g1["USD"](50)), Ter(tecUNFUNDED_OFFER));

                // With nothing to cross, a2's offer just joins the book.
                env(offer(a2, g1["USD"](50), XRP(50)));
                BEAST_EXPECT(env.balance(a2, g1["USD"]) == g1["USD"](0));
            }
            else
            {
                env(offer(a1, XRP(50), g1["USD"](50)));

                // a2 -- authorized, and doing nothing wrong -- takes it.
                env(offer(a2, g1["USD"](50), XRP(50)));
                BEAST_EXPECT(env.balance(a2, g1["USD"]) == g1["USD"](50));
            }
        }

        // test: a payment crossing the line balance through zero would turn
        // the holder into an unauthorized holder. Historically the
        // transactor's auth gate (DirectIPaymentStep::check) only fired on
        // an exactly-zero balance, waving the payment through on the
        // strength of the pre-existing opposite-direction balance. Both
        // behaviors documented: post fixCleanup3_4_0 the engine rejects the
        // crossing outright; pre amendment it succeeds and mints an
        // unauthorized balance.
        for (bool const withCleanup : {true, false})
        {
            Env env = makeEnv(
                withCleanup ? testableAmendments() : testableAmendments() - fixCleanup3_4_0);
            Account const a1{"A1"};
            env.fund(XRP(1000), a1, g1);
            env(fset(g1, asfRequireAuth));
            env.close();
            // Both sides extend trust for USD on the one shared line; g1
            // never authorizes a1.
            env(trust(g1, a1["USD"](1000)));
            env(trust(a1, g1["USD"](1000)));
            env.close();
            // a1 issues 5 USD to g1: g1 is the holder here, a1 requires no
            // authorization, and the shared line's balance is now nonzero.
            env(pay(a1, g1, a1["USD"](5)));
            env.close();
            // g1 pays 10 USD: 5 redeem g1's holdings and the other 5 would
            // make the unauthorized a1 a holder of g1's IOU.
            if (withCleanup)
            {
                env(pay(g1, a1, g1["USD"](10)), Ter(tecNO_AUTH));
                env.close();
                BEAST_EXPECT(env.balance(a1, g1["USD"]) == g1["USD"](-5));
            }
            else
            {
                env(pay(g1, a1, g1["USD"](10)));
                env.close();
                BEAST_EXPECT(env.balance(a1, g1["USD"]) == g1["USD"](5));
            }
        }

        // test: fully withdrawing an AMM that holds a RequireAuth IOU
        // deletes the AMM pseudo-account together with its trust lines in
        // the same transaction; the deleted pseudo-account must remain
        // exempt even though it is gone from the post-transaction view.
        {
            Env env = makeEnv(testableAmendments());
            Account const alice{"alice"};
            env.fund(XRP(30000), g1, alice);
            env(fset(g1, asfRequireAuth));
            env.close();
            env(trust(alice, g1["USD"](10000)));
            env(trust(g1, g1["USD"](0), alice, tfSetfAuth));
            env(pay(g1, alice, g1["USD"](1000)));
            env.close();

            AMM amm(env, alice, XRP(100), g1["USD"](100));
            BEAST_EXPECT(amm.ammExists());
            amm.withdrawAll(alice);
            BEAST_EXPECT(!amm.ammExists());
        }

        // test: pseudo-accounts are implicitly authorized (mirroring
        // requireAuth). The vault deposit in preclose already exercises the
        // exemption under real transactions -- the vault pseudo-account's
        // line to g1 gains funds carrying no authorization flag -- and the
        // precheck then grows the same line directly.
        std::optional<Keylet> vaultKeylet;
        doInvariantCheck(
            {},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                if (!vaultKeylet)
                    return false;
                auto const vaultSle = ac.view().read(*vaultKeylet);
                if (!vaultSle)
                    return false;
                AccountID const pseudo = vaultSle->at(sfAccount);
                auto const sle =
                    ac.view().peek(keylet::trustLine(pseudo, g1.id(), g1["USD"].currency));
                if (!sle)
                    return false;
                bool const pseudoIsLow = pseudo < g1.id();
                sle->setFieldAmount(sfBalance, g1["USD"](pseudoIsLow ? 150 : -150));
                ac.view().update(sle);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject& tx) {}},
            {tesSUCCESS, tesSUCCESS},
            [&](Account const& a1, Account const& a2, Env& env) {
                authorizedLine(a1, a2, env);
                env(pay(g1, a1, g1["USD"](500)));
                env.close();
                Vault const vault{env};
                auto const [tx, keylet] = vault.create({.owner = a1, .asset = Issue{g1["USD"]}});
                vaultKeylet = keylet;
                env(tx);
                env(vault.deposit({.depositor = a1, .id = keylet.key, .amount = g1["USD"](100)}));
                env.close();
                return env.le(keylet) != nullptr;
            });
    }

    void
    testUnauthorizedAccountHolds()
    {
        using namespace test::jtx;
        testcase << "accountHolds authorization handling";

        Account const g1{"G1"};
        Account const a1{"A1"};
        Account const a2{"A2"};

        for (bool const withCleanup : {true, false})
        {
            Env env = makeEnv(
                withCleanup ? testableAmendments() : testableAmendments() - fixCleanup3_4_0);
            env.fund(XRP(1000), a1, a2, g1);
            env(fset(g1, asfRequireAuth));
            env.close();
            env.trust(g1["USD"](10000), a1);
            env.trust(g1["USD"](10000), a2);
            env(trust(g1, g1["USD"](0), a2, tfSetfAuth));
            env.close();
            env(pay(g1, a2, g1["USD"](50)));

            // a1's unauthorized balance is legacy state (XRPLF/rippled issue
            // #5450); no valid transaction can create it.
            env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) {
                auto const sle =
                    std::make_shared<SLE>(*view.read(keylet::trustLine(a1, g1["USD"])));
                bool const holderIsLow = a1.id() < g1.id();
                sle->setFieldAmount(sfBalance, g1["USD"](holderIsLow ? 100 : -100));
                view.rawReplace(sle);
                return true;
            });

            auto const& view = *env.current();
            auto const j = env.journal;
            Issue const usd = g1["USD"];

            // IgnoreAuth always reports the raw balance.
            BEAST_EXPECT(
                accountHolds(
                    view, a1, usd, FreezeHandling::ZeroIfFrozen, AuthHandling::IgnoreAuth, j) ==
                g1["USD"](100));

            // ZeroIfUnauthorized hides the unauthorized balance, but only
            // once fixCleanup3_4_0 is enabled.
            BEAST_EXPECT(
                accountHolds(
                    view,
                    a1,
                    usd,
                    FreezeHandling::ZeroIfFrozen,
                    AuthHandling::ZeroIfUnauthorized,
                    j) == (withCleanup ? g1["USD"](0) : g1["USD"](100)));

            // An authorized holder is unaffected either way.
            BEAST_EXPECT(
                accountHolds(
                    view,
                    a2,
                    usd,
                    FreezeHandling::ZeroIfFrozen,
                    AuthHandling::ZeroIfUnauthorized,
                    j) == g1["USD"](50));
        }
    }

    void
    testLegacyUnauthorizedEndToEnd()
    {
        using namespace test::jtx;
        testcase << "legacy unauthorized balance end to end";

        Account const g1{"G1"};
        Account const a1{"A1"};
        Account const a2{"A2"};

        // Start pre-amendment, with the NFT authorization gap still open:
        // this is the very flow that created the unauthorized balances of
        // XRPLF/rippled issue #5450.
        Env env = makeEnv(testableAmendments() - fixEnforceNFTokenTrustlineV2 - fixCleanup3_4_0);
        env.fund(XRP(10000), g1, a1, a2);
        env(fset(g1, asfRequireAuth));
        env.close();
        env(trust(a1, g1["USD"](10000)));
        env(trust(g1, g1["USD"](0), a1, tfSetfAuth));
        env(pay(g1, a1, g1["USD"](1000)));
        env(trust(a2, g1["USD"](10000)));
        env.close();

        // a2 sells an NFT to the authorized a1 for USD and legally ends up
        // holding 10 USD without authorization.
        uint256 const nftID = token::getNextID(env, a2, 0u, tfTransferable);
        env(token::mint(a2, 0), Txflags(tfTransferable));
        env.close();
        uint256 const sellIdx = keylet::nftokenOffer(a2, SeqProxy::rawSequence(env.seq(a2))).key;
        env(token::createOffer(a2, nftID, g1["USD"](10)), Txflags(tfSellNFToken));
        env.close();
        env(token::acceptSellOffer(a1, sellIdx));
        env.close();
        BEAST_EXPECT(env.balance(a2, g1["USD"]) == g1["USD"](10));

        // a2 parks an offer selling the unauthorized USD; pre-amendment the
        // offer is funded and rests in the book.
        auto const staleOffer = keylet::offer(a2.id(), SeqProxy::rawSequence(env.seq(a2)));
        env(offer(a2, XRP(10), g1["USD"](10)));
        env.close();
        BEAST_EXPECT(env.le(staleOffer));

        // The amendment activates with the stale state in place.
        env.enableFeature(fixCleanup3_4_0);
        env.close();

        // The raw balance is untouched, but funding-style reads see zero.
        BEAST_EXPECT(env.balance(a2, g1["USD"]) == g1["USD"](10));
        Issue const usd = g1["USD"];
        BEAST_EXPECT(
            accountHolds(
                *env.current(),
                a2,
                usd,
                FreezeHandling::ZeroIfFrozen,
                AuthHandling::ZeroIfUnauthorized,
                env.journal) == g1["USD"](0));

        // a1 crosses the stale offer: it is removed as unfunded, nothing
        // trades, and a1's own offer rests. No third party eats a failure
        // for touching the leftover.
        auto const a1Offer = keylet::offer(a1.id(), SeqProxy::rawSequence(env.seq(a1)));
        env(offer(a1, g1["USD"](10), XRP(10)));
        env.close();
        BEAST_EXPECT(!env.le(staleOffer));
        BEAST_EXPECT(env.le(a1Offer));
        BEAST_EXPECT(env.balance(a2, g1["USD"]) == g1["USD"](10));

        // Spending to a third party is rejected by the payment engine
        // itself: the strand spends the unauthorized balance past the
        // issuer, so DirectStepI::check fails it cleanly.
        env(pay(a2, a1, g1["USD"](5)), Ter(tecNO_AUTH));
        env.close();

        // Converting the balance is spending it too: the USD hop of a
        // cross-currency payment is not the strand's last step, so the same
        // gate rejects it.
        env(pay(a2, a2, XRP(5)), Sendmax(g1["USD"](5)), Ter(tecNO_AUTH));
        env.close();

        // Receiving more is rejected by the payment engine as well: post
        // fixCleanup3_4_0 the grandfather clause no longer lets a nonzero
        // unauthorized line receive.
        env(pay(a1, a2, g1["USD"](5)), Ter(tecNO_AUTH));
        env.close();

        // A check to a third party cannot be cashed against the invisible
        // balance...
        uint256 const chkToA1 = keylet::check(a2.id(), SeqProxy::rawSequence(env.seq(a2))).key;
        env(check::create(a2, a1, g1["USD"](5)));
        env.close();
        env(check::cash(a1, chkToA1, g1["USD"](5)), Ter(tecPATH_PARTIAL));
        env.close();

        // ...but the issuer cashing a check is a redemption and stays legal.
        uint256 const chkToG1 = keylet::check(a2.id(), SeqProxy::rawSequence(env.seq(a2))).key;
        env(check::create(a2, g1, g1["USD"](4)));
        env.close();
        env(check::cash(g1, chkToG1, g1["USD"](4)));
        env.close();
        BEAST_EXPECT(env.balance(a2, g1["USD"]) == g1["USD"](6));

        // Returning the rest to the issuer stays legal and drains the
        // legacy state for good.
        env(pay(a2, g1, g1["USD"](6)));
        env.close();
        BEAST_EXPECT(env.balance(a2, g1["USD"]) == g1["USD"](0));

        // And nothing is left to fund a new offer.
        env(offer(a2, XRP(10), g1["USD"](10)), Ter(tecUNFUNDED_OFFER));
    }

    void
    testUnauthorizedCheckCash()
    {
        using namespace test::jtx;
        testcase << "checks written against an unauthorized balance";

        Account const g1{"G1"};
        Account const a1{"A1"};
        Account const a2{"A2"};

        for (bool const withCleanup : {true, false})
        {
            Env env = makeEnv(
                withCleanup ? testableAmendments() : testableAmendments() - fixCleanup3_4_0);
            env.fund(XRP(1000), a1, a2, g1);
            env(fset(g1, asfRequireAuth));
            env.close();
            env.trust(g1["USD"](10000), a1);
            env.trust(g1["USD"](10000), a2);
            env(trust(g1, g1["USD"](0), a2, tfSetfAuth));
            env.close();

            // Seed a1's legacy unauthorized balance into the open ledger; no
            // close from here on, or the raw state is lost.
            env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) {
                auto const sle =
                    std::make_shared<SLE>(*view.read(keylet::trustLine(a1, g1["USD"])));
                bool const holderIsLow = a1.id() < g1.id();
                sle->setFieldAmount(sfBalance, g1["USD"](holderIsLow ? 100 : -100));
                view.rawReplace(sle);
                return true;
            });

            // a1 writes checks to the authorized a2 and to the issuer.
            uint256 const toA2 = keylet::check(a1.id(), SeqProxy::rawSequence(env.seq(a1))).key;
            env(check::create(a1, a2, g1["USD"](5)));
            uint256 const toG1 = keylet::check(a1.id(), SeqProxy::rawSequence(env.seq(a1))).key;
            env(check::create(a1, g1, g1["USD"](4)));

            // Post fixCleanup3_4_0 the writer's unauthorized balance reads
            // as zero for a third-party casher; pre amendment the legacy
            // balance is spendable.
            if (withCleanup)
            {
                env(check::cash(a2, toA2, g1["USD"](5)), Ter(tecPATH_PARTIAL));
                BEAST_EXPECT(env.balance(a1, g1["USD"]) == g1["USD"](100));
            }
            else
            {
                env(check::cash(a2, toA2, g1["USD"](5)));
                BEAST_EXPECT(env.balance(a1, g1["USD"]) == g1["USD"](95));
                BEAST_EXPECT(env.balance(a2, g1["USD"]) == g1["USD"](5));
            }

            // The issuer cashing a check is a redemption and works in both
            // eras.
            env(check::cash(g1, toG1, g1["USD"](4)));
            BEAST_EXPECT(env.balance(a1, g1["USD"]) == g1["USD"](withCleanup ? 96 : 91));
        }
    }

    void
    testUnauthorizedEmptyLineReceive()
    {
        using namespace test::jtx;
        testcase << "unauthorized empty line cannot receive";

        Account const g1{"G1"};
        Account const a1{"A1"};

        for (bool const withCleanup : {true, false})
        {
            Env env = makeEnv(
                withCleanup ? testableAmendments() : testableAmendments() - fixCleanup3_4_0);
            env.fund(XRP(1000), a1, g1);
            env(fset(g1, asfRequireAuth));
            env.close();
            // a1 opens the line; g1 never authorizes it. Balance is zero.
            env.trust(g1["USD"](10000), a1);
            env.close();

            // Post fixCleanup3_4_0 the engine answers a final tecNO_AUTH;
            // before it, the retriable terNO_AUTH left a dry path.
            env(pay(g1, a1, g1["USD"](5)), Ter(withCleanup ? TER{tecNO_AUTH} : TER{tecPATH_DRY}));
            env.close();
            BEAST_EXPECT(env.balance(a1, g1["USD"]) == g1["USD"](0));

            // Authorization repairs it either way.
            env(trust(g1, g1["USD"](0), a1, tfSetfAuth));
            env.close();
            env(pay(g1, a1, g1["USD"](5)));
            env.close();
            BEAST_EXPECT(env.balance(a1, g1["USD"]) == g1["USD"](5));
        }
    }

    void
    testUnauthorizedOwnIssuance()
    {
        using namespace test::jtx;
        testcase << "unauthorized holder may still spend its own issuance";

        Account const g1{"G1"};
        Account const a1{"A1"};
        Account const a2{"A2"};

        Env env = makeEnv(testableAmendments());
        env.fund(XRP(1000), g1, a1, a2);
        env(fset(g1, asfRequireAuth));
        env(fset(g1, asfDefaultRipple));
        env.close();

        // g1 extends credit to a2 on their shared USD line; the line carries
        // no issuer-side auth flag, so a2 is unauthorized for g1's USD --
        // but the same line can still carry a2's own issuance to g1.
        env(trust(g1, a2["USD"](100)));
        env(trust(a1, g1["USD"](100)));
        env(trust(g1, g1["USD"](0), a1, tfSetfAuth));
        env.close();

        // a2 pays a1 in g1's USD funded purely by a2's own issuance: on the
        // a2 -> g1 hop the balance favors g1 (g1 holds a2's IOU), so the
        // spend gate must not fire.
        env(pay(a2, a1, g1["USD"](5)));
        env.close();
        BEAST_EXPECT(env.balance(a1, g1["USD"]) == g1["USD"](5));
        BEAST_EXPECT(env.balance(a2, g1["USD"]) == g1["USD"](-5));
    }

    void
    run() override
    {
        testNoXRPTrustLine();
        testNoDeepFreezeTrustLinesWithoutFreeze();
        testTransfersNotFrozen();
        testValidTrustLineAuth();
        testUnauthorizedAccountHolds();
        testLegacyUnauthorizedEndToEnd();
        testUnauthorizedCheckCash();
        testUnauthorizedEmptyLineReceive();
        testUnauthorizedOwnIssuance();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsTrustLine, app, xrpl);

}  // namespace xrpl::test
