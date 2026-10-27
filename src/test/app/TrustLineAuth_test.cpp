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

#include <xrpld/app/ledger/OpenLedger.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <memory>
#include <optional>

namespace xrpl::test {

// Unauthorized trust lines carrying a balance exist in ledger history: they
// predate today's authorization checks (XRPLF/rippled issue #5450). Post
// fixCleanup3_5_0 such a balance may only return to its issuer; every
// funding-style read sees it as zero and the payment engine refuses to spend
// it past the issuer. These tests pin both eras.
class TrustLineAuth_test : public beast::unit_test::Suite
{
    // g1 requires authorization; a1 opens a line g1 never authorizes, a2 is
    // authorized. a1's balance is legacy state that no valid transaction can
    // create, so it is seeded straight into the open ledger. No close may
    // follow, or the raw state is lost.
    static void
    seedLegacyBalance(
        jtx::Env& env,
        jtx::Account const& g1,
        jtx::Account const& a1,
        jtx::Account const& a2,
        int amount)
    {
        using namespace jtx;
        env.fund(XRP(1000), a1, a2, g1);
        env(fset(g1, asfRequireAuth));
        env.close();
        env.trust(g1["USD"](10000), a1);
        env.trust(g1["USD"](10000), a2);
        env(trust(g1, g1["USD"](0), a2, tfSetfAuth));
        env.close();
        env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) {
            auto const sle = std::make_shared<SLE>(*view.read(keylet::trustLine(a1, g1["USD"])));
            bool const holderIsLow = a1.id() < g1.id();
            sle->setFieldAmount(sfBalance, g1["USD"](holderIsLow ? amount : -amount));
            view.rawReplace(sle);
            return true;
        });
    }

    static jtx::Env
    makeEnv(beast::unit_test::Suite& suite, bool withCleanup)
    {
        using namespace jtx;
        return withCleanup ? Env{suite} : Env{suite, testableAmendments() - fixCleanup3_5_0};
    }

    void
    testAccountHolds()
    {
        using namespace jtx;
        testcase("accountHolds authorization handling");

        Account const g1{"G1"};
        Account const a1{"A1"};
        Account const a2{"A2"};

        for (bool const withCleanup : {true, false})
        {
            Env env = makeEnv(*this, withCleanup);
            seedLegacyBalance(env, g1, a1, a2, 100);
            env(pay(g1, a2, g1["USD"](50)));

            auto const& view = *env.current();
            auto const j = env.journal;
            Issue const usd = g1["USD"];

            // IgnoreAuth always reports the raw balance.
            BEAST_EXPECT(
                accountHolds(
                    view, a1, usd, FreezeHandling::ZeroIfFrozen, AuthHandling::IgnoreAuth, j) ==
                g1["USD"](100));

            // ZeroIfUnauthorized hides the unauthorized balance, but only
            // once fixCleanup3_5_0 is enabled.
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
    testSpend()
    {
        using namespace jtx;
        testcase("unauthorized balance may only return to the issuer");

        Account const g1{"G1"};
        Account const a1{"A1"};
        Account const a2{"A2"};

        for (bool const withCleanup : {true, false})
        {
            Env env = makeEnv(*this, withCleanup);
            seedLegacyBalance(env, g1, a1, a2, 100);

            // a2 -- authorized, and doing nothing wrong -- buys USD for XRP,
            // so a1 has an offer to convert the balance against.
            env(offer(a2, g1["USD"](10), XRP(10)));

            if (withCleanup)
            {
                // Spending to a third party is rejected by the payment
                // engine itself: the strand spends the unauthorized balance
                // past the issuer, so DirectStepI::check fails it cleanly.
                env(pay(a1, a2, g1["USD"](5)), Ter(tecNO_AUTH));

                // Converting the balance is spending it too: the USD hop of
                // a cross-currency payment is not the strand's last step, so
                // the same gate rejects it.
                env(pay(a1, a1, XRP(5)), Sendmax(g1["USD"](5)), Ter(tecNO_AUTH));
                BEAST_EXPECT(env.balance(a1, g1["USD"]) == g1["USD"](100));
            }
            else
            {
                // Pre-amendment both settle.
                env(pay(a1, a2, g1["USD"](5)));
                env(pay(a1, a1, XRP(5)), Sendmax(g1["USD"](5)));
                BEAST_EXPECT(env.balance(a1, g1["USD"]) == g1["USD"](90));
            }

            // Returning the balance to the issuer stays legal in both eras
            // and drains the legacy state for good.
            env(pay(a1, g1, env.balance(a1, g1["USD"]).value()));
            BEAST_EXPECT(env.balance(a1, g1["USD"]) == g1["USD"](0));
        }
    }

    void
    testDexCashOut()
    {
        using namespace jtx;
        testcase("unauthorized balance cannot fund an offer");

        Account const g1{"G1"};
        Account const a1{"A1"};
        Account const a2{"A2"};

        // Cashing out on the DEX is the same spend, just initiated by the
        // taker. Post fixCleanup3_5_0 the unauthorized balance reads as zero
        // in accountFunds, so the offer cannot even be placed and the book
        // stays clean of landmines; pre amendment the offer places and the
        // taker's crossing succeeds.
        for (bool const withCleanup : {true, false})
        {
            Env env = makeEnv(*this, withCleanup);
            seedLegacyBalance(env, g1, a1, a2, 100);

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
                env(offer(a2, g1["USD"](50), XRP(50)));
                BEAST_EXPECT(env.balance(a2, g1["USD"]) == g1["USD"](50));
            }
        }
    }

    void
    testCheckCash()
    {
        using namespace jtx;
        testcase("checks written against an unauthorized balance");

        Account const g1{"G1"};
        Account const a1{"A1"};
        Account const a2{"A2"};

        for (bool const withCleanup : {true, false})
        {
            Env env = makeEnv(*this, withCleanup);
            seedLegacyBalance(env, g1, a1, a2, 100);

            // a1 writes checks to the authorized a2 and to the issuer.
            uint256 const toA2 = keylet::check(a1.id(), SeqProxy::rawSequence(env.seq(a1))).key;
            env(check::create(a1, a2, g1["USD"](5)));
            uint256 const toG1 = keylet::check(a1.id(), SeqProxy::rawSequence(env.seq(a1))).key;
            env(check::create(a1, g1, g1["USD"](4)));

            // Post fixCleanup3_5_0 the writer's unauthorized balance reads
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
    testOwnIssuance()
    {
        using namespace jtx;
        testcase("unauthorized holder may still spend its own issuance");

        Account const g1{"G1"};
        Account const a1{"A1"};
        Account const a2{"A2"};

        Env env{*this};
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
    testClawbackRecreatedBalance()
    {
        using namespace jtx;
        testcase("balance recreated by AMMClawback is locked");

        // gwc enables clawback before issuing; gw2 requires authorization
        // for eur2. alice, authorized, pools her entire eur2 balance; bob
        // is authorized and only serves as a would-be recipient.
        Account const gwc{"gwc"};
        Account const gw2{"gw2"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        auto const usdC = gwc["USD"];
        auto const eur2 = gw2["EUR"];

        for (bool const withCleanup : {true, false})
        {
            Env env = makeEnv(*this, withCleanup);
            env.fund(XRP(30'000), gwc, gw2, alice, bob);
            env.close();
            env(fset(gwc, asfAllowTrustLineClawback));
            env(fset(gw2, asfRequireAuth));
            env.close();

            env.trust(usdC(2'000), alice);
            env.trust(eur2(2'000), alice);
            env(trust(gw2, eur2(0), alice, tfSetfAuth));
            env.trust(eur2(2'000), bob);
            env(trust(gw2, eur2(0), bob, tfSetfAuth));
            env.close();
            env(pay(gwc, alice, usdC(1'000)));
            env(pay(gw2, alice, eur2(1'000)));
            env.close();

            AMM const amm(env, alice, usdC(1'000), eur2(1'000));

            // alice divests her now-empty eur2 line; only the pool holds
            // eur2.
            env(trust(alice, eur2(0)));
            env.close();
            BEAST_EXPECT(!env.le(keylet::trustLine(alice.id(), eur2)));

            // The clawback succeeds in both eras: issuer-driven remediation
            // is never held hostage by alice's missing eur2 authorization.
            // The paired eur2 lands on a recreated, unauthorized line.
            env(amm::ammClawback(gwc, alice, usdC, eur2, std::nullopt));
            env.close();
            BEAST_EXPECT(env.balance(alice, eur2) == eur2(1'000));

            if (withCleanup)
            {
                // The recreated balance is constrained like any other
                // unauthorized balance: it cannot reach a third party...
                env(pay(alice, bob, eur2(100)), Ter(tecNO_AUTH));
                env.close();

                // ...but returning it to the issuer stays legal.
                env(pay(alice, gw2, eur2(1'000)));
                env.close();
                BEAST_EXPECT(env.balance(alice, eur2) == eur2(0));
            }
            else
            {
                // Pre-amendment the unauthorized balance is freely spendable.
                env(pay(alice, bob, eur2(100)));
                env.close();
                BEAST_EXPECT(env.balance(bob, eur2) == eur2(100));
            }
        }
    }

    void
    testEmptyLineReceive()
    {
        using namespace jtx;
        testcase("unauthorized empty line cannot receive");

        Account const g1{"G1"};
        Account const a1{"A1"};

        for (bool const withCleanup : {true, false})
        {
            Env env = makeEnv(*this, withCleanup);
            env.fund(XRP(1000), a1, g1);
            env(fset(g1, asfRequireAuth));
            env.close();
            // a1 opens the line; g1 never authorizes it. Balance is zero.
            env.trust(g1["USD"](10000), a1);
            env.close();

            // Post fixCleanup3_5_0 the engine answers a final tecNO_AUTH;
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
    testZeroCrossing()
    {
        using namespace jtx;
        testcase("payment crossing zero cannot mint an unauthorized balance");

        Account const g1{"G1"};
        Account const a1{"A1"};

        // Historically the auth gate in DirectIPaymentStep::check only fired
        // on an exactly-zero balance, waving a payment through on the
        // strength of the pre-existing opposite-direction balance. Post
        // fixCleanup3_5_0 the engine delivers the redeemable part and then
        // goes dry, so a payment asking for more cannot complete; pre
        // amendment it succeeds and mints an unauthorized balance.
        for (bool const withCleanup : {true, false})
        {
            Env env = makeEnv(*this, withCleanup);
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
                // Only the 5 redeemable units are available, so the strand
                // goes dry short of the requested amount and the whole
                // payment fails without touching the balance.
                env(pay(g1, a1, g1["USD"](10)), Ter(tecPATH_PARTIAL));
                env.close();
                BEAST_EXPECT(env.balance(a1, g1["USD"]) == g1["USD"](-5));

                // Redeeming exactly what g1 holds creates no unauthorized
                // balance, so it stays legal and drains the line to zero.
                env(pay(g1, a1, g1["USD"](5)));
                env.close();
                BEAST_EXPECT(env.balance(a1, g1["USD"]) == g1["USD"](0));

                // With the line at zero there is nothing left to redeem, so
                // any further receive is the plain unauthorized-receive case.
                env(pay(g1, a1, g1["USD"](1)), Ter(tecNO_AUTH));
                env.close();
                BEAST_EXPECT(env.balance(a1, g1["USD"]) == g1["USD"](0));
            }
            else
            {
                env(pay(g1, a1, g1["USD"](10)));
                env.close();
                BEAST_EXPECT(env.balance(a1, g1["USD"]) == g1["USD"](5));
            }
        }
    }

    void
    testLegacyEndToEnd()
    {
        using namespace jtx;
        testcase("legacy unauthorized balance end to end");

        Account const g1{"G1"};
        Account const a1{"A1"};
        Account const a2{"A2"};

        // Start pre-amendment, with the NFT authorization gap still open:
        // this is the very flow that created the unauthorized balances of
        // XRPLF/rippled issue #5450.
        Env env{*this, testableAmendments() - fixEnforceNFTokenTrustlineV2 - fixCleanup3_5_0};
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
        env.enableFeature(fixCleanup3_5_0);
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
        // fixCleanup3_5_0 the grandfather clause no longer lets a nonzero
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
    run() override
    {
        testAccountHolds();
        testSpend();
        testDexCashOut();
        testCheckCash();
        testOwnIssuance();
        testClawbackRecreatedBalance();
        testEmptyLineReceive();
        testZeroCrossing();
        testLegacyEndToEnd();
    }
};

BEAST_DEFINE_TESTSUITE(TrustLineAuth, app, xrpl);

}  // namespace xrpl::test
