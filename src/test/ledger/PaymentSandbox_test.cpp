#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/PathSet.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/jtx_json.h>
#include <test/jtx/offer.h>
#include <test/jtx/pay.h>
#include <test/jtx/txflags.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ApplyViewImpl.h>
#include <xrpl/ledger/PaymentSandbox.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/AmountConversions.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Issue.h>
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

            BEAST_EXPECT(issueIOU(pv, alice, toCredit, iss, j) == tesSUCCESS);
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
            return env.current()->fees().accountReserve(count);
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
        };
        using namespace jtx;
        auto const sa = testableAmendments();
        testAll(sa - featurePermissionedDEX);
        testAll(sa);
    }
};

BEAST_DEFINE_TESTSUITE(PaymentSandbox, ledger, xrpl);

}  // namespace xrpl::test
