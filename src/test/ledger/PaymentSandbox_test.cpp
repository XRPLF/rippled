#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/PathSet.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/jtx_json.h>
#include <test/jtx/offer.h>
#include <test/jtx/pay.h>
#include <test/jtx/sig.h>
#include <test/jtx/sponsor.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ApplyViewImpl.h>
#include <xrpl/ledger/OwnerCounts.h>
#include <xrpl/ledger/PaymentSandbox.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/AmountConversions.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>

namespace xrpl::test {

class PaymentSandbox_test : public beast::unit_test::Suite
{
    /*
      Create paths so one path funds another path.

      Two accounts: sender and receiver.
      Two gateways: gw1 and gw2.
      Sender and receiver both have trust lines to the gateways.
      Sender has 2 gw1/USD and 4 gw2/USD.
      Sender has offer to exchange 2 gw1 for gw2 and gw2 for gw1 1-for-1.
      Paths are:
      1) GW1 -> [OB GW1/USD->GW2/USD] -> GW2
      2) GW2 -> [OB GW2/USD->GW1/USD] -> GW1

      sender pays receiver 4 USD.
      Path 1:
      1) Sender exchanges 2 GW1/USD for 2 GW2/USD
      2) Old code: the 2 GW1/USD is available to sender
         New code: the 2 GW1/USD is not available until the
         end of the transaction.
      3) Receiver gets 2 GW2/USD
      Path 2:
      1) Old code: Sender exchanges 2 GW2/USD for 2 GW1/USD
      2) Old code: Receiver get 2 GW1
      2) New code: Path is dry because sender does not have any
         GW1 to spend until the end of the transaction.
    */
    void
    testSelfFunding(FeatureBitset features)
    {
        testcase("selfFunding");

        using namespace jtx;
        Env env(*this, features);
        Account const gw1("gw1");
        Account const gw2("gw2");
        Account const snd("snd");
        Account const rcv("rcv");

        env.fund(XRP(10000), snd, rcv, gw1, gw2);

        auto const usdGw1 = gw1["USD"];
        auto const usdGw2 = gw2["USD"];

        env.trust(usdGw1(10), snd);
        env.trust(usdGw2(10), snd);
        env.trust(usdGw1(100), rcv);
        env.trust(usdGw2(100), rcv);

        env(pay(gw1, snd, usdGw1(2)));
        env(pay(gw2, snd, usdGw2(4)));

        env(offer(snd, usdGw1(2), usdGw2(2)), Txflags(tfPassive));
        env(offer(snd, usdGw2(2), usdGw1(2)), Txflags(tfPassive));

        PathSet const paths(TestPath(gw1, usdGw2, gw2), TestPath(gw2, usdGw1, gw1));

        env(pay(snd, rcv, kAny(usdGw1(4))),
            Json(paths.json()),
            Txflags(tfNoRippleDirect | tfPartialPayment));

        env.require(Balance("rcv", usdGw1(0)));
        env.require(Balance("rcv", usdGw2(2)));
    }

    void
    testSubtractCredits(FeatureBitset features)
    {
        testcase("subtractCredits");

        using namespace jtx;
        Env env(*this, features);
        Account const gw1("gw1");
        Account const gw2("gw2");
        Account const alice("alice");

        env.fund(XRP(10000), alice, gw1, gw2);

        auto j = env.app().getJournal("View");

        auto const usdGw1 = gw1["USD"];
        auto const usdGw2 = gw2["USD"];

        env.trust(usdGw1(100), alice);
        env.trust(usdGw2(100), alice);

        env(pay(gw1, alice, usdGw1(50)));
        env(pay(gw2, alice, usdGw2(50)));

        STAmount const toCredit(usdGw1(30));
        STAmount const toDebit(usdGw1(20));
        {
            // accountSend, no deferredCredits
            ApplyViewImpl av(&*env.current(), TapNone);

            auto const iss = usdGw1;
            auto const startingAmount =
                accountHolds(av, alice, iss.currency, iss.account, FreezeHandling::IgnoreFreeze, j);
            {
                auto r = accountSend(av, gw1, alice, toCredit, j);
                BEAST_EXPECT(isTesSuccess(r));
            }
            BEAST_EXPECT(
                accountHolds(
                    av, alice, iss.currency, iss.account, FreezeHandling::IgnoreFreeze, j) ==
                startingAmount + toCredit);
            {
                auto r = accountSend(av, alice, gw1, toDebit, j);
                BEAST_EXPECT(isTesSuccess(r));
            }
            BEAST_EXPECT(
                accountHolds(
                    av, alice, iss.currency, iss.account, FreezeHandling::IgnoreFreeze, j) ==
                startingAmount + toCredit - toDebit);
        }

        {
            // directSendNoFee, no deferredCredits
            ApplyViewImpl av(&*env.current(), TapNone);

            auto const iss = usdGw1;
            auto const startingAmount =
                accountHolds(av, alice, iss.currency, iss.account, FreezeHandling::IgnoreFreeze, j);

            directSendNoFee(av, gw1, alice, toCredit, true, j);
            BEAST_EXPECT(
                accountHolds(
                    av, alice, iss.currency, iss.account, FreezeHandling::IgnoreFreeze, j) ==
                startingAmount + toCredit);

            directSendNoFee(av, alice, gw1, toDebit, true, j);
            BEAST_EXPECT(
                accountHolds(
                    av, alice, iss.currency, iss.account, FreezeHandling::IgnoreFreeze, j) ==
                startingAmount + toCredit - toDebit);
        }

        {
            // accountSend, w/ deferredCredits
            ApplyViewImpl av(&*env.current(), TapNone);
            PaymentSandbox pv(&av);

            auto const iss = usdGw1;
            auto const startingAmount =
                accountHolds(pv, alice, iss.currency, iss.account, FreezeHandling::IgnoreFreeze, j);

            {
                auto r = accountSend(pv, gw1, alice, toCredit, j);
                BEAST_EXPECT(isTesSuccess(r));
            }
            BEAST_EXPECT(
                accountHolds(
                    pv, alice, iss.currency, iss.account, FreezeHandling::IgnoreFreeze, j) ==
                startingAmount);

            {
                auto r = accountSend(pv, alice, gw1, toDebit, j);
                BEAST_EXPECT(isTesSuccess(r));
            }
            BEAST_EXPECT(
                accountHolds(
                    pv, alice, iss.currency, iss.account, FreezeHandling::IgnoreFreeze, j) ==
                startingAmount - toDebit);
        }

        {
            // directSendNoFee, w/ deferredCredits
            ApplyViewImpl av(&*env.current(), TapNone);
            PaymentSandbox pv(&av);

            auto const iss = usdGw1;
            auto const startingAmount =
                accountHolds(pv, alice, iss.currency, iss.account, FreezeHandling::IgnoreFreeze, j);

            directSendNoFee(pv, gw1, alice, toCredit, true, j);
            BEAST_EXPECT(
                accountHolds(
                    pv, alice, iss.currency, iss.account, FreezeHandling::IgnoreFreeze, j) ==
                startingAmount);
        }

        {
            // redeemIOU, w/ deferredCredits
            ApplyViewImpl av(&*env.current(), TapNone);
            PaymentSandbox pv(&av);

            auto const iss = usdGw1;
            auto const startingAmount =
                accountHolds(pv, alice, iss.currency, iss.account, FreezeHandling::IgnoreFreeze, j);

            BEAST_EXPECT(redeemIOU(pv, alice, toDebit, iss, j) == tesSUCCESS);
            BEAST_EXPECT(
                accountHolds(
                    pv, alice, iss.currency, iss.account, FreezeHandling::IgnoreFreeze, j) ==
                startingAmount - toDebit);
        }

        {
            // issueIOU, w/ deferredCredits
            ApplyViewImpl av(&*env.current(), TapNone);
            PaymentSandbox pv(&av);

            auto const iss = usdGw1;
            auto const startingAmount =
                accountHolds(pv, alice, iss.currency, iss.account, FreezeHandling::IgnoreFreeze, j);

            BEAST_EXPECT(issueIOU(pv, alice, toCredit, iss, {}, j) == tesSUCCESS);
            BEAST_EXPECT(
                accountHolds(
                    pv, alice, iss.currency, iss.account, FreezeHandling::IgnoreFreeze, j) ==
                startingAmount);
        }

        {
            // accountSend, w/ deferredCredits and stacked views
            ApplyViewImpl av(&*env.current(), TapNone);
            PaymentSandbox pv(&av);

            auto const iss = usdGw1;
            auto const startingAmount =
                accountHolds(pv, alice, iss.currency, iss.account, FreezeHandling::IgnoreFreeze, j);

            {
                auto r = accountSend(pv, gw1, alice, toCredit, j);
                BEAST_EXPECT(isTesSuccess(r));
            }
            BEAST_EXPECT(
                accountHolds(
                    pv, alice, iss.currency, iss.account, FreezeHandling::IgnoreFreeze, j) ==
                startingAmount);

            {
                PaymentSandbox pv2(&pv);
                BEAST_EXPECT(
                    accountHolds(
                        pv2, alice, iss.currency, iss.account, FreezeHandling::IgnoreFreeze, j) ==
                    startingAmount);
                {
                    auto r = accountSend(pv2, gw1, alice, toCredit, j);
                    BEAST_EXPECT(isTesSuccess(r));
                }
                BEAST_EXPECT(
                    accountHolds(
                        pv2, alice, iss.currency, iss.account, FreezeHandling::IgnoreFreeze, j) ==
                    startingAmount);
            }

            {
                auto r = accountSend(pv, alice, gw1, toDebit, j);
                BEAST_EXPECT(isTesSuccess(r));
            }
            BEAST_EXPECT(
                accountHolds(
                    pv, alice, iss.currency, iss.account, FreezeHandling::IgnoreFreeze, j) ==
                startingAmount - toDebit);
        }
    }

    void
    testTinyBalance(FeatureBitset features)
    {
        testcase("Tiny balance");

        // Add and subtract a huge credit from a tiny balance, expect the tiny
        // balance back. Numerical stability problems could cause the balance to
        // be zero.

        using namespace jtx;

        Env const env(*this, features);

        Account const gw("gw");
        Account const alice("alice");
        auto const usd = gw["USD"];

        auto const issue = usd;
        STAmount const tinyAmt(
            issue, STAmount::kMinValue, STAmount::kMinOffset + 1, false, STAmount::Unchecked{});
        STAmount const hugeAmt(
            issue, STAmount::kMaxValue, STAmount::kMaxOffset - 1, false, STAmount::Unchecked{});

        ApplyViewImpl av(&*env.current(), TapNone);
        PaymentSandbox pv(&av);
        pv.creditHookIOU(gw, alice, hugeAmt, -tinyAmt);
        BEAST_EXPECT(pv.balanceHookIOU(alice, gw, hugeAmt) == tinyAmt);
    }

    void
    testReserve(FeatureBitset features)
    {
        testcase("Reserve");
        using namespace jtx;

        auto accountFundsXRP =
            [](ReadView const& view, AccountID const& id, beast::Journal j) -> XRPAmount {
            return toAmount<XRPAmount>(accountHolds(
                view, id, xrpCurrency(), xrpAccount(), FreezeHandling::ZeroIfFrozen, j));
        };

        auto reserve = [](jtx::Env& env, std::uint32_t count) -> XRPAmount {
            return env.current()->fees().accountReserve(count, 1);
        };

        Env env(*this, features);

        Account const alice("alice");
        env.fund(reserve(env, 1), alice);

        env.close();
        ApplyViewImpl av(&*env.current(), TapNone);
        PaymentSandbox sb(&av);
        {
            // Send alice an amount and spend it. The deferredCredits will cause
            // her balance to drop below the reserve. Make sure her funds are
            // zero (there was a bug that caused her funds to become negative).

            {
                auto r = accountSend(sb, xrpAccount(), alice, XRP(100), env.journal);
                BEAST_EXPECT(isTesSuccess(r));
            }
            {
                auto r = accountSend(sb, alice, xrpAccount(), XRP(100), env.journal);
                BEAST_EXPECT(isTesSuccess(r));
            }
            BEAST_EXPECT(accountFundsXRP(sb, alice, env.journal) == beast::kZero);
        }
    }

    void
    testBalanceHook(FeatureBitset features)
    {
        // Make sure the Issue::Account returned by
        // PaymentSandbox::balanceHookIOU is correct.
        testcase("balanceHook");

        using namespace jtx;
        Env const env(*this, features);

        Account const gw("gw");
        auto const usd = gw["USD"];
        Account const alice("alice");

        ApplyViewImpl av(&*env.current(), TapNone);
        PaymentSandbox sb(&av);

        // The currency we pass for the last argument mimics the currency that
        // is typically passed to creditHookIOU, since it comes from a trust
        // line.
        Issue tlIssue = noIssue();
        tlIssue.currency = usd.currency;

        sb.creditHookIOU(gw.id(), alice.id(), {usd, 400}, {tlIssue, 600});
        sb.creditHookIOU(gw.id(), alice.id(), {usd, 100}, {tlIssue, 600});

        // Expect that the STAmount issuer returned by balanceHookIOU() is correct.
        STAmount const balance = sb.balanceHookIOU(gw.id(), alice.id(), {usd, 600});
        BEAST_EXPECT(balance.getIssuer() == usd.account.id());
    }

    void
    testOwnerCountHook(FeatureBitset features)
    {
        // Test that PaymentSandbox::adjustOwnerCountHook and ownerCountHook
        // correctly track and return the maximum owner counts during a payment.
        testcase("ownerCountHook");

        using namespace jtx;
        Env env(*this, features);
        Account const alice("alice");
        Account const sponsor("sponsor");

        env.fund(XRP(10000), alice, sponsor);
        env.close();

        ApplyViewImpl av(&*env.current(), TapNone);
        PaymentSandbox sb(&av);

        // Test basic owner count hook without sponsor
        {
            auto const aliceSle = sb.peek(keylet::account(alice));
            BEAST_EXPECT(aliceSle);

            OwnerCounts const initial(aliceSle);
            OwnerCounts updated = initial;
            updated.owner = initial.owner + 2;

            // Simulate adjusting owner count
            sb.adjustOwnerCountHook(alice, initial, updated);

            // ownerCountHook should return the max value
            OwnerCounts const retrieved = sb.ownerCountHook(alice, initial);
            BEAST_EXPECT(retrieved.owner == updated.owner);
            BEAST_EXPECT(retrieved.sponsored == updated.sponsored);
            BEAST_EXPECT(retrieved.sponsoring == updated.sponsoring);
        }

        // Test owner count hook with sponsor-related counts
        {
            auto const sponsorSle = sb.peek(keylet::account(sponsor));
            BEAST_EXPECT(sponsorSle);

            OwnerCounts const sponsorInitial(sponsorSle);
            OwnerCounts sponsorUpdated = sponsorInitial;
            sponsorUpdated.owner = sponsorInitial.owner + 1;
            sponsorUpdated.sponsoring = sponsorInitial.sponsoring + 1;

            sb.adjustOwnerCountHook(sponsor, sponsorInitial, sponsorUpdated);

            OwnerCounts const sponsorRetrieved = sb.ownerCountHook(sponsor, sponsorInitial);
            BEAST_EXPECT(sponsorRetrieved.owner == sponsorUpdated.owner);
            BEAST_EXPECT(sponsorRetrieved.sponsoring == sponsorUpdated.sponsoring);
        }

        // Test with stacked PaymentSandboxes
        {
            PaymentSandbox sb2(&sb);

            auto const aliceSle = sb2.peek(keylet::account(alice));
            OwnerCounts const current(aliceSle);
            OwnerCounts further = current;
            further.owner = current.owner + 3;

            sb2.adjustOwnerCountHook(alice, current, further);

            // The nested sandbox should see the max from both levels
            OwnerCounts const retrieved = sb2.ownerCountHook(alice, OwnerCounts());
            BEAST_EXPECT(retrieved.owner >= further.owner);
        }

        // Test that max logic works correctly
        {
            auto const aliceSle = sb.peek(keylet::account(alice));
            OwnerCounts const current(aliceSle);
            OwnerCounts lower = current;
            lower.owner = (current.owner > 0) ? current.owner - 1 : 0;

            // Adjusting to a lower value
            sb.adjustOwnerCountHook(alice, current, lower);

            // Should still return the higher value seen previously
            OwnerCounts const retrieved = sb.ownerCountHook(alice, OwnerCounts());
            BEAST_EXPECT(retrieved.owner >= lower.owner);
        }
    }

    void
    testOwnerCountWithTransaction(FeatureBitset features)
    {
        // Test that owner count hooks work correctly during actual transactions.
        // This verifies that when transactions modify owner counts (by creating
        // or deleting ledger objects), the hooks properly track these changes.
        testcase(
            std::string("ownerCountWithTransaction") +
            (features[featureSponsor] ? " with sponsor" : " without sponsor"));

        using namespace jtx;

        auto reserve = [](jtx::Env& env, std::uint32_t count) -> XRPAmount {
            return env.current()->fees().accountReserve(count, 1);
        };

        Env env(*this, features);
        Account const gw("gw");
        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");

        auto const usd = gw["USD"];

        // Fund accounts. Alice starts with exactly enough for base reserve + 2 objects
        env.fund(XRP(10000), gw, bob, sponsor);
        env.fund(reserve(env, 3) + XRP(100), alice);  // Base + 2 objects + extra for fees
        env.close();

        // Verify initial state - no owner count
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(ownerCount(env, bob) == 0);

        // Create a trust line - this increases owner count
        env(trust(alice, usd(1000)));
        env.close();

        // alice now has 1 object (owner count = 1)
        BEAST_EXPECT(ownerCount(env, alice) == 1);

        // Create an offer - this further increases owner count
        env(trust(bob, usd(1000)));
        env(pay(gw, alice, usd(100)));
        env.close();

        auto const aliceOfferSeq = env.seq(alice);  // Capture the sequence before creating offer
        env(offer(alice, usd(50), XRP(50)));
        env.close();

        // alice now has 2 objects (trust line + offer)
        BEAST_EXPECT(ownerCount(env, alice) == 2);

        // If sponsor feature is enabled, test sponsorship transfer
        if (features[featureSponsor])
        {
            auto const trustId = keylet::trustLine(alice, gw, usd.currency);
            BEAST_EXPECT(env.le(trustId));

            // Transfer sponsorship - sponsor now sponsors alice's trust line
            env(sponsor::transfer(alice, tfSponsorshipCreate, trustId.key),
                sponsor::As(sponsor, spfSponsorReserve),
                Sig(sfSponsorSignature, sponsor));
            env.close();

            // alice still has 2 objects but 1 is sponsored
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            // sponsor's sponsoring count should increase
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);
        }

        // Verify alice's available balance respects the reserve
        auto const aliceSle = env.le(keylet::account(alice));
        BEAST_EXPECT(aliceSle);

        auto const aliceBalance = aliceSle->getFieldAmount(sfBalance);
        // With sponsor, 1 object is sponsored so only 1 counts for reserve
        auto const aliceReserve = reserve(env, features[featureSponsor] ? 1 : 2);

        // alice should have limited available balance after accounting for reserve
        auto const available = aliceBalance.xrp() - aliceReserve;
        if (features[featureSponsor])
        {
            // With sponsor, alice has more available (1 sponsored object = less reserve)
            BEAST_EXPECT(available > XRP(150));
        }
        else
        {
            BEAST_EXPECT(available < XRP(150));  // Most of the balance is in reserve
        }

        // Try to send nearly all balance - should fail due to reserve in both cases
        auto const tooMuch = aliceBalance.xrp() - XRP(1);
        env(pay(alice, bob, tooMuch), Ter(tecUNFUNDED_PAYMENT));
        env.close();

        // Verify owner count hasn't changed
        BEAST_EXPECT(ownerCount(env, alice) == 2);

        // Cancel the offer - this decreases owner count
        env(offerCancel(alice, aliceOfferSeq));
        env.close();

        // alice now has 1 object (just the trust line)
        BEAST_EXPECT(ownerCount(env, alice) == 1);

        if (features[featureSponsor])
        {
            // Verify sponsored count stayed the same (trust line is still sponsored)
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);
        }

        // Now alice should have more available balance (less reserve needed)
        auto const aliceSle2 = env.le(keylet::account(alice));
        auto const aliceBalance2 = aliceSle2->getFieldAmount(sfBalance);
        // With sponsor, trust line is still sponsored so 0 objects for reserve
        // Without sponsor, 1 object for reserve
        auto const aliceReserve2 = reserve(env, features[featureSponsor] ? 0 : 1);
        auto const available2 = aliceBalance2.xrp() - aliceReserve2;

        // available2 should be greater than available (less reserve needed)
        BEAST_EXPECT(available2 > available);
    }

public:
    void
    run() override
    {
        auto testAll = [this](FeatureBitset features) {
            testSelfFunding(features);
            testSubtractCredits(features);
            testTinyBalance(features);
            testReserve(features);
            testBalanceHook(features);
            testOwnerCountHook(features);
        };
        using namespace jtx;
        auto const sa = testableAmendments();
        testAll(sa - featurePermissionedDEX);
        testAll(sa);

        // Test owner count with transactions
        testOwnerCountWithTransaction(sa - featureSponsor);
        testOwnerCountWithTransaction(sa);
    }
};

BEAST_DEFINE_TESTSUITE(PaymentSandbox, ledger, xrpl);

}  // namespace xrpl::test
