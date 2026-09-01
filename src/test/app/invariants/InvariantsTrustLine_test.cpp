#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/AMM.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/flags.h>
#include <test/jtx/offer.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpld/app/ledger/OpenLedger.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/AccountID.h>
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
        // same spend, just initiated by the taker: the crossing moves the
        // funds to a third party. Real transactions end to end -- placing
        // the offer moves no funds and succeeds either way (accountHolds
        // does not check authorization yet, so the offer is funded), and
        // post fixCleanup3_4_0 the taker's crossing fails while pre
        // amendment it succeeds.
        for (bool const withCleanup : {true, false})
        {
            Account const a1{"A1"};
            Account const a2{"A2"};
            Env env = makeEnv(
                withCleanup ? testableAmendments() : testableAmendments() - fixCleanup3_4_0);
            seedLegacyBalance(env, a1, a2);

            // a1 offers the unauthorized USD for XRP.
            env(offer(a1, XRP(50), g1["USD"](50)));

            // a2 -- authorized, and doing nothing wrong -- takes it.
            if (withCleanup)
            {
                env(offer(a2, g1["USD"](50), XRP(50)), Ter(tecINVARIANT_FAILED));
                BEAST_EXPECT(env.balance(a2, g1["USD"]) == g1["USD"](0));
            }
            else
            {
                env(offer(a2, g1["USD"](50), XRP(50)));
                BEAST_EXPECT(env.balance(a2, g1["USD"]) == g1["USD"](50));
            }
        }

        // test: a payment crossing the line balance through zero would turn
        // the holder into an unauthorized holder. The transactor's auth gate
        // (DirectIPaymentStep::check) only fires on an exactly-zero balance,
        // so it waves the payment through on the strength of the pre-existing
        // opposite-direction balance. Both behaviors documented: post
        // fixCleanup3_4_0 the invariant blocks the crossing; pre amendment
        // it succeeds and mints an unauthorized balance.
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
                env(pay(g1, a1, g1["USD"](10)), Ter(tecINVARIANT_FAILED));
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
    run() override
    {
        testNoXRPTrustLine();
        testNoDeepFreezeTrustLinesWithoutFreeze();
        testTransfersNotFrozen();
        testValidTrustLineAuth();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsTrustLine, app, xrpl);

}  // namespace xrpl::test
