#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/PathSet.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/WSClient.h>
#include <test/jtx/acctdelete.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/jtx_json.h>
#include <test/jtx/noop.h>
#include <test/jtx/offer.h>
#include <test/jtx/owners.h>
#include <test/jtx/paths.h>
#include <test/jtx/pay.h>
#include <test/jtx/quality.h>
#include <test/jtx/rate.h>
#include <test/jtx/require.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/tags.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::test {

class OfferBaseUtil_test : public beast::unit_test::Suite
{
    static XRPAmount
    reserve(jtx::Env& env, std::uint32_t count)
    {
        return env.current()->fees().accountReserve(count);
    }

    static std::uint32_t
    lastClose(jtx::Env& env)
    {
        return env.current()->header().parentCloseTime.time_since_epoch().count();
    }

    static auto
    ledgerEntryOffer(jtx::Env& env, jtx::Account const& acct, std::uint32_t offerSeq)
    {
        json::Value jvParams;
        jvParams[jss::offer][jss::account] = acct.human();
        jvParams[jss::offer][jss::seq] = offerSeq;
        return env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
    }

    static auto
    getBookOffers(jtx::Env& env, Issue const& takerPays, Issue const& takerGets)
    {
        json::Value jvbp;
        jvbp[jss::ledger_index] = "current";
        jvbp[jss::taker_pays][jss::currency] = to_string(takerPays.currency);
        jvbp[jss::taker_pays][jss::issuer] = to_string(takerPays.account);
        jvbp[jss::taker_gets][jss::currency] = to_string(takerGets.currency);
        jvbp[jss::taker_gets][jss::issuer] = to_string(takerGets.account);
        return env.rpc("json", "book_offers", to_string(jvbp))[jss::result];
    }

public:
    void
    testRmFundedOffer(FeatureBitset features)
    {
        testcase("Incorrect Removal of Funded Offers");

        // We need at least two paths. One at good quality and one at bad
        // quality.  The bad quality path needs two offer books in a row.
        // Each offer book should have two offers at the same quality, the
        // offers should be completely consumed, and the payment should
        // should require both offers to be satisfied. The first offer must
        // be "taker gets" XRP. Old, broken would remove the first
        // "taker gets" xrp offer, even though the offer is still funded and
        // not used for the payment.

        using namespace jtx;
        Env env{*this, features};

        auto const gw = Account{"gateway"};
        auto const usd = gw["USD"];
        auto const btc = gw["BTC"];
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};

        env.fund(XRP(10000), alice, bob, carol, gw);
        env.close();
        env.trust(usd(1000), alice, bob, carol);
        env.trust(btc(1000), alice, bob, carol);

        env(pay(gw, alice, btc(1000)));

        env(pay(gw, carol, usd(1000)));
        env(pay(gw, carol, btc(1000)));

        // Must be two offers at the same quality
        // "taker gets" must be XRP
        // (Different amounts so I can distinguish the offers)
        env(offer(carol, btc(49), XRP(49)));
        env(offer(carol, btc(51), XRP(51)));

        // Offers for the poor quality path
        // Must be two offers at the same quality
        env(offer(carol, XRP(50), usd(50)));
        env(offer(carol, XRP(50), usd(50)));

        // Offers for the good quality path
        env(offer(carol, btc(1), usd(100)));

        PathSet const paths(TestPath(XRP, usd), TestPath(usd));

        env(pay(alice, bob, usd(100)),
            Json(paths.json()),
            Sendmax(btc(1000)),
            Txflags(tfPartialPayment));

        env.require(Balance(bob, usd(100)));
        BEAST_EXPECT(
            !isOffer(env, carol, btc(1), usd(100)) && isOffer(env, carol, btc(49), XRP(49)));
    }

    void
    testCanceledOffer(FeatureBitset features)
    {
        testcase("Removing Canceled Offers");

        using namespace jtx;
        Env env{*this, features};

        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const usd = gw["USD"];

        env.fund(XRP(10000), alice, gw);
        env.close();
        env.trust(usd(100), alice);
        env.close();

        env(pay(gw, alice, usd(50)));
        env.close();

        auto const offer1Seq = env.seq(alice);

        env(offer(alice, XRP(500), usd(100)), Require(offers(alice, 1)));
        env.close();

        BEAST_EXPECT(isOffer(env, alice, XRP(500), usd(100)));

        // cancel the offer above and replace it with a new offer
        auto const offer2Seq = env.seq(alice);

        env(offer(alice, XRP(300), usd(100)),
            Json(jss::OfferSequence, offer1Seq),
            Require(offers(alice, 1)));
        env.close();

        BEAST_EXPECT(
            isOffer(env, alice, XRP(300), usd(100)) && !isOffer(env, alice, XRP(500), usd(100)));

        // Test canceling non-existent offer.
        //      auto const offer3Seq = env.seq (alice);

        env(offer(alice, XRP(400), usd(200)),
            Json(jss::OfferSequence, offer1Seq),
            Require(offers(alice, 2)));
        env.close();

        BEAST_EXPECT(
            isOffer(env, alice, XRP(300), usd(100)) && isOffer(env, alice, XRP(400), usd(200)));

        // Test cancellation now with OfferCancel tx
        auto const offer4Seq = env.seq(alice);
        env(offer(alice, XRP(222), usd(111)), Require(offers(alice, 3)));
        env.close();

        BEAST_EXPECT(isOffer(env, alice, XRP(222), usd(111)));
        env(offerCancel(alice, offer4Seq));
        env.close();
        BEAST_EXPECT(env.seq(alice) == offer4Seq + 2);

        BEAST_EXPECT(!isOffer(env, alice, XRP(222), usd(111)));

        // Create an offer that both fails with a tecEXPIRED code and removes
        // an offer.  Show that the attempt to remove the offer fails.
        env.require(offers(alice, 2));

        env(offer(alice, XRP(5), usd(2)),
            Json(sfExpiration.fieldName, lastClose(env)),
            Json(jss::OfferSequence, offer2Seq),
            Ter(tecEXPIRED));
        env.close();

        env.require(offers(alice, 2));
        BEAST_EXPECT(isOffer(env, alice, XRP(300), usd(100)));  // offer2
        BEAST_EXPECT(!isOffer(env, alice, XRP(5), usd(2)));     // expired
    }

    void
    testTinyPayment(FeatureBitset features)
    {
        testcase("Tiny payments");

        // Regression test for tiny payments
        using namespace jtx;
        using namespace std::chrono_literals;
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const carol = Account{"carol"};
        auto const gw = Account{"gw"};

        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        Env env{*this, features};

        env.fund(XRP(10000), alice, bob, carol, gw);
        env.close();
        env.trust(usd(1000), alice, bob, carol);
        env.trust(eur(1000), alice, bob, carol);
        env(pay(gw, alice, usd(100)));
        env(pay(gw, carol, eur(100)));

        // Create more offers than the loop max count in DeliverNodeReverse
        // Note: the DeliverNodeReverse code has been removed; however since
        // this is a regression test the original test is being left as-is for
        // now.
        for (int i = 0; i < 101; ++i)
            env(offer(carol, usd(1), eur(2)));

        env(pay(alice, bob, eur(kEpsilon)), Path(~eur), Sendmax(usd(100)));
    }

    void
    testXRPTinyPayment(FeatureBitset features)
    {
        testcase("XRP Tiny payments");

        // Regression test for tiny xrp payments
        // In some cases, when the payment code calculates
        // the amount of xrp needed as input to an xrp->iou offer
        // it would incorrectly round the amount to zero (even when
        // round-up was set to true).
        // The bug would cause funded offers to be incorrectly removed
        // because the code thought they were unfunded.
        // The conditions to trigger the bug are:
        // 1) When we calculate the amount of input xrp needed for an offer
        //    from xrp->iou, the amount is less than 1 drop (after rounding
        //    up the float representation).
        // 2) There is another offer in the same book with a quality
        //    sufficiently bad that when calculating the input amount
        //    needed the amount is not set to zero.

        using namespace jtx;
        using namespace std::chrono_literals;
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const carol = Account{"carol"};
        auto const dan = Account{"dan"};
        auto const erin = Account{"erin"};
        auto const gw = Account{"gw"};

        auto const usd = gw["USD"];
        Env env{*this, features};

        env.fund(XRP(10000), alice, bob, carol, dan, erin, gw);
        env.close();
        env.trust(usd(1000), alice, bob, carol, dan, erin);
        env.close();
        env(pay(gw, carol, usd(0.99999)));
        env(pay(gw, dan, usd(1)));
        env(pay(gw, erin, usd(1)));
        env.close();

        // Carol doesn't quite have enough funds for this offer
        // The amount left after this offer is taken will cause
        // STAmount to incorrectly round to zero when the next offer
        // (at a good quality) is considered. (when the now removed
        // stAmountCalcSwitchover2 patch was inactive)
        env(offer(carol, drops(1), usd(0.99999)));
        // Offer at a quality poor enough so when the input xrp is
        // calculated  in the reverse pass, the amount is not zero.
        env(offer(dan, XRP(100), usd(1)));

        env.close();
        // This is the funded offer that will be incorrectly removed.
        // It is considered after the offer from carol, which leaves a
        // tiny amount left to pay. When calculating the amount of xrp
        // needed for this offer, it will incorrectly compute zero in both
        // the forward and reverse passes (when the now removed
        // stAmountCalcSwitchover2 was inactive.)
        env(offer(erin, drops(2), usd(1)));

        env(pay(alice, bob, usd(1)),
            Path(~usd),
            Sendmax(XRP(102)),
            Txflags(tfNoRippleDirect | tfPartialPayment));

        env.require(offers(carol, 0), offers(dan, 1));

        // offer was correctly consumed. There is still some
        // liquidity left on that offer.
        env.require(Balance(erin, usd(0.99999)), offers(erin, 1));
    }

    void
    testRmSmallIncreasedQOffersXRP(FeatureBitset features)
    {
        testcase("Rm small increased q offers XRP");

        // Carol places an offer, but cannot fully fund the offer. When her
        // funding is taken into account, the offer's quality drops below its
        // initial quality and has an input amount of 1 drop. This is removed as
        // an offer that may block offer books.

        using namespace jtx;
        using namespace std::chrono_literals;
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const carol = Account{"carol"};
        auto const gw = Account{"gw"};

        auto const usd = gw["USD"];

        // Test offer crossing
        for (auto crossBothOffers : {false, true})
        {
            Env env{*this, features};

            env.fund(XRP(10000), alice, bob, carol, gw);
            env.close();
            env.trust(usd(1000), alice, bob, carol);
            // underfund carol's offer
            auto initialCarolUSD = usd(0.499);
            env(pay(gw, carol, initialCarolUSD));
            env(pay(gw, bob, usd(100)));
            env.close();
            // This offer is underfunded
            env(offer(carol, drops(1), usd(1)));
            env.close();
            // offer at a lower quality
            env(offer(bob, drops(2), usd(1), tfPassive));
            env.close();
            env.require(offers(bob, 1), offers(carol, 1));

            // alice places an offer that crosses carol's; depending on
            // "crossBothOffers" it may cross bob's as well
            auto aliceTakerGets = crossBothOffers ? drops(2) : drops(1);
            env(offer(alice, usd(1), aliceTakerGets));
            env.close();

            env.require(
                offers(carol, 0),
                Balance(
                    carol,
                    initialCarolUSD));  // offer is removed but not taken
            if (crossBothOffers)
            {
                env.require(offers(alice, 0), Balance(alice, usd(1)));  // alice's offer is crossed
            }
            else
            {
                env.require(
                    offers(alice, 1), Balance(alice, usd(0)));  // alice's offer is not crossed
            }
        }

        // Test payments
        for (auto partialPayment : {false, true})
        {
            Env env{*this, features};

            env.fund(XRP(10000), alice, bob, carol, gw);
            env.close();
            env.trust(usd(1000), alice, bob, carol);
            env.close();
            auto const initialCarolUSD = usd(0.999);
            env(pay(gw, carol, initialCarolUSD));
            env.close();
            env(pay(gw, bob, usd(100)));
            env.close();
            env(offer(carol, drops(1), usd(1)));
            env.close();
            env(offer(bob, drops(2), usd(2), tfPassive));
            env.close();
            env.require(offers(bob, 1), offers(carol, 1));

            std::uint32_t const flags =
                partialPayment ? (tfNoRippleDirect | tfPartialPayment) : tfNoRippleDirect;

            TER const expectedTer = partialPayment ? TER{tesSUCCESS} : TER{tecPATH_PARTIAL};

            env(pay(alice, bob, usd(5)),
                Path(~usd),
                Sendmax(XRP(1)),
                Txflags(flags),
                Ter(expectedTer));
            env.close();

            if (isTesSuccess(expectedTer))
            {
                env.require(offers(carol, 0));
                env.require(Balance(carol,
                                    initialCarolUSD));  // offer is removed but not taken
            }
            else
            {
                // TODO: Offers are not removed when payments fail
                // If that is addressed, the test should show that carol's
                // offer is removed but not taken, as in the other branch of
                // this if statement
            }
        }
    }

    void
    testRmSmallIncreasedQOffersIOU(FeatureBitset features)
    {
        testcase("Rm small increased q offers IOU");

        // Carol places an offer, but cannot fully fund the offer. When her
        // funding is taken into account, the offer's quality drops below its
        // initial quality and has an input amount of 1 drop. This is removed as
        // an offer that may block offer books.

        using namespace jtx;
        using namespace std::chrono_literals;
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const carol = Account{"carol"};
        auto const gw = Account{"gw"};

        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        auto tinyAmount = [&](IOU const& iou) -> PrettyAmount {
            STAmount const amt(
                iou,
                /*mantissa*/ 1,
                /*exponent*/ -81);
            return PrettyAmount(amt, iou.account.name());
        };

        // Test offer crossing
        for (auto crossBothOffers : {false, true})
        {
            Env env{*this, features};

            env.fund(XRP(10000), alice, bob, carol, gw);
            env.close();
            env.trust(usd(1000), alice, bob, carol);
            env.trust(eur(1000), alice, bob, carol);
            // underfund carol's offer
            auto initialCarolUSD = tinyAmount(usd);
            env(pay(gw, carol, initialCarolUSD));
            env(pay(gw, bob, usd(100)));
            env(pay(gw, alice, eur(100)));
            env.close();
            // This offer is underfunded
            env(offer(carol, eur(1), usd(10)));
            env.close();
            // offer at a lower quality
            env(offer(bob, eur(1), usd(5), tfPassive));
            env.close();
            env.require(offers(bob, 1), offers(carol, 1));

            // alice places an offer that crosses carol's; depending on
            // "crossBothOffers" it may cross bob's as well
            // Whatever
            auto aliceTakerGets = crossBothOffers ? eur(0.2) : eur(0.1);
            env(offer(alice, usd(1), aliceTakerGets));
            env.close();

            env.require(
                offers(carol, 0),
                Balance(
                    carol,
                    initialCarolUSD));  // offer is removed but not taken
            if (crossBothOffers)
            {
                env.require(offers(alice, 0), Balance(alice, usd(1)));  // alice's offer is crossed
            }
            else
            {
                env.require(
                    offers(alice, 1), Balance(alice, usd(0)));  // alice's offer is not crossed
            }
        }

        // Test payments
        for (auto partialPayment : {false, true})
        {
            Env env{*this, features};

            env.fund(XRP(10000), alice, bob, carol, gw);
            env.close();
            env.trust(usd(1000), alice, bob, carol);
            env.trust(eur(1000), alice, bob, carol);
            env.close();
            // underfund carol's offer
            auto const initialCarolUSD = tinyAmount(usd);
            env(pay(gw, carol, initialCarolUSD));
            env(pay(gw, bob, usd(100)));
            env(pay(gw, alice, eur(100)));
            env.close();
            // This offer is underfunded
            env(offer(carol, eur(1), usd(2)));
            env.close();
            env(offer(bob, eur(2), usd(4), tfPassive));
            env.close();
            env.require(offers(bob, 1), offers(carol, 1));

            std::uint32_t const flags =
                partialPayment ? (tfNoRippleDirect | tfPartialPayment) : tfNoRippleDirect;

            TER const expectedTer = partialPayment ? TER{tesSUCCESS} : TER{tecPATH_PARTIAL};

            env(pay(alice, bob, usd(5)),
                Path(~usd),
                Sendmax(eur(10)),
                Txflags(flags),
                Ter(expectedTer));
            env.close();

            if (isTesSuccess(expectedTer))
            {
                env.require(offers(carol, 0));
                env.require(Balance(carol,
                                    initialCarolUSD));  // offer is removed but not taken
            }
            else
            {
                // TODO: Offers are not removed when payments fail
                // If that is addressed, the test should show that carol's
                // offer is removed but not taken, as in the other branch of
                // this if statement
            }
        }
    }

    void
    testEnforceNoRipple(FeatureBitset features)
    {
        testcase("Enforce No Ripple");

        using namespace jtx;

        auto const gw = Account{"gateway"};
        auto const usd = gw["USD"];
        auto const btc = gw["BTC"];
        auto const eur = gw["EUR"];
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const dan{"dan"};

        {
            // No ripple with an implied account step after an offer
            Env env{*this, features};

            auto const gw1 = Account{"gw1"};
            auto const usD1 = gw1["USD"];
            auto const gw2 = Account{"gw2"};
            auto const usD2 = gw2["USD"];

            env.fund(XRP(10000), alice, noripple(bob), carol, dan, gw1, gw2);
            env.close();
            env.trust(usD1(1000), alice, carol, dan);
            env(trust(bob, usD1(1000), tfSetNoRipple));
            env.trust(usD2(1000), alice, carol, dan);
            env(trust(bob, usD2(1000), tfSetNoRipple));

            env(pay(gw1, dan, usD1(50)));
            env(pay(gw1, bob, usD1(50)));
            env(pay(gw2, bob, usD2(50)));

            env(offer(dan, XRP(50), usD1(50)));

            env(pay(alice, carol, usD2(50)),
                Path(~usD1, bob),
                Sendmax(XRP(50)),
                Txflags(tfNoRippleDirect),
                Ter(tecPATH_DRY));
        }
        {
            // Make sure payment works with default flags
            Env env{*this, features};

            auto const gw1 = Account{"gw1"};
            auto const usD1 = gw1["USD"];
            auto const gw2 = Account{"gw2"};
            auto const usD2 = gw2["USD"];

            env.fund(XRP(10000), alice, bob, carol, dan, gw1, gw2);
            env.close();
            env.trust(usD1(1000), alice, bob, carol, dan);
            env.trust(usD2(1000), alice, bob, carol, dan);

            env(pay(gw1, dan, usD1(50)));
            env(pay(gw1, bob, usD1(50)));
            env(pay(gw2, bob, usD2(50)));

            env(offer(dan, XRP(50), usD1(50)));

            env(pay(alice, carol, usD2(50)),
                Path(~usD1, bob),
                Sendmax(XRP(50)),
                Txflags(tfNoRippleDirect));

            env.require(Balance(alice, xrpMinusFee(env, 10000 - 50)));
            env.require(Balance(bob, usD1(100)));
            env.require(Balance(bob, usD2(0)));
            env.require(Balance(carol, usD2(50)));
        }
    }

    void
    testInsufficientReserve(FeatureBitset features)
    {
        testcase("Insufficient Reserve");

        // If an account places an offer and its balance
        // *before* the transaction began isn't high enough
        // to meet the reserve *after* the transaction runs,
        // then no offer should go on the books but if the
        // offer partially or fully crossed the tx succeeds.

        using namespace jtx;

        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const carol = Account{"carol"};
        auto const usd = gw["USD"];

        auto const usdOffer = usd(1000);
        auto const xrpOffer = XRP(1000);

        // No crossing:
        {
            Env env{*this, features};

            env.fund(XRP(1000000), gw);

            auto const f = env.current()->fees().base;
            auto const r = reserve(env, 0);

            env.fund(r + f, alice);

            env(trust(alice, usdOffer), Ter(tesSUCCESS));
            env(pay(gw, alice, usdOffer), Ter(tesSUCCESS));
            env(offer(alice, xrpOffer, usdOffer), Ter(tecINSUF_RESERVE_OFFER));

            env.require(Balance(alice, r - f), Owners(alice, 1));
        }

        // Partial cross:
        {
            Env env{*this, features};

            env.fund(XRP(1000000), gw);

            auto const f = env.current()->fees().base;
            auto const r = reserve(env, 0);

            auto const usdOffer2 = usd(500);
            auto const xrpOffer2 = XRP(500);

            env.fund(r + f + xrpOffer, bob);

            env(offer(bob, usdOffer2, xrpOffer2), Ter(tesSUCCESS));
            env.fund(r + f, alice);

            env(trust(alice, usdOffer), Ter(tesSUCCESS));
            env(pay(gw, alice, usdOffer), Ter(tesSUCCESS));
            env(offer(alice, xrpOffer, usdOffer), Ter(tesSUCCESS));

            env.require(
                Balance(alice, r - f + xrpOffer2),
                Balance(alice, usdOffer2),
                Owners(alice, 1),
                Balance(bob, r + xrpOffer2),
                Balance(bob, usdOffer2),
                Owners(bob, 1));
        }

        // Account has enough reserve as is, but not enough
        // if an offer were added. Attempt to sell IOUs to
        // buy XRP. If it fully crosses, we succeed.
        {
            Env env{*this, features};

            env.fund(XRP(1000000), gw);

            auto const f = env.current()->fees().base;
            auto const r = reserve(env, 0);

            auto const usdOffer2 = usd(500);
            auto const xrpOffer2 = XRP(500);

            env.fund(r + f + xrpOffer, bob, carol);

            env(offer(bob, usdOffer2, xrpOffer2), Ter(tesSUCCESS));
            env(offer(carol, usdOffer, xrpOffer), Ter(tesSUCCESS));

            env.fund(r + f, alice);

            env(trust(alice, usdOffer), Ter(tesSUCCESS));
            env(pay(gw, alice, usdOffer), Ter(tesSUCCESS));
            env(offer(alice, xrpOffer, usdOffer), Ter(tesSUCCESS));

            env.require(
                Balance(alice, r - f + xrpOffer),
                Balance(alice, usd(0)),
                Owners(alice, 1),
                Balance(bob, r + xrpOffer2),
                Balance(bob, usdOffer2),
                Owners(bob, 1),
                Balance(carol, r + xrpOffer2),
                Balance(carol, usdOffer2),
                Owners(carol, 2));
        }
    }

    // Helper function that returns the Offers on an account.
    static std::vector<std::shared_ptr<SLE const>>
    offersOnAccount(jtx::Env& env, jtx::Account const& account)
    {
        std::vector<std::shared_ptr<SLE const>> result;
        forEachItem(*env.current(), account, [&result](std::shared_ptr<SLE const> const& sle) {
            if (sle->getType() == ltOFFER)
                result.push_back(sle);
        });
        return result;
    }

    void
    testFillModes(FeatureBitset features)
    {
        testcase("Fill Modes");

        using namespace jtx;

        auto const startBalance = XRP(1000000);
        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const usd = gw["USD"];

        // Fill or Kill - unless we fully cross, just charge a fee and don't
        // place the offer on the books.  But also clean up expired offers
        // that are discovered along the way.
        {
            Env env{*this, features};

            auto const f = env.current()->fees().base;

            env.fund(startBalance, gw, alice, bob);
            env.close();

            // bob creates an offer that expires before the next ledger close.
            env(offer(bob, usd(500), XRP(500)),
                Json(sfExpiration.fieldName, lastClose(env) + 1),
                Ter(tesSUCCESS));

            // The offer expires (it's not removed yet).
            env.close();
            env.require(Owners(bob, 1), offers(bob, 1));

            // bob creates the offer that will be crossed.
            env(offer(bob, usd(500), XRP(500)), Ter(tesSUCCESS));
            env.close();
            env.require(Owners(bob, 2), offers(bob, 2));

            env(trust(alice, usd(1000)), Ter(tesSUCCESS));
            env(pay(gw, alice, usd(1000)), Ter(tesSUCCESS));

            // Order that can't be filled but will remove bob's expired offer:
            {
                TER const killedCode{TER{tecKILLED}};
                env(offer(alice, XRP(1000), usd(1000)), Txflags(tfFillOrKill), Ter(killedCode));
            }
            env.require(
                Balance(alice, startBalance - (f * 2)),
                Balance(alice, usd(1000)),
                Owners(alice, 1),
                offers(alice, 0),
                Balance(bob, startBalance - (f * 2)),
                Balance(bob, usd(kNone)),
                Owners(bob, 1),
                offers(bob, 1));

            // Order that can be filled
            env(offer(alice, XRP(500), usd(500)), Txflags(tfFillOrKill), Ter(tesSUCCESS));

            env.require(
                Balance(alice, startBalance - (f * 3) + XRP(500)),
                Balance(alice, usd(500)),
                Owners(alice, 1),
                offers(alice, 0),
                Balance(bob, startBalance - (f * 2) - XRP(500)),
                Balance(bob, usd(500)),
                Owners(bob, 1),
                offers(bob, 0));
        }

        // Immediate or Cancel - cross as much as possible
        // and add nothing on the books:
        {
            Env env{*this, features};

            auto const f = env.current()->fees().base;

            env.fund(startBalance, gw, alice, bob);
            env.close();

            env(trust(alice, usd(1000)), Ter(tesSUCCESS));
            env(pay(gw, alice, usd(1000)), Ter(tesSUCCESS));

            // No cross:
            {
                TER const expectedCode = tecKILLED;
                env(offer(alice, XRP(1000), usd(1000)),
                    Txflags(tfImmediateOrCancel),
                    Ter(expectedCode));
            }

            env.require(
                Balance(alice, startBalance - f - f),
                Balance(alice, usd(1000)),
                Owners(alice, 1),
                offers(alice, 0));

            // Partially cross:
            env(offer(bob, usd(50), XRP(50)), Ter(tesSUCCESS));
            env(offer(alice, XRP(1000), usd(1000)), Txflags(tfImmediateOrCancel), Ter(tesSUCCESS));

            env.require(
                Balance(alice, startBalance - f - f - f + XRP(50)),
                Balance(alice, usd(950)),
                Owners(alice, 1),
                offers(alice, 0),
                Balance(bob, startBalance - f - XRP(50)),
                Balance(bob, usd(50)),
                Owners(bob, 1),
                offers(bob, 0));

            // Fully cross:
            env(offer(bob, usd(50), XRP(50)), Ter(tesSUCCESS));
            env(offer(alice, XRP(50), usd(50)), Txflags(tfImmediateOrCancel), Ter(tesSUCCESS));

            env.require(
                Balance(alice, startBalance - f - f - f - f + XRP(100)),
                Balance(alice, usd(900)),
                Owners(alice, 1),
                offers(alice, 0),
                Balance(bob, startBalance - f - f - XRP(100)),
                Balance(bob, usd(100)),
                Owners(bob, 1),
                offers(bob, 0));
        }

        // tfPassive -- place the offer without crossing it.
        {
            Env env(*this, features);

            env.fund(startBalance, gw, alice, bob);
            env.close();

            env(trust(bob, usd(1000)));
            env.close();

            env(pay(gw, bob, usd(1000)));
            env.close();

            env(offer(alice, usd(1000), XRP(2000)));
            env.close();

            auto const aliceOffers = offersOnAccount(env, alice);
            BEAST_EXPECT(aliceOffers.size() == 1);
            for (auto const& offerPtr : aliceOffers)
            {
                auto const& offer = *offerPtr;
                BEAST_EXPECT(offer[sfTakerGets] == XRP(2000));
                BEAST_EXPECT(offer[sfTakerPays] == usd(1000));
            }

            // bob creates a passive offer that could cross alice's.
            // bob's offer should stay in the ledger.
            env(offer(bob, XRP(2000), usd(1000), tfPassive));
            env.close();
            env.require(offers(alice, 1));

            auto const bobOffers = offersOnAccount(env, bob);
            BEAST_EXPECT(bobOffers.size() == 1);
            for (auto const& offerPtr : bobOffers)
            {
                auto const& offer = *offerPtr;
                BEAST_EXPECT(offer[sfTakerGets] == usd(1000));
                BEAST_EXPECT(offer[sfTakerPays] == XRP(2000));
            }

            // It should be possible for gw to cross both of those offers.
            env(offer(gw, XRP(2000), usd(1000)));
            env.close();
            env.require(offers(alice, 0));
            env.require(offers(gw, 0));
            env.require(offers(bob, 1));

            env(offer(gw, usd(1000), XRP(2000)));
            env.close();
            env.require(offers(bob, 0));
            env.require(offers(gw, 0));
        }

        // tfPassive -- cross only offers of better quality.
        {
            Env env(*this, features);

            env.fund(startBalance, gw, "alice", "bob");
            env.close();

            env(trust("bob", usd(1000)));
            env.close();

            env(pay(gw, "bob", usd(1000)));
            env(offer("alice", usd(500), XRP(1001)));
            env.close();

            env(offer("alice", usd(500), XRP(1000)));
            env.close();

            auto const aliceOffers = offersOnAccount(env, "alice");
            BEAST_EXPECT(aliceOffers.size() == 2);

            // bob creates a passive offer.  That offer should cross one
            // of alice's (the one with better quality) and leave alice's
            // other offer untouched.
            env(offer("bob", XRP(2000), usd(1000), tfPassive));
            env.close();
            env.require(offers("alice", 1));

            auto const bobOffers = offersOnAccount(env, "bob");
            BEAST_EXPECT(bobOffers.size() == 1);
            for (auto const& offerPtr : bobOffers)
            {
                auto const& offer = *offerPtr;
                BEAST_EXPECT(offer[sfTakerGets] == usd(499.5));
                BEAST_EXPECT(offer[sfTakerPays] == XRP(999));
            }
        }
    }

    void
    testMalformed(FeatureBitset features)
    {
        testcase("Malformed Detection");

        using namespace jtx;

        auto const startBalance = XRP(1000000);
        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const usd = gw["USD"];

        Env env{*this, features};

        env.fund(startBalance, gw, alice);
        env.close();

        // Order that has invalid flags
        env(offer(alice, usd(1000), XRP(1000)),
            Txflags(tfImmediateOrCancel + 1),
            Ter(temINVALID_FLAG));
        env.require(Balance(alice, startBalance), Owners(alice, 0), offers(alice, 0));

        // Order with incompatible flags
        env(offer(alice, usd(1000), XRP(1000)),
            Txflags(tfImmediateOrCancel | tfFillOrKill),
            Ter(temINVALID_FLAG));
        env.require(Balance(alice, startBalance), Owners(alice, 0), offers(alice, 0));

        // Sell and buy the same asset
        {
            // Alice tries an XRP to XRP order:
            env(offer(alice, XRP(1000), XRP(1000)), Ter(temBAD_OFFER));
            env.require(Owners(alice, 0), offers(alice, 0));

            // Alice tries an IOU to IOU order:
            env(trust(alice, usd(1000)), Ter(tesSUCCESS));
            env(pay(gw, alice, usd(1000)), Ter(tesSUCCESS));
            env(offer(alice, usd(1000), usd(1000)), Ter(temREDUNDANT));
            env.require(Owners(alice, 1), offers(alice, 0));
        }

        // Offers with negative amounts
        {
            env(offer(alice, -usd(1000), XRP(1000)), Ter(temBAD_OFFER));
            env.require(Owners(alice, 1), offers(alice, 0));

            env(offer(alice, usd(1000), -XRP(1000)), Ter(temBAD_OFFER));
            env.require(Owners(alice, 1), offers(alice, 0));
        }

        // Offer with a bad expiration
        {
            env(offer(alice, usd(1000), XRP(1000)),
                Json(sfExpiration.fieldName, std::uint32_t(0)),
                Ter(temBAD_EXPIRATION));
            env.require(Owners(alice, 1), offers(alice, 0));
        }

        // Offer with a bad offer sequence
        {
            env(offer(alice, usd(1000), XRP(1000)),
                Json(jss::OfferSequence, std::uint32_t(0)),
                Ter(temBAD_SEQUENCE));
            env.require(Owners(alice, 1), offers(alice, 0));
        }

        // Use XRP as a currency code
        {
            auto const bad = IOU(gw, badCurrency());

            env(offer(alice, XRP(1000), bad(1000)), Ter(temBAD_CURRENCY));
            env.require(Owners(alice, 1), offers(alice, 0));
        }
    }

    void
    testExpiration(FeatureBitset features)
    {
        testcase("Offer Expiration");

        using namespace jtx;

        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const usd = gw["USD"];

        auto const startBalance = XRP(1000000);
        auto const usdOffer = usd(1000);
        auto const xrpOffer = XRP(1000);

        Env env{*this, features};

        env.fund(startBalance, gw, alice, bob);
        env.close();

        auto const f = env.current()->fees().base;

        env(trust(alice, usdOffer), Ter(tesSUCCESS));
        env(pay(gw, alice, usdOffer), Ter(tesSUCCESS));
        env.close();
        env.require(
            Balance(alice, startBalance - f),
            Balance(alice, usdOffer),
            offers(alice, 0),
            Owners(alice, 1));

        env(offer(alice, xrpOffer, usdOffer),
            Json(sfExpiration.fieldName, lastClose(env)),
            Ter(tecEXPIRED));

        env.require(
            Balance(alice, startBalance - f - f),
            Balance(alice, usdOffer),
            offers(alice, 0),
            Owners(alice, 1));
        env.close();

        // Add an offer that expires before the next ledger close
        env(offer(alice, xrpOffer, usdOffer),
            Json(sfExpiration.fieldName, lastClose(env) + 1),
            Ter(tesSUCCESS));
        env.require(
            Balance(alice, startBalance - f - f - f),
            Balance(alice, usdOffer),
            offers(alice, 1),
            Owners(alice, 2));

        // The offer expires (it's not removed yet)
        env.close();
        env.require(
            Balance(alice, startBalance - f - f - f),
            Balance(alice, usdOffer),
            offers(alice, 1),
            Owners(alice, 2));

        // Add offer - the expired offer is removed
        env(offer(bob, usdOffer, xrpOffer), Ter(tesSUCCESS));
        env.require(
            Balance(alice, startBalance - f - f - f),
            Balance(alice, usdOffer),
            offers(alice, 0),
            Owners(alice, 1),
            Balance(bob, startBalance - f),
            Balance(bob, usd(kNone)),
            offers(bob, 1),
            Owners(bob, 1));
    }

    void
    testUnfundedCross(FeatureBitset features)
    {
        testcase("Unfunded Crossing");

        using namespace jtx;

        auto const gw = Account{"gateway"};
        auto const usd = gw["USD"];

        auto const usdOffer = usd(1000);
        auto const xrpOffer = XRP(1000);

        Env env{*this, features};

        env.fund(XRP(1000000), gw);
        env.close();

        // The fee that's charged for transactions
        auto const f = env.current()->fees().base;

        // Account is at the reserve, and will dip below once
        // fees are subtracted.
        env.fund(reserve(env, 0), "alice");
        env.close();
        env(offer("alice", usdOffer, xrpOffer), Ter(tecUNFUNDED_OFFER));
        env.require(Balance("alice", reserve(env, 0) - f), Owners("alice", 0));

        // Account has just enough for the reserve and the
        // fee.
        env.fund(reserve(env, 0) + f, "bob");
        env.close();
        env(offer("bob", usdOffer, xrpOffer), Ter(tecUNFUNDED_OFFER));
        env.require(Balance("bob", reserve(env, 0)), Owners("bob", 0));

        // Account has enough for the reserve, the fee and
        // the offer, and a bit more, but not enough for the
        // reserve after the offer is placed.
        env.fund(reserve(env, 0) + f + XRP(1), "carol");
        env.close();
        env(offer("carol", usdOffer, xrpOffer), Ter(tecINSUF_RESERVE_OFFER));
        env.require(Balance("carol", reserve(env, 0) + XRP(1)), Owners("carol", 0));

        // Account has enough for the reserve plus one
        // offer, and the fee.
        env.fund(reserve(env, 1) + f, "dan");
        env.close();
        env(offer("dan", usdOffer, xrpOffer), Ter(tesSUCCESS));
        env.require(Balance("dan", reserve(env, 1)), Owners("dan", 1));

        // Account has enough for the reserve plus one
        // offer, the fee and the entire offer amount.
        env.fund(reserve(env, 1) + f + xrpOffer, "eve");
        env.close();
        env(offer("eve", usdOffer, xrpOffer), Ter(tesSUCCESS));
        env.require(Balance("eve", reserve(env, 1) + xrpOffer), Owners("eve", 1));
    }

    void
    testSelfCross(bool usePartner, FeatureBitset features)
    {
        testcase(std::string("Self-crossing") + (usePartner ? ", with partner account" : ""));

        using namespace jtx;

        auto const gw = Account{"gateway"};
        auto const partner = Account{"partner"};
        auto const usd = gw["USD"];
        auto const btc = gw["BTC"];

        Env env{*this, features};
        env.close();

        env.fund(XRP(10000), gw);
        if (usePartner)
        {
            env.fund(XRP(10000), partner);
            env.close();
            env(trust(partner, usd(100)));
            env(trust(partner, btc(500)));
            env.close();
            env(pay(gw, partner, usd(100)));
            env(pay(gw, partner, btc(500)));
        }
        auto const& accountToTest = usePartner ? partner : gw;

        env.close();
        env.require(offers(accountToTest, 0));

        // PART 1:
        // we will make two offers that can be used to bridge BTC to USD
        // through XRP
        env(offer(accountToTest, btc(250), XRP(1000)));
        env.require(offers(accountToTest, 1));

        // validate that the book now shows a BTC for XRP offer
        BEAST_EXPECT(isOffer(env, accountToTest, btc(250), XRP(1000)));

        auto const secondLegSeq = env.seq(accountToTest);
        env(offer(accountToTest, XRP(1000), usd(50)));
        env.require(offers(accountToTest, 2));

        // validate that the book also shows a XRP for USD offer
        BEAST_EXPECT(isOffer(env, accountToTest, XRP(1000), usd(50)));

        // now make an offer that will cross and auto-bridge, meaning
        // the outstanding offers will be taken leaving us with kNone
        env(offer(accountToTest, usd(50), btc(250)));

        auto jrr = getBookOffers(env, usd, btc);
        BEAST_EXPECT(jrr[jss::offers].isArray());
        BEAST_EXPECT(jrr[jss::offers].size() == 0);

        jrr = getBookOffers(env, btc, XRP);
        BEAST_EXPECT(jrr[jss::offers].isArray());
        BEAST_EXPECT(jrr[jss::offers].size() == 0);

        // At this point, all offers are expected to be consumed.
        {
            auto acctOffers = offersOnAccount(env, accountToTest);

            BEAST_EXPECT(acctOffers.empty());
            for (auto const& offerPtr : acctOffers)
            {
                auto const& offer = *offerPtr;
                BEAST_EXPECT(offer[sfLedgerEntryType] == ltOFFER);
                BEAST_EXPECT(offer[sfTakerGets] == usd(0));
                BEAST_EXPECT(offer[sfTakerPays] == XRP(0));
            }
        }

        // cancel that lingering second offer so that it doesn't interfere
        // with the next set of offers we test. This will not be needed once
        // the bridging bug is fixed
        env(offerCancel(accountToTest, secondLegSeq));
        env.require(offers(accountToTest, 0));

        // PART 2:
        // simple direct crossing  BTC to USD and then USD to BTC which causes
        // the first offer to be replaced
        env(offer(accountToTest, btc(250), usd(50)));
        env.require(offers(accountToTest, 1));

        // validate that the book shows one BTC for USD offer and no USD for
        // BTC offers
        BEAST_EXPECT(isOffer(env, accountToTest, btc(250), usd(50)));

        jrr = getBookOffers(env, usd, btc);
        BEAST_EXPECT(jrr[jss::offers].isArray());
        BEAST_EXPECT(jrr[jss::offers].size() == 0);

        // this second offer would self-cross directly, so it causes the first
        // offer by the same owner/taker to be removed
        env(offer(accountToTest, usd(50), btc(250)));
        env.require(offers(accountToTest, 1));

        // validate that we now have just the second offer...the first
        // was removed
        jrr = getBookOffers(env, btc, usd);
        BEAST_EXPECT(jrr[jss::offers].isArray());
        BEAST_EXPECT(jrr[jss::offers].size() == 0);

        BEAST_EXPECT(isOffer(env, accountToTest, usd(50), btc(250)));
    }

    void
    testNegativeBalance(FeatureBitset features)
    {
        // This test creates an offer test for negative balance
        // with transfer fees and minuscule funds.
        testcase("Negative Balance");

        using namespace jtx;

        // This is one of the few tests where fixReducedOffersV2 changes the
        // results.  So test both with and without fixReducedOffersV2.
        for (FeatureBitset localFeatures :
             {features - fixReducedOffersV2, features | fixReducedOffersV2})
        {
            Env env{*this, localFeatures};

            auto const gw = Account{"gateway"};
            auto const alice = Account{"alice"};
            auto const bob = Account{"bob"};
            auto const usd = gw["USD"];
            auto const btc = gw["BTC"];

            // these *interesting* amounts were taken
            // from the original JS test that was ported here
            auto const gwInitialBalance = drops(1149999730);
            auto const aliceInitialBalance = drops(499946999680);
            auto const bobInitialBalance = drops(10199999920);
            auto const smallAmount = STAmount{bob["USD"], UINT64_C(2710505431213761), -33};

            env.fund(gwInitialBalance, gw);
            env.fund(aliceInitialBalance, alice);
            env.fund(bobInitialBalance, bob);
            env.close();

            env(rate(gw, 1.005));

            env(trust(alice, usd(500)));
            env(trust(bob, usd(50)));
            env(trust(gw, alice["USD"](100)));

            env(pay(gw, alice, alice["USD"](50)));
            env(pay(gw, bob, smallAmount));

            env(offer(alice, usd(50), XRP(150000)));

            // unfund the offer
            env(pay(alice, gw, usd(100)));

            // drop the trust line (set to 0)
            env(trust(gw, alice["USD"](0)));

            // verify balances
            auto jrr = ledgerEntryState(env, alice, gw, "USD");
            BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "50");

            jrr = ledgerEntryState(env, bob, gw, "USD");
            BEAST_EXPECT(
                jrr[jss::node][sfBalance.fieldName][jss::value] == "-2710505431213761e-33");

            // create crossing offer
            std::uint32_t const bobOfferSeq = env.seq(bob);
            env(offer(bob, XRP(2000), usd(1)));

            if (localFeatures[fixReducedOffersV2])
            {
                // With the rounding introduced by fixReducedOffersV2, bob's
                // offer does not cross alice's offer and goes straight into
                // the ledger.
                jrr = ledgerEntryState(env, bob, gw, "USD");
                BEAST_EXPECT(
                    jrr[jss::node][sfBalance.fieldName][jss::value] == "-2710505431213761e-33");

                json::Value const bobOffer = ledgerEntryOffer(env, bob, bobOfferSeq)[jss::node];
                BEAST_EXPECT(bobOffer[sfTakerGets.jsonName][jss::value] == "1");
                BEAST_EXPECT(bobOffer[sfTakerPays.jsonName] == "2000000000");
                return;
            }

            // verify balances again.
            //
            // NOTE:
            // Here a difference in the rounding modes of our two offer
            // crossing algorithms becomes apparent.  The old offer crossing
            // would consume small_amount and transfer no XRP.  The new offer
            // crossing transfers a single drop, rather than no drops.
            auto const crossingDelta = drops(1);

            jrr = ledgerEntryState(env, alice, gw, "USD");
            BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "50");
            BEAST_EXPECT(
                env.balance(alice, xrpIssue()) ==
                aliceInitialBalance - env.current()->fees().base * 3 - crossingDelta);

            jrr = ledgerEntryState(env, bob, gw, "USD");
            BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "0");
            BEAST_EXPECT(
                env.balance(bob, xrpIssue()) ==
                bobInitialBalance - env.current()->fees().base * 2 + crossingDelta);
        }
    }

    void
    testOfferCrossWithXRP(bool reverseOrder, FeatureBitset features)
    {
        testcase(
            std::string("Offer Crossing with XRP, ") + (reverseOrder ? "Reverse" : "Normal") +
            " order");

        using namespace jtx;

        Env env{*this, features};

        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const usd = gw["USD"];

        env.fund(XRP(10000), gw, alice, bob);
        env.close();

        env(trust(alice, usd(1000)));
        env(trust(bob, usd(1000)));

        env(pay(gw, alice, alice["USD"](500)));

        if (reverseOrder)
            env(offer(bob, usd(1), XRP(4000)));

        env(offer(alice, XRP(150000), usd(50)));

        if (!reverseOrder)
            env(offer(bob, usd(1), XRP(4000)));

        // Existing offer pays better than this wants.
        // Fully consume existing offer.
        // Pay 1 USD, get 4000 XRP.

        auto jrr = ledgerEntryState(env, bob, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-1");
        jrr = ledgerEntryRoot(env, bob);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            to_string(
                (XRP(10000) - XRP(reverseOrder ? 4000 : 3000) - env.current()->fees().base * 2)
                    .xrp()));

        jrr = ledgerEntryState(env, alice, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-499");
        jrr = ledgerEntryRoot(env, alice);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            to_string(
                (XRP(10000) + XRP(reverseOrder ? 4000 : 3000) - env.current()->fees().base * 2)
                    .xrp()));
    }

    void
    testOfferCrossWithLimitOverride(FeatureBitset features)
    {
        testcase("Offer Crossing with Limit Override");

        using namespace jtx;

        Env env{*this, features};

        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const usd = gw["USD"];

        env.fund(XRP(100000), gw, alice, bob);
        env.close();

        env(trust(alice, usd(1000)));

        env(pay(gw, alice, alice["USD"](500)));

        env(offer(alice, XRP(150000), usd(50)));
        env(offer(bob, usd(1), XRP(3000)));

        auto jrr = ledgerEntryState(env, bob, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-1");
        jrr = ledgerEntryRoot(env, bob);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            to_string((XRP(100000) - XRP(3000) - env.current()->fees().base * 1).xrp()));

        jrr = ledgerEntryState(env, alice, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-499");
        jrr = ledgerEntryRoot(env, alice);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            to_string((XRP(100000) + XRP(3000) - env.current()->fees().base * 2).xrp()));
    }

    void
    testOfferAcceptThenCancel(FeatureBitset features)
    {
        testcase("Offer Accept then Cancel.");

        using namespace jtx;

        Env env{*this, features};

        auto const usd = env.master["USD"];

        auto const nextOfferSeq = env.seq(env.master);
        env(offer(env.master, XRP(500), usd(100)));
        env.close();

        env(offerCancel(env.master, nextOfferSeq));
        BEAST_EXPECT(env.seq(env.master) == nextOfferSeq + 2);

        // ledger_accept, call twice and verify no odd behavior
        env.close();
        env.close();
        BEAST_EXPECT(env.seq(env.master) == nextOfferSeq + 2);
    }

    void
    testOfferCancelPastAndFuture(FeatureBitset features)
    {
        testcase("Offer Cancel Past and Future Sequence.");

        using namespace jtx;

        Env env{*this, features};

        auto const alice = Account{"alice"};

        auto const nextOfferSeq = env.seq(env.master);
        env.fund(XRP(10000), alice);
        env.close();

        env(offerCancel(env.master, nextOfferSeq));

        env(offerCancel(env.master, env.seq(env.master)), Ter(temBAD_SEQUENCE));

        env(offerCancel(env.master, env.seq(env.master) + 1), Ter(temBAD_SEQUENCE));

        env.close();
    }

    void
    testCurrencyConversionEntire(FeatureBitset features)
    {
        testcase("Currency Conversion: Entire Offer");

        using namespace jtx;

        Env env{*this, features};

        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const usd = gw["USD"];

        env.fund(XRP(10000), gw, alice, bob);
        env.close();
        env.require(Owners(bob, 0));

        env(trust(alice, usd(100)));
        env(trust(bob, usd(1000)));

        env.require(Owners(alice, 1), Owners(bob, 1));

        env(pay(gw, alice, alice["USD"](100)));
        auto const bobOfferSeq = env.seq(bob);
        env(offer(bob, usd(100), XRP(500)));

        env.require(Owners(alice, 1), Owners(bob, 2));
        auto jro = ledgerEntryOffer(env, bob, bobOfferSeq);
        BEAST_EXPECT(jro[jss::node][jss::TakerGets] == XRP(500).value().getText());
        BEAST_EXPECT(
            jro[jss::node][jss::TakerPays] == usd(100).value().getJson(JsonOptions::Values::None));

        env(pay(alice, alice, XRP(500)), Sendmax(usd(100)));

        auto jrr = ledgerEntryState(env, alice, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "0");
        jrr = ledgerEntryRoot(env, alice);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            to_string((XRP(10000) + XRP(500) - env.current()->fees().base * 2).xrp()));

        jrr = ledgerEntryState(env, bob, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-100");

        jro = ledgerEntryOffer(env, bob, bobOfferSeq);
        BEAST_EXPECT(jro[jss::error] == "entryNotFound");

        env.require(Owners(alice, 1), Owners(bob, 1));
    }

    void
    testCurrencyConversionIntoDebt(FeatureBitset features)
    {
        testcase("Currency Conversion: Offerer Into Debt");

        using namespace jtx;

        Env env{*this, features};

        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const carol = Account{"carol"};

        env.fund(XRP(10000), alice, bob, carol);
        env.close();

        env(trust(alice, carol["EUR"](2000)));
        env(trust(bob, alice["USD"](100)));
        env(trust(carol, bob["EUR"](1000)));

        auto const bobOfferSeq = env.seq(bob);
        env(offer(bob, alice["USD"](50), carol["EUR"](200)), Ter(tecUNFUNDED_OFFER));

        env(offer(alice, carol["EUR"](200), alice["USD"](50)));

        auto jro = ledgerEntryOffer(env, bob, bobOfferSeq);
        BEAST_EXPECT(jro[jss::error] == "entryNotFound");
    }

    void
    testCurrencyConversionInParts(FeatureBitset features)
    {
        testcase("Currency Conversion: In Parts");

        using namespace jtx;

        Env env{*this, features};

        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const usd = gw["USD"];

        env.fund(XRP(10000), gw, alice, bob);
        env.close();

        env(trust(alice, usd(200)));
        env(trust(bob, usd(1000)));

        env(pay(gw, alice, alice["USD"](200)));

        auto const bobOfferSeq = env.seq(bob);
        env(offer(bob, usd(100), XRP(500)));

        env(pay(alice, alice, XRP(200)), Sendmax(usd(100)));

        // The previous payment reduced the remaining offer amount by 200 XRP
        auto jro = ledgerEntryOffer(env, bob, bobOfferSeq);
        BEAST_EXPECT(jro[jss::node][jss::TakerGets] == XRP(300).value().getText());
        BEAST_EXPECT(
            jro[jss::node][jss::TakerPays] == usd(60).value().getJson(JsonOptions::Values::None));

        // the balance between alice and gw is 160 USD..200 less the 40 taken
        // by the offer
        auto jrr = ledgerEntryState(env, alice, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-160");
        // alice now has 200 more XRP from the payment
        jrr = ledgerEntryRoot(env, alice);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            to_string((XRP(10000) + XRP(200) - env.current()->fees().base * 2).xrp()));

        // bob got 40 USD from partial consumption of the offer
        jrr = ledgerEntryState(env, bob, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-40");

        // Alice converts USD to XRP which should fail
        // due to PartialPayment.
        env(pay(alice, alice, XRP(600)), Sendmax(usd(100)), Ter(tecPATH_PARTIAL));

        // Alice converts USD to XRP, should succeed because
        // we permit partial payment
        env(pay(alice, alice, XRP(600)), Sendmax(usd(100)), Txflags(tfPartialPayment));

        // Verify the offer was consumed
        jro = ledgerEntryOffer(env, bob, bobOfferSeq);
        BEAST_EXPECT(jro[jss::error] == "entryNotFound");

        // verify balances look right after the partial payment
        // only 300 XRP should be have been payed since that's all
        // that remained in the offer from bob. The alice balance is now
        // 100 USD because another 60 USD were transferred to bob in the second
        // payment
        jrr = ledgerEntryState(env, alice, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-100");
        jrr = ledgerEntryRoot(env, alice);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            to_string((XRP(10000) + XRP(200) + XRP(300) - env.current()->fees().base * 4).xrp()));

        // bob now has 100 USD - 40 from the first payment and 60 from the
        // second (partial) payment
        jrr = ledgerEntryState(env, bob, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-100");
    }

    void
    testCrossCurrencyStartXRP(FeatureBitset features)
    {
        testcase("Cross Currency Payment: Start with XRP");

        using namespace jtx;

        Env env{*this, features};

        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const carol = Account{"carol"};
        auto const usd = gw["USD"];

        env.fund(XRP(10000), gw, alice, bob, carol);
        env.close();

        env(trust(carol, usd(1000)));
        env(trust(bob, usd(2000)));

        env(pay(gw, carol, carol["USD"](500)));

        auto const carolOfferSeq = env.seq(carol);
        env(offer(carol, XRP(500), usd(50)));

        env(pay(alice, bob, usd(25)), Sendmax(XRP(333)));

        auto jrr = ledgerEntryState(env, bob, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-25");

        jrr = ledgerEntryState(env, carol, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-475");

        auto jro = ledgerEntryOffer(env, carol, carolOfferSeq);
        BEAST_EXPECT(
            jro[jss::node][jss::TakerGets] == usd(25).value().getJson(JsonOptions::Values::None));
        BEAST_EXPECT(jro[jss::node][jss::TakerPays] == XRP(250).value().getText());
    }

    void
    testCrossCurrencyEndXRP(FeatureBitset features)
    {
        testcase("Cross Currency Payment: End with XRP");

        using namespace jtx;

        Env env{*this, features};

        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const carol = Account{"carol"};
        auto const usd = gw["USD"];

        env.fund(XRP(10000), gw, alice, bob, carol);
        env.close();

        env(trust(alice, usd(1000)));
        env(trust(carol, usd(2000)));

        env(pay(gw, alice, alice["USD"](500)));

        auto const carolOfferSeq = env.seq(carol);
        env(offer(carol, usd(50), XRP(500)));

        env(pay(alice, bob, XRP(250)), Sendmax(usd(333)));

        auto jrr = ledgerEntryState(env, alice, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-475");

        jrr = ledgerEntryState(env, carol, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-25");

        jrr = ledgerEntryRoot(env, bob);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            std::to_string(XRP(10000).value().mantissa() + XRP(250).value().mantissa()));

        auto jro = ledgerEntryOffer(env, carol, carolOfferSeq);
        BEAST_EXPECT(jro[jss::node][jss::TakerGets] == XRP(250).value().getText());
        BEAST_EXPECT(
            jro[jss::node][jss::TakerPays] == usd(25).value().getJson(JsonOptions::Values::None));
    }

    void
    testCrossCurrencyBridged(FeatureBitset features)
    {
        testcase("Cross Currency Payment: Bridged");

        using namespace jtx;

        Env env{*this, features};

        auto const gw1 = Account{"gateway_1"};
        auto const gw2 = Account{"gateway_2"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const carol = Account{"carol"};
        auto const dan = Account{"dan"};
        auto const usd = gw1["USD"];
        auto const eur = gw2["EUR"];

        env.fund(XRP(10000), gw1, gw2, alice, bob, carol, dan);
        env.close();

        env(trust(alice, usd(1000)));
        env(trust(bob, eur(1000)));
        env(trust(carol, usd(1000)));
        env(trust(dan, eur(1000)));

        env(pay(gw1, alice, alice["USD"](500)));
        env(pay(gw2, dan, dan["EUR"](400)));

        auto const carolOfferSeq = env.seq(carol);
        env(offer(carol, usd(50), XRP(500)));

        auto const danOfferSeq = env.seq(dan);
        env(offer(dan, XRP(500), eur(50)));

        json::Value jtp{json::ValueType::Array};
        jtp[0u][0u][jss::currency] = "XRP";
        env(pay(alice, bob, eur(30)), Json(jss::Paths, jtp), Sendmax(usd(333)));

        auto jrr = ledgerEntryState(env, alice, gw1, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "470");

        jrr = ledgerEntryState(env, bob, gw2, "EUR");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-30");

        jrr = ledgerEntryState(env, carol, gw1, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-30");

        jrr = ledgerEntryState(env, dan, gw2, "EUR");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-370");

        auto jro = ledgerEntryOffer(env, carol, carolOfferSeq);
        BEAST_EXPECT(jro[jss::node][jss::TakerGets] == XRP(200).value().getText());
        BEAST_EXPECT(
            jro[jss::node][jss::TakerPays] == usd(20).value().getJson(JsonOptions::Values::None));

        jro = ledgerEntryOffer(env, dan, danOfferSeq);
        BEAST_EXPECT(
            jro[jss::node][jss::TakerGets] ==
            gw2["EUR"](20).value().getJson(JsonOptions::Values::None));
        BEAST_EXPECT(jro[jss::node][jss::TakerPays] == XRP(200).value().getText());
    }

    void
    testBridgedSecondLegDry(FeatureBitset features)
    {
        // At least with Taker bridging, a sensitivity was identified if the
        // second leg goes dry before the first one.  This test exercises that
        // case.
        testcase("Auto Bridged Second Leg Dry");

        using namespace jtx;
        Env env(*this, features);

        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const gw{"gateway"};
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        env.fund(XRP(100000000), alice, bob, carol, gw);
        env.close();

        env.trust(usd(10), alice);
        env.close();
        env(pay(gw, alice, usd(10)));
        env.trust(usd(10), carol);
        env.close();
        env(pay(gw, carol, usd(3)));

        env(offer(alice, eur(2), XRP(1)));
        env(offer(alice, eur(2), XRP(1)));

        env(offer(alice, XRP(1), usd(4)));
        env(offer(carol, XRP(1), usd(3)));
        env.close();

        // Bob offers to buy 10 USD for 10 EUR.
        //  1. He spends 2 EUR taking Alice's auto-bridged offers and
        //     gets 4 USD for that.
        //  2. He spends another 2 EUR taking Alice's last EUR->XRP offer and
        //     Carol's XRP-USD offer.  He gets 3 USD for that.
        // The key for this test is that Alice's XRP->USD leg goes dry before
        // Alice's EUR->XRP.  The XRP->USD leg is the second leg which showed
        // some sensitivity.
        env.trust(eur(10), bob);
        env.close();
        env(pay(gw, bob, eur(10)));
        env.close();
        env(offer(bob, usd(10), eur(10)));
        env.close();

        env.require(Balance(bob, usd(7)));
        env.require(Balance(bob, eur(6)));
        env.require(offers(bob, 1));
        env.require(Owners(bob, 3));

        env.require(Balance(alice, usd(6)));
        env.require(Balance(alice, eur(4)));
        env.require(offers(alice, 0));
        env.require(Owners(alice, 2));

        env.require(Balance(carol, usd(0)));
        env.require(Balance(carol, eur(kNone)));

        env.require(offers(carol, 0));
        env.require(Owners(carol, 1));
    }

    void
    testOfferFeesConsumeFunds(FeatureBitset features)
    {
        testcase("Offer Fees Consume Funds");

        using namespace jtx;

        Env env{*this, features};

        auto const gw1 = Account{"gateway_1"};
        auto const gw2 = Account{"gateway_2"};
        auto const gw3 = Account{"gateway_3"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const usD1 = gw1["USD"];
        auto const usD2 = gw2["USD"];
        auto const usD3 = gw3["USD"];

        // Provide micro amounts to compensate for fees to make results round
        // nice.
        // reserve: Alice has 3 entries in the ledger, via trust lines
        // fees:
        //  1 for each trust limit == 3 (alice < mtgox/amazon/bitstamp) +
        //  1 for payment          == 4
        auto const startingXrp =
            XRP(100) + env.current()->fees().accountReserve(3) + env.current()->fees().base * 4;

        env.fund(startingXrp, gw1, gw2, gw3, alice, bob);
        env.close();

        env(trust(alice, usD1(1000)));
        env(trust(alice, usD2(1000)));
        env(trust(alice, usD3(1000)));
        env(trust(bob, usD1(1000)));
        env(trust(bob, usD2(1000)));

        env(pay(gw1, bob, bob["USD"](500)));

        env(offer(bob, XRP(200), usD1(200)));
        // Alice has 350 fees - a reserve of 50 = 250 reserve = 100 available.
        // Ask for more than available to prove reserve works.
        env(offer(alice, usD1(200), XRP(200)));

        auto jrr = ledgerEntryState(env, alice, gw1, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "100");
        jrr = ledgerEntryRoot(env, alice);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            STAmount(env.current()->fees().accountReserve(3)).getText());

        jrr = ledgerEntryState(env, bob, gw1, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-400");
    }

    void
    testOfferCreateThenCross(FeatureBitset features)
    {
        testcase("Offer Create, then Cross");

        using namespace jtx;

        for (auto numberSwitchOver : {false, true})
        {
            Env env{*this, features};
            if (numberSwitchOver)
            {
                env.enableFeature(fixUniversalNumber);
            }
            else
            {
                env.disableFeature(fixUniversalNumber);
            }

            auto const gw = Account{"gateway"};
            auto const alice = Account{"alice"};
            auto const bob = Account{"bob"};
            auto const usd = gw["USD"];

            env.fund(XRP(10000), gw, alice, bob);
            env.close();

            env(rate(gw, 1.005));

            env(trust(alice, usd(1000)));
            env(trust(bob, usd(1000)));
            env(trust(gw, alice["USD"](50)));

            env(pay(gw, bob, bob["USD"](1)));
            env(pay(alice, gw, usd(50)));

            env(trust(gw, alice["USD"](0)));

            env(offer(alice, usd(50), XRP(150000)));
            env(offer(bob, XRP(100), usd(0.1)));

            auto jrr = ledgerEntryState(env, alice, gw, "USD");
            BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "49.96666666666667");

            jrr = ledgerEntryState(env, bob, gw, "USD");
            json::Value const bobUSD = jrr[jss::node][sfBalance.fieldName][jss::value];
            if (!numberSwitchOver)
            {
                BEAST_EXPECT(bobUSD == "-0.966500000033334");
            }
            else
            {
                BEAST_EXPECT(bobUSD == "-0.9665000000333333");
            }
        }
    }

    void
    testSellFlagBasic(FeatureBitset features)
    {
        testcase("Offer tfSell: Basic Sell");

        using namespace jtx;

        Env env{*this, features};

        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const usd = gw["USD"];

        auto const startingXrp =
            XRP(100) + env.current()->fees().accountReserve(1) + env.current()->fees().base * 2;

        env.fund(startingXrp, gw, alice, bob);
        env.close();

        env(trust(alice, usd(1000)));
        env(trust(bob, usd(1000)));

        env(pay(gw, bob, bob["USD"](500)));

        env(offer(bob, XRP(200), usd(200)), Json(jss::Flags, tfSell));
        // Alice has 350 + fees - a reserve of 50 = 250 reserve = 100 available.
        // Alice has 350 + fees - a reserve of 50 = 250 reserve = 100 available.
        // Ask for more than available to prove reserve works.
        env(offer(alice, usd(200), XRP(200)), Json(jss::Flags, tfSell));

        auto jrr = ledgerEntryState(env, alice, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-100");
        jrr = ledgerEntryRoot(env, alice);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            STAmount(env.current()->fees().accountReserve(1)).getText());

        jrr = ledgerEntryState(env, bob, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-400");
    }

    void
    testSellFlagExceedLimit(FeatureBitset features)
    {
        testcase("Offer tfSell: 2x Sell Exceed Limit");

        using namespace jtx;

        Env env{*this, features};

        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const usd = gw["USD"];

        auto const startingXrp =
            XRP(100) + env.current()->fees().accountReserve(1) + env.current()->fees().base * 2;

        env.fund(startingXrp, gw, alice, bob);
        env.close();

        env(trust(alice, usd(150)));
        env(trust(bob, usd(1000)));

        env(pay(gw, bob, bob["USD"](500)));

        env(offer(bob, XRP(100), usd(200)));
        // Alice has 350 fees - a reserve of 50 = 250 reserve = 100 available.
        // Ask for more than available to prove reserve works.
        // Taker pays 100 USD for 100 XRP.
        // Selling XRP.
        // Will sell all 100 XRP and get more USD than asked for.
        env(offer(alice, usd(100), XRP(100)), Json(jss::Flags, tfSell));

        auto jrr = ledgerEntryState(env, alice, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-200");
        jrr = ledgerEntryRoot(env, alice);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            STAmount(env.current()->fees().accountReserve(1)).getText());

        jrr = ledgerEntryState(env, bob, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-300");
    }

    void
    testGatewayCrossCurrency(FeatureBitset features)
    {
        testcase("Client Issue #535: Gateway Cross Currency");

        using namespace jtx;

        Env env{*this, features};

        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const xts = gw["XTS"];
        auto const xxx = gw["XXX"];

        auto const startingXrp =
            XRP(100.1) + env.current()->fees().accountReserve(1) + env.current()->fees().base * 2;

        env.fund(startingXrp, gw, alice, bob);
        env.close();

        env(trust(alice, xts(1000)));
        env(trust(alice, xxx(1000)));
        env(trust(bob, xts(1000)));
        env(trust(bob, xxx(1000)));

        env(pay(gw, alice, alice["XTS"](100)));
        env(pay(gw, alice, alice["XXX"](100)));
        env(pay(gw, bob, bob["XTS"](100)));
        env(pay(gw, bob, bob["XXX"](100)));

        env(offer(alice, xts(100), xxx(100)));

        // WS client is used here because the RPC client could not
        // be convinced to pass the build_path argument
        auto wsc = makeWSClient(env.app().config());
        json::Value payment;
        payment[jss::secret] = toBase58(generateSeed("bob"));
        payment[jss::id] = env.seq(bob);
        payment[jss::build_path] = true;
        payment[jss::tx_json] = pay(bob, bob, bob["XXX"](1));
        payment[jss::tx_json][jss::Sequence] =
            env.current()->read(keylet::account(bob.id()))->getFieldU32(sfSequence);
        payment[jss::tx_json][jss::Fee] = to_string(env.current()->fees().base);
        payment[jss::tx_json][jss::SendMax] =
            bob["XTS"](1.5).value().getJson(JsonOptions::Values::None);
        auto jrr = wsc->invoke("submit", payment);
        BEAST_EXPECT(jrr[jss::status] == "success");
        BEAST_EXPECT(jrr[jss::result][jss::engine_result] == "tesSUCCESS");
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(jrr.isMember(jss::jsonrpc) && jrr[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(jrr.isMember(jss::ripplerpc) && jrr[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(jrr.isMember(jss::id) && jrr[jss::id] == 5);
        }

        jrr = ledgerEntryState(env, alice, gw, "XTS");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-101");
        jrr = ledgerEntryState(env, alice, gw, "XXX");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-99");

        jrr = ledgerEntryState(env, bob, gw, "XTS");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-99");
        jrr = ledgerEntryState(env, bob, gw, "XXX");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-101");
    }

    // Helper function that validates a *defaulted* trustline: one that has
    // no unusual flags set and doesn't have high or low limits set. Such a
    // trustline may have an actual balance (it can be created automatically
    // if a user places an offer to acquire an IOU for which they don't have
    // a trust line defined). If the trustline is not defaulted then the tests
    // will not pass.
    void
    verifyDefaultTrustline(
        jtx::Env& env,
        jtx::Account const& account,
        jtx::PrettyAmount const& expectBalance)
    {
        Issue const& issue = expectBalance.value().get<Issue>();
        auto const sleTrust = env.le(keylet::line(account.id(), issue));
        BEAST_EXPECT(sleTrust);
        if (sleTrust)
        {
            bool const accountLow = account.id() < issue.account;

            STAmount low{issue};
            STAmount high{issue};

            low.get<Issue>().account = (accountLow ? account.id() : issue.account);
            high.get<Issue>().account = (accountLow ? issue.account : account.id());

            BEAST_EXPECT(sleTrust->getFieldAmount(sfLowLimit) == low);
            BEAST_EXPECT(sleTrust->getFieldAmount(sfHighLimit) == high);

            STAmount actualBalance{sleTrust->getFieldAmount(sfBalance)};
            if (!accountLow)
                actualBalance.negate();

            BEAST_EXPECT(actualBalance == expectBalance);
        }
    }

    void
    testPartialCross(FeatureBitset features)
    {
        // Test a number of different corner cases regarding adding a
        // possibly crossable offer to an account.  The test is table
        // driven so it should be easy to add or remove tests.
        testcase("Partial Crossing");

        using namespace jtx;

        auto const gw = Account("gateway");
        auto const usd = gw["USD"];

        Env env{*this, features};

        env.fund(XRP(10000000), gw);
        env.close();

        // The fee that's charged for transactions
        auto const f = env.current()->fees().base;

        // To keep things simple all offers are 1 : 1 for XRP : USD.
        enum class PreTrustType { NoPreTrust, GwPreTrust, AcctPreTrust };
        struct TestData
        {
            std::string account;      // Account operated on
            STAmount fundXrp;         // Account funded with
            int bookAmount;           // USD -> XRP offer on the books
            PreTrustType preTrust;    // If true, pre-establish trust line
            int offerAmount;          // Account offers this much XRP -> USD
            TER tec;                  // Returned tec code
            STAmount spentXrp;        // Amount removed from fundXrp
            PrettyAmount balanceUsd;  // Balance on account end
            int offers;               // Offers on account
            int owners;               // Owners on account
        };

        // clang-format off
        TestData const tests[]{
            // acct                     fundXrp        bookAmt   preTrust  offerAmount                   tec     spentXrp       balanceUSD offers  owners
            {.account="ann",             .fundXrp=reserve(env, 0) + 0 * f,    .bookAmount=1,   .preTrust=PreTrustType::NoPreTrust, .offerAmount=1000,      .tec=tecUNFUNDED_OFFER,               .spentXrp=f, .balanceUsd=usd(      0),    .offers=0, .owners=0},  // Account is at the reserve, and will dip below once fees are subtracted.
            {.account="bev",             .fundXrp=reserve(env, 0) + 1 * f,    .bookAmount=1,   .preTrust=PreTrustType::NoPreTrust, .offerAmount=1000,      .tec=tecUNFUNDED_OFFER,               .spentXrp=f, .balanceUsd=usd(      0),    .offers=0, .owners=0},  // Account has just enough for the reserve and the fee.
            {.account="cam",             .fundXrp=reserve(env, 0) + 2 * f,    .bookAmount=0,   .preTrust=PreTrustType::NoPreTrust, .offerAmount=1000, .tec=tecINSUF_RESERVE_OFFER,               .spentXrp=f, .balanceUsd=usd(      0),    .offers=0, .owners=0},  // Account has enough for the reserve, the fee and the offer, and a bit more, but not enough for the reserve after the offer is placed.
            {.account="deb", .fundXrp=drops(10) + reserve(env, 0) + 1 * f,    .bookAmount=1,   .preTrust=PreTrustType::NoPreTrust, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=drops(10)   + f, .balanceUsd=usd(0.00001),    .offers=0, .owners=1},  // Account has enough to buy a little USD then the offer runs dry.
            {.account="eve",             .fundXrp=reserve(env, 1) + 0 * f,    .bookAmount=0,   .preTrust=PreTrustType::NoPreTrust, .offerAmount=1000,             .tec=tesSUCCESS,               .spentXrp=f, .balanceUsd=usd(      0),    .offers=1, .owners=1},  // No offer to cross
            {.account="flo",             .fundXrp=reserve(env, 1) + 0 * f,    .bookAmount=1,   .preTrust=PreTrustType::NoPreTrust, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP(   1)   + f, .balanceUsd=usd(      1),    .offers=0, .owners=1},
            {.account="gay",             .fundXrp=reserve(env, 1) + 1 * f, .bookAmount=1000,   .preTrust=PreTrustType::NoPreTrust, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP(  50)   + f, .balanceUsd=usd(     50),    .offers=0, .owners=1},
            {.account="hye", .fundXrp=XRP(1000)                   + 1 * f, .bookAmount=1000,   .preTrust=PreTrustType::NoPreTrust, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP( 800)   + f, .balanceUsd=usd(    800),    .offers=0, .owners=1},
            {.account="ivy", .fundXrp=XRP(   1) + reserve(env, 1) + 1 * f,    .bookAmount=1,   .preTrust=PreTrustType::NoPreTrust, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP(   1)   + f, .balanceUsd=usd(      1),    .offers=0, .owners=1},
            {.account="joy", .fundXrp=XRP(   1) + reserve(env, 2) + 1 * f,    .bookAmount=1,   .preTrust=PreTrustType::NoPreTrust, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP(   1)   + f, .balanceUsd=usd(      1),    .offers=1, .owners=2},
            {.account="kim", .fundXrp=XRP( 900) + reserve(env, 2) + 1 * f,  .bookAmount=999,   .preTrust=PreTrustType::NoPreTrust, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP( 999)   + f, .balanceUsd=usd(    999),    .offers=0, .owners=1},
            {.account="liz", .fundXrp=XRP( 998) + reserve(env, 0) + 1 * f,  .bookAmount=999,   .preTrust=PreTrustType::NoPreTrust, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP( 998)   + f, .balanceUsd=usd(    998),    .offers=0, .owners=1},
            {.account="meg", .fundXrp=XRP( 998) + reserve(env, 1) + 1 * f,  .bookAmount=999,   .preTrust=PreTrustType::NoPreTrust, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP( 999)   + f, .balanceUsd=usd(    999),    .offers=0, .owners=1},
            {.account="nia", .fundXrp=XRP( 998) + reserve(env, 2) + 1 * f,  .bookAmount=999,   .preTrust=PreTrustType::NoPreTrust, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP( 999)   + f, .balanceUsd=usd(    999),    .offers=1, .owners=2},
            {.account="ova", .fundXrp=XRP( 999) + reserve(env, 0) + 1 * f, .bookAmount=1000,   .preTrust=PreTrustType::NoPreTrust, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP( 999)   + f, .balanceUsd=usd(    999),    .offers=0, .owners=1},
            {.account="pam", .fundXrp=XRP( 999) + reserve(env, 1) + 1 * f, .bookAmount=1000,   .preTrust=PreTrustType::NoPreTrust, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP(1000)   + f, .balanceUsd=usd(   1000),    .offers=0, .owners=1},
            {.account="rae", .fundXrp=XRP( 999) + reserve(env, 2) + 1 * f, .bookAmount=1000,   .preTrust=PreTrustType::NoPreTrust, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP(1000)   + f, .balanceUsd=usd(   1000),    .offers=0, .owners=1},
            {.account="sue", .fundXrp=XRP(1000) + reserve(env, 2) + 1 * f,    .bookAmount=0,   .preTrust=PreTrustType::NoPreTrust, .offerAmount=1000,             .tec=tesSUCCESS,               .spentXrp=f, .balanceUsd=usd(      0),    .offers=1, .owners=1},
            //---------------- Pre-established trust lines ---------------------
            {.account="abe",             .fundXrp=reserve(env, 0) + 0 * f,    .bookAmount=1,   .preTrust=PreTrustType::GwPreTrust, .offerAmount=1000,      .tec=tecUNFUNDED_OFFER,               .spentXrp=f, .balanceUsd=usd(      0),    .offers=0, .owners=0},
            {.account="bud",             .fundXrp=reserve(env, 0) + 1 * f,    .bookAmount=1,   .preTrust=PreTrustType::GwPreTrust, .offerAmount=1000,      .tec=tecUNFUNDED_OFFER,               .spentXrp=f, .balanceUsd=usd(      0),    .offers=0, .owners=0},
            {.account="che",             .fundXrp=reserve(env, 0) + 2 * f,    .bookAmount=0,   .preTrust=PreTrustType::GwPreTrust, .offerAmount=1000, .tec=tecINSUF_RESERVE_OFFER,               .spentXrp=f, .balanceUsd=usd(      0),    .offers=0, .owners=0},
            {.account="dan", .fundXrp=drops(10) + reserve(env, 0) + 1 * f,    .bookAmount=1,   .preTrust=PreTrustType::GwPreTrust, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=drops(10)   + f, .balanceUsd=usd(0.00001),    .offers=0, .owners=0},
            {.account="eli", .fundXrp=XRP(  20) + reserve(env, 0) + 1 * f, .bookAmount=1000,   .preTrust=PreTrustType::GwPreTrust, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP(20) + 1 * f, .balanceUsd=usd(     20),    .offers=0, .owners=0},
            {.account="fyn",             .fundXrp=reserve(env, 1) + 0 * f,    .bookAmount=0,   .preTrust=PreTrustType::GwPreTrust, .offerAmount=1000,             .tec=tesSUCCESS,               .spentXrp=f, .balanceUsd=usd(      0),    .offers=1, .owners=1},
            {.account="gar",             .fundXrp=reserve(env, 1) + 0 * f,    .bookAmount=1,   .preTrust=PreTrustType::GwPreTrust, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP( 1) +     f, .balanceUsd=usd(      1),    .offers=1, .owners=1},
            {.account="hal",             .fundXrp=reserve(env, 1) + 1 * f,    .bookAmount=1,   .preTrust=PreTrustType::GwPreTrust, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP( 1) +     f, .balanceUsd=usd(      1),    .offers=1, .owners=1},

            {.account="ned",             .fundXrp=reserve(env, 1) + 0 * f,    .bookAmount=1, .preTrust=PreTrustType::AcctPreTrust, .offerAmount=1000,      .tec=tecUNFUNDED_OFFER,           .spentXrp=2 * f, .balanceUsd=usd(      0),    .offers=0, .owners=1},
            {.account="ole",             .fundXrp=reserve(env, 1) + 1 * f,    .bookAmount=1, .preTrust=PreTrustType::AcctPreTrust, .offerAmount=1000,      .tec=tecUNFUNDED_OFFER,           .spentXrp=2 * f, .balanceUsd=usd(      0),    .offers=0, .owners=1},
            {.account="pat",             .fundXrp=reserve(env, 1) + 2 * f,    .bookAmount=0, .preTrust=PreTrustType::AcctPreTrust, .offerAmount=1000,      .tec=tecUNFUNDED_OFFER,           .spentXrp=2 * f, .balanceUsd=usd(      0),    .offers=0, .owners=1},
            {.account="quy",             .fundXrp=reserve(env, 1) + 2 * f,    .bookAmount=1, .preTrust=PreTrustType::AcctPreTrust, .offerAmount=1000,      .tec=tecUNFUNDED_OFFER,           .spentXrp=2 * f, .balanceUsd=usd(      0),    .offers=0, .owners=1},
            {.account="ron",             .fundXrp=reserve(env, 1) + 3 * f,    .bookAmount=0, .preTrust=PreTrustType::AcctPreTrust, .offerAmount=1000, .tec=tecINSUF_RESERVE_OFFER,           .spentXrp=2 * f, .balanceUsd=usd(      0),    .offers=0, .owners=1},
            {.account="syd", .fundXrp=drops(10) + reserve(env, 1) + 2 * f,    .bookAmount=1, .preTrust=PreTrustType::AcctPreTrust, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=drops(10) + 2 * f, .balanceUsd=usd(0.00001),    .offers=0, .owners=1},
            {.account="ted", .fundXrp=XRP(  20) + reserve(env, 1) + 2 * f, .bookAmount=1000, .preTrust=PreTrustType::AcctPreTrust, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP(20) + 2 * f, .balanceUsd=usd(     20),    .offers=0, .owners=1},
            {.account="uli",             .fundXrp=reserve(env, 2) + 0 * f,    .bookAmount=0, .preTrust=PreTrustType::AcctPreTrust, .offerAmount=1000, .tec=tecINSUF_RESERVE_OFFER,           .spentXrp=2 * f, .balanceUsd=usd(      0),    .offers=0, .owners=1},
            {.account="vic",             .fundXrp=reserve(env, 2) + 0 * f,    .bookAmount=1, .preTrust=PreTrustType::AcctPreTrust, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP( 1) + 2 * f, .balanceUsd=usd(      1),    .offers=0, .owners=1},
            {.account="wes",             .fundXrp=reserve(env, 2) + 1 * f,    .bookAmount=0, .preTrust=PreTrustType::AcctPreTrust, .offerAmount=1000,             .tec=tesSUCCESS,           .spentXrp=2 * f, .balanceUsd=usd(      0),    .offers=1, .owners=2},
            {.account="xan",             .fundXrp=reserve(env, 2) + 1 * f,    .bookAmount=1, .preTrust=PreTrustType::AcctPreTrust, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP( 1) + 2 * f, .balanceUsd=usd(      1),    .offers=1, .owners=2},
        };
        // clang-format on

        for (auto const& t : tests)
        {
            auto const acct = Account(t.account);
            env.fund(t.fundXrp, acct);
            env.close();

            // Make sure gateway has no current offers.
            env.require(offers(gw, 0));

            // The gateway optionally creates an offer that would be crossed.
            auto const book = t.bookAmount;
            if (book != 0)
                env(offer(gw, XRP(book), usd(book)));
            env.close();
            std::uint32_t const gwOfferSeq = env.seq(gw) - 1;

            // Optionally pre-establish a trustline between gw and acct.
            if (t.preTrust == PreTrustType::GwPreTrust)
                env(trust(gw, acct["USD"](1)));
            env.close();

            // Optionally pre-establish a trustline between acct and gw.
            // Note this is not really part of the test, so we expect there
            // to be enough XRP reserve for acct to create the trust line.
            if (t.preTrust == PreTrustType::AcctPreTrust)
                env(trust(acct, usd(1)));
            env.close();

            {
                // Acct creates an offer.  This is the heart of the test.
                auto const acctOffer = t.offerAmount;
                env(offer(acct, usd(acctOffer), XRP(acctOffer)), Ter(t.tec));
                env.close();
            }
            std::uint32_t const acctOfferSeq = env.seq(acct) - 1;

            BEAST_EXPECT(env.balance(acct, usd) == t.balanceUsd);
            BEAST_EXPECT(env.balance(acct, xrpIssue()) == t.fundXrp - t.spentXrp);
            env.require(offers(acct, t.offers));
            env.require(Owners(acct, t.owners));

            auto acctOffers = offersOnAccount(env, acct);
            BEAST_EXPECT(acctOffers.size() == t.offers);
            if (!acctOffers.empty() && (t.offers != 0))
            {
                auto const& acctOffer = *(acctOffers.front());

                auto const leftover = t.offerAmount - t.bookAmount;
                BEAST_EXPECT(acctOffer[sfTakerGets] == XRP(leftover));
                BEAST_EXPECT(acctOffer[sfTakerPays] == usd(leftover));
            }

            if (t.preTrust == PreTrustType::NoPreTrust)
            {
                if (t.balanceUsd.value().signum() != 0)
                {
                    // Verify the correct contents of the trustline
                    verifyDefaultTrustline(env, acct, t.balanceUsd);
                }
                else
                {
                    // Verify that no trustline was created.
                    auto const sleTrust = env.le(keylet::line(acct, usd));
                    BEAST_EXPECT(!sleTrust);
                }
            }

            // Give the next loop a clean slate by canceling any left-overs
            // in the offers.
            env(offerCancel(acct, acctOfferSeq));
            env(offerCancel(gw, gwOfferSeq));
            env.close();
        }
    }

    void
    testXRPDirectCross(FeatureBitset features)
    {
        testcase("XRP Direct Crossing");

        using namespace jtx;

        auto const gw = Account("gateway");
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const usd = gw["USD"];

        auto const usdOffer = usd(1000);
        auto const xrpOffer = XRP(1000);

        Env env{*this, features};

        env.fund(XRP(1000000), gw, bob);
        env.close();

        // The fee that's charged for transactions.
        auto const fee = env.current()->fees().base;

        // alice's account has enough for the reserve, one trust line plus two
        // offers, and two fees.
        env.fund(reserve(env, 2) + fee * 2, alice);
        env.close();

        env(trust(alice, usdOffer));

        env.close();

        env(pay(gw, alice, usdOffer));
        env.close();
        env.require(Balance(alice, usdOffer), offers(alice, 0), offers(bob, 0));

        // The scenario:
        //   o alice has USD but wants XRP.
        //   o bob has XRP but wants USD.
        auto const aliceXRP = env.balance(alice);
        auto const bobXRP = env.balance(bob);

        env(offer(alice, xrpOffer, usdOffer));
        env.close();
        env(offer(bob, usdOffer, xrpOffer));

        env.close();
        env.require(
            Balance(alice, usd(0)),
            Balance(bob, usdOffer),
            Balance(alice, aliceXRP + xrpOffer - fee),
            Balance(bob, bobXRP - xrpOffer - fee),
            offers(alice, 0),
            offers(bob, 0));

        verifyDefaultTrustline(env, bob, usdOffer);

        // Make two more offers that leave one of the offers non-dry.
        env(offer(alice, usd(999), XRP(999)));
        env(offer(bob, xrpOffer, usdOffer));

        env.close();
        env.require(Balance(alice, usd(999)));
        env.require(Balance(bob, usd(1)));
        env.require(offers(alice, 0));
        verifyDefaultTrustline(env, bob, usd(1));
        {
            auto const bobOffers = offersOnAccount(env, bob);
            BEAST_EXPECT(bobOffers.size() == 1);
            auto const& bobOffer = *(bobOffers.front());

            BEAST_EXPECT(bobOffer[sfLedgerEntryType] == ltOFFER);
            BEAST_EXPECT(bobOffer[sfTakerGets] == usd(1));
            BEAST_EXPECT(bobOffer[sfTakerPays] == XRP(1));
        }
    }

    void
    testDirectCross(FeatureBitset features)
    {
        testcase("Direct Crossing");

        using namespace jtx;

        auto const gw = Account("gateway");
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        auto const usdOffer = usd(1000);
        auto const eurOffer = eur(1000);

        Env env{*this, features};

        env.fund(XRP(1000000), gw);
        env.close();

        // The fee that's charged for transactions.
        auto const fee = env.current()->fees().base;

        // Each account has enough for the reserve, two trust lines, one
        // offer, and two fees.
        env.fund(reserve(env, 3) + fee * 3, alice);
        env.fund(reserve(env, 3) + fee * 2, bob);
        env.close();

        env(trust(alice, usdOffer));
        env(trust(bob, eurOffer));
        env.close();

        env(pay(gw, alice, usdOffer));
        env(pay(gw, bob, eurOffer));
        env.close();

        env.require(Balance(alice, usdOffer), Balance(bob, eurOffer));

        // The scenario:
        //   o alice has USD but wants EUR.
        //   o bob has EUR but wants USD.
        env(offer(alice, eurOffer, usdOffer));
        env(offer(bob, usdOffer, eurOffer));

        env.close();
        env.require(
            Balance(alice, eurOffer), Balance(bob, usdOffer), offers(alice, 0), offers(bob, 0));

        // Alice's offer crossing created a default EUR trustline and
        // Bob's offer crossing created a default USD trustline:
        verifyDefaultTrustline(env, alice, eurOffer);
        verifyDefaultTrustline(env, bob, usdOffer);

        // Make two more offers that leave one of the offers non-dry.
        // Guarantee the order of application by putting a close()
        // between them.
        env(offer(bob, eurOffer, usdOffer));
        env.close();

        env(offer(alice, usd(999), eurOffer));
        env.close();

        env.require(offers(alice, 0));
        env.require(offers(bob, 1));

        env.require(Balance(alice, usd(999)));
        env.require(Balance(alice, eur(1)));
        env.require(Balance(bob, usd(1)));
        env.require(Balance(bob, eur(999)));

        {
            auto bobOffers = offersOnAccount(env, bob);
            if (BEAST_EXPECT(bobOffers.size() == 1))
            {
                auto const& bobOffer = *(bobOffers.front());

                BEAST_EXPECT(bobOffer[sfTakerGets] == usd(1));
                BEAST_EXPECT(bobOffer[sfTakerPays] == eur(1));
            }
        }

        // alice makes one more offer that cleans out bob's offer.
        env(offer(alice, usd(1), eur(1)));
        env.close();

        env.require(Balance(alice, usd(1000)));
        env.require(Balance(alice, eur(kNone)));
        env.require(Balance(bob, usd(kNone)));
        env.require(Balance(bob, eur(1000)));
        env.require(offers(alice, 0));
        env.require(offers(bob, 0));

        // The two trustlines that were generated by offers should be gone.
        BEAST_EXPECT(!env.le(keylet::line(alice.id(), eur)));
        BEAST_EXPECT(!env.le(keylet::line(bob.id(), usd)));

        // Make two more offers that leave one of the offers non-dry. We
        // need to properly sequence the transactions:
        env(offer(alice, eur(999), usdOffer));
        env.close();

        env(offer(bob, usdOffer, eurOffer));
        env.close();

        env.require(offers(alice, 0));
        env.require(offers(bob, 0));

        env.require(Balance(alice, usd(0)));
        env.require(Balance(alice, eur(999)));
        env.require(Balance(bob, usd(1000)));
        env.require(Balance(bob, eur(1)));
    }

    void
    testBridgedCross(FeatureBitset features)
    {
        testcase("Bridged Crossing");

        using namespace jtx;

        auto const gw = Account("gateway");
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        auto const usdOffer = usd(1000);
        auto const eurOffer = eur(1000);

        Env env{*this, features};

        env.fund(XRP(1000000), gw, alice, bob, carol);
        env.close();

        env(trust(alice, usdOffer));
        env(trust(carol, eurOffer));
        env.close();
        env(pay(gw, alice, usdOffer));
        env(pay(gw, carol, eurOffer));
        env.close();

        // The scenario:
        //   o alice has USD but wants XRP.
        //   o bob has XRP but wants EUR.
        //   o carol has EUR but wants USD.
        // Note that carol's offer must come last.  If carol's offer is placed
        // before bob's or alice's, then autobridging will not occur.
        env(offer(alice, XRP(1000), usdOffer));
        env(offer(bob, eurOffer, XRP(1000)));
        auto const bobXrpBalance = env.balance(bob);
        env.close();

        // carol makes an offer that partially consumes alice and bob's offers.
        env(offer(carol, usd(400), eur(400)));
        env.close();

        env.require(
            Balance(alice, usd(600)),
            Balance(bob, eur(400)),
            Balance(carol, usd(400)),
            Balance(bob, bobXrpBalance - XRP(400)),
            offers(carol, 0));
        verifyDefaultTrustline(env, bob, eur(400));
        verifyDefaultTrustline(env, carol, usd(400));
        {
            auto const aliceOffers = offersOnAccount(env, alice);
            BEAST_EXPECT(aliceOffers.size() == 1);
            auto const& aliceOffer = *(aliceOffers.front());

            BEAST_EXPECT(aliceOffer[sfLedgerEntryType] == ltOFFER);
            BEAST_EXPECT(aliceOffer[sfTakerGets] == usd(600));
            BEAST_EXPECT(aliceOffer[sfTakerPays] == XRP(600));
        }
        {
            auto const bobOffers = offersOnAccount(env, bob);
            BEAST_EXPECT(bobOffers.size() == 1);
            auto const& bobOffer = *(bobOffers.front());

            BEAST_EXPECT(bobOffer[sfLedgerEntryType] == ltOFFER);
            BEAST_EXPECT(bobOffer[sfTakerGets] == XRP(600));
            BEAST_EXPECT(bobOffer[sfTakerPays] == eur(600));
        }

        // carol makes an offer that exactly consumes alice and bob's offers.
        env(offer(carol, usd(600), eur(600)));
        env.close();

        env.require(
            Balance(alice, usd(0)),
            Balance(bob, eurOffer),
            Balance(carol, usdOffer),
            Balance(bob, bobXrpBalance - XRP(1000)),
            offers(bob, 0),
            offers(carol, 0));
        verifyDefaultTrustline(env, bob, eur(1000));
        verifyDefaultTrustline(env, carol, usd(1000));

        // In pre-flow code alice's offer is left empty in the ledger.
        auto const aliceOffers = offersOnAccount(env, alice);
        if (!aliceOffers.empty())
        {
            BEAST_EXPECT(aliceOffers.size() == 1);
            auto const& aliceOffer = *(aliceOffers.front());

            BEAST_EXPECT(aliceOffer[sfLedgerEntryType] == ltOFFER);
            BEAST_EXPECT(aliceOffer[sfTakerGets] == usd(0));
            BEAST_EXPECT(aliceOffer[sfTakerPays] == XRP(0));
        }
    }

    void
    testSellOffer(FeatureBitset features)
    {
        // Test a number of different corner cases regarding offer crossing
        // when the tfSell flag is set.  The test is table driven so it
        // should be easy to add or remove tests.
        testcase("Sell Offer");

        using namespace jtx;

        auto const gw = Account("gateway");
        auto const usd = gw["USD"];

        Env env{*this, features};

        env.fund(XRP(10000000), gw);
        env.close();

        // The fee that's charged for transactions
        auto const f = env.current()->fees().base;

        // To keep things simple all offers are 1 : 1 for XRP : USD.
        enum class PreTrustType { NoPreTrust, GwPreTrust, AcctPreTrust };
        struct TestData
        {
            std::string account;  // Account operated on
            STAmount fundXrp;     // XRP acct funded with
            STAmount fundUSD;     // USD acct funded with
            STAmount gwGets;      // gw's offer
            STAmount gwPays;      //
            STAmount acctGets;    // acct's offer
            STAmount acctPays;    //
            TER tec;              // Returned tec code
            STAmount spentXrp;    // Amount removed from fundXrp
            STAmount finalUsd;    // Final USD balance on acct
            int offers;           // Offers on acct
            int owners;           // Owners on acct
            STAmount takerGets;   // Remainder of acct's offer
            STAmount takerPays;   //

            // Constructor with takerGets/takerPays
            TestData(
                std::string&& account,  // Account operated on
                STAmount fundXrp,       // XRP acct funded with
                STAmount fundUsd,       // USD acct funded with
                STAmount gwGets,        // gw's offer
                STAmount gwPays,        //
                STAmount acctGets,      // acct's offer
                STAmount acctPays,      //
                TER tec,                // Returned tec code
                STAmount spentXrp,      // Amount removed from fundXrp
                STAmount finalUsd,      // Final USD balance on acct
                int offers,             // Offers on acct
                int owners,             // Owners on acct
                STAmount takerGets,     // Remainder of acct's offer
                STAmount takerPays)     //
                : account(std::move(account))
                , fundXrp(std::move(fundXrp))
                , fundUSD(std::move(fundUsd))
                , gwGets(std::move(gwGets))
                , gwPays(std::move(gwPays))
                , acctGets(std::move(acctGets))
                , acctPays(std::move(acctPays))
                , tec(tec)
                , spentXrp(std::move(spentXrp))
                , finalUsd(std::move(finalUsd))
                , offers(offers)
                , owners(owners)
                , takerGets(std::move(takerGets))
                , takerPays(std::move(takerPays))
            {
            }

            // Constructor without takerGets/takerPays
            TestData(
                std::string&& account,     // Account operated on
                STAmount const& fundXrp,   // XRP acct funded with
                STAmount const& fundUsd,   // USD acct funded with
                STAmount const& gwGets,    // gw's offer
                STAmount const& gwPays,    //
                STAmount const& acctGets,  // acct's offer
                STAmount const& acctPays,  //
                TER tec,                   // Returned tec code
                STAmount const& spentXrp,  // Amount removed from fundXrp
                STAmount const& finalUsd,  // Final USD balance on acct
                int offers,                // Offers on acct
                int owners)                // Owners on acct
                : TestData(
                      std::move(account),
                      fundXrp,
                      fundUsd,
                      gwGets,
                      gwPays,
                      acctGets,
                      acctPays,
                      tec,
                      spentXrp,
                      finalUsd,
                      offers,
                      owners,
                      STAmount{0},
                      STAmount{0})
            {
            }
        };

        // clang-format off
        TestData const tests[]{
            // acct pays XRP
            // acct                           fundXrp  fundUSD   gwGets   gwPays acctGets acctPays                     tec            spentXrp  finalUSD offers  owners  takerGets  takerPays
            {"ann", XRP(10) + reserve(env, 0) + 1 * f, usd( 0), XRP(10), usd( 5), usd(10), XRP(10), tecINSUF_RESERVE_OFFER, XRP(  0) + (1 * f), usd( 0),      0,      0},
            {"bev", XRP(10) + reserve(env, 1) + 1 * f, usd( 0), XRP(10), usd( 5), usd(10), XRP(10),             tesSUCCESS, XRP(  0) + (1 * f), usd( 0),      1,      1,   XRP(10), usd(10)},
            {"cam", XRP(10) + reserve(env, 0) + 1 * f, usd( 0), XRP(10), usd(10), usd(10), XRP(10),             tesSUCCESS, XRP( 10) + (1 * f), usd(10),      0,      1},
            {"deb", XRP(10) + reserve(env, 0) + 1 * f, usd( 0), XRP(10), usd(20), usd(10), XRP(10),             tesSUCCESS, XRP( 10) + (1 * f), usd(20),      0,      1},
            {"eve", XRP(10) + reserve(env, 0) + 1 * f, usd( 0), XRP(10), usd(20), usd( 5), XRP( 5),             tesSUCCESS, XRP(  5) + (1 * f), usd(10),      0,      1},
            {"flo", XRP(10) + reserve(env, 0) + 1 * f, usd( 0), XRP(10), usd(20), usd(20), XRP(20),             tesSUCCESS, XRP( 10) + (1 * f), usd(20),      0,      1},
            {"gay", XRP(20) + reserve(env, 1) + 1 * f, usd( 0), XRP(10), usd(20), usd(20), XRP(20),             tesSUCCESS, XRP( 10) + (1 * f), usd(20),      0,      1},
            {"hye", XRP(20) + reserve(env, 2) + 1 * f, usd( 0), XRP(10), usd(20), usd(20), XRP(20),             tesSUCCESS, XRP( 10) + (1 * f), usd(20),      1,      2,   XRP(10), usd(10)},
            // acct pays USD
            {"meg",           reserve(env, 1) + 2 * f, usd(10), usd(10), XRP( 5), XRP(10), usd(10), tecINSUF_RESERVE_OFFER, XRP(  0) + (2 * f), usd(10),      0,      1},
            {"nia",           reserve(env, 2) + 2 * f, usd(10), usd(10), XRP( 5), XRP(10), usd(10),             tesSUCCESS, XRP(  0) + (2 * f), usd(10),      1,      2,   usd(10), XRP(10)},
            {"ova",           reserve(env, 1) + 2 * f, usd(10), usd(10), XRP(10), XRP(10), usd(10),             tesSUCCESS, XRP(-10) + (2 * f), usd( 0),      0,      1},
            {"pam",           reserve(env, 1) + 2 * f, usd(10), usd(10), XRP(20), XRP(10), usd(10),             tesSUCCESS, XRP(-20) + (2 * f), usd( 0),      0,      1},
            {"qui",           reserve(env, 1) + 2 * f, usd(10), usd(20), XRP(40), XRP(10), usd(10),             tesSUCCESS, XRP(-20) + (2 * f), usd( 0),      0,      1},
            {"rae",           reserve(env, 2) + 2 * f, usd(10), usd( 5), XRP( 5), XRP(10), usd(10),             tesSUCCESS, XRP( -5) + (2 * f), usd( 5),      1,      2,   usd( 5), XRP( 5)},
            {"sue",           reserve(env, 2) + 2 * f, usd(10), usd( 5), XRP(10), XRP(10), usd(10),             tesSUCCESS, XRP(-10) + (2 * f), usd( 5),      1,      2,   usd( 5), XRP( 5)},
        };
        // clang-format on

        auto const zeroUsd = usd(0);
        for (auto const& t : tests)
        {
            // Make sure gateway has no current offers.
            env.require(offers(gw, 0));

            auto const acct = Account(t.account);

            env.fund(t.fundXrp, acct);
            env.close();

            // Optionally give acct some USD.  This is not part of the test,
            // so we assume that acct has sufficient USD to cover the reserve
            // on the trust line.
            if (t.fundUSD != zeroUsd)
            {
                env(trust(acct, t.fundUSD));
                env.close();
                env(pay(gw, acct, t.fundUSD));
                env.close();
            }

            env(offer(gw, t.gwGets, t.gwPays));
            env.close();
            std::uint32_t const gwOfferSeq = env.seq(gw) - 1;

            // Acct creates a tfSell offer.  This is the heart of the test.
            env(offer(acct, t.acctGets, t.acctPays, tfSell), Ter(t.tec));
            env.close();
            std::uint32_t const acctOfferSeq = env.seq(acct) - 1;

            // Check results
            BEAST_EXPECT(env.balance(acct, usd) == t.finalUsd);
            BEAST_EXPECT(env.balance(acct, xrpIssue()) == t.fundXrp - t.spentXrp);
            env.require(offers(acct, t.offers));
            env.require(Owners(acct, t.owners));

            if (t.offers != 0)
            {
                auto const acctOffers = offersOnAccount(env, acct);
                if (!acctOffers.empty())
                {
                    BEAST_EXPECT(acctOffers.size() == 1);
                    auto const& acctOffer = *(acctOffers.front());

                    BEAST_EXPECT(acctOffer[sfLedgerEntryType] == ltOFFER);
                    BEAST_EXPECT(acctOffer[sfTakerGets] == t.takerGets);
                    BEAST_EXPECT(acctOffer[sfTakerPays] == t.takerPays);
                }
            }

            // Give the next loop a clean slate by canceling any left-overs
            // in the offers.
            env(offerCancel(acct, acctOfferSeq));
            env(offerCancel(gw, gwOfferSeq));
            env.close();
        }
    }

    void
    testSellWithFillOrKill(FeatureBitset features)
    {
        // Test a number of different corner cases regarding offer crossing
        // when both the tfSell flag and tfFillOrKill flags are set.
        testcase("Combine tfSell with tfFillOrKill");

        using namespace jtx;

        auto const gw = Account("gateway");
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const usd = gw["USD"];

        Env env{*this, features};

        env.fund(XRP(10000000), gw, alice, bob);
        env.close();

        // Code returned if an offer is killed.
        TER const killedCode{TER{tecKILLED}};

        // bob offers XRP for USD.
        env(trust(bob, usd(200)));
        env.close();
        env(pay(gw, bob, usd(100)));
        env.close();
        env(offer(bob, XRP(2000), usd(20)));
        env.close();
        {
            // alice submits a tfSell | tfFillOrKill offer that does not cross.
            env(offer(alice, usd(21), XRP(2100), tfSell | tfFillOrKill), Ter(killedCode));
            env.close();
            env.require(Balance(alice, usd(kNone)));
            env.require(offers(alice, 0));
            env.require(Balance(bob, usd(100)));
        }
        {
            // alice submits a tfSell | tfFillOrKill offer that crosses.
            // Even though tfSell is present it doesn't matter this time.
            env(offer(alice, usd(20), XRP(2000), tfSell | tfFillOrKill));
            env.close();
            env.require(Balance(alice, usd(20)));
            env.require(offers(alice, 0));
            env.require(Balance(bob, usd(80)));
        }
        {
            // alice submits a tfSell | tfFillOrKill offer that crosses and
            // returns more than was asked for (because of the tfSell flag).
            env(offer(bob, XRP(2000), usd(20)));
            env.close();
            env(offer(alice, usd(10), XRP(1500), tfSell | tfFillOrKill));
            env.close();
            env.require(Balance(alice, usd(35)));
            env.require(offers(alice, 0));
            env.require(Balance(bob, usd(65)));
        }
        {
            // alice submits a tfSell | tfFillOrKill offer that doesn't cross.
            // This would have succeeded with a regular tfSell, but the
            // fillOrKill prevents the transaction from crossing since not
            // all of the offer is consumed.

            // We're using bob's left-over offer for XRP(500), USD(5)
            env(offer(alice, usd(1), XRP(501), tfSell | tfFillOrKill), Ter(killedCode));
            env.close();
            env.require(Balance(alice, usd(35)));
            env.require(offers(alice, 0));
            env.require(Balance(bob, usd(65)));
        }
        {
            // Alice submits a tfSell | tfFillOrKill offer that finishes
            // off the remainder of bob's offer.

            // We're using bob's left-over offer for XRP(500), USD(5)
            env(offer(alice, usd(1), XRP(500), tfSell | tfFillOrKill));
            env.close();
            env.require(Balance(alice, usd(40)));
            env.require(offers(alice, 0));
            env.require(Balance(bob, usd(60)));
        }
    }

    void
    testTransferRateOffer(FeatureBitset features)
    {
        testcase("Transfer Rate Offer");

        using namespace jtx;

        auto const gw1 = Account("gateway1");
        auto const usd = gw1["USD"];

        Env env{*this, features};

        // The fee that's charged for transactions.
        auto const fee = env.current()->fees().base;

        env.fund(XRP(100000), gw1);
        env.close();

        env(rate(gw1, 1.25));
        {
            auto const ann = Account("ann");
            auto const bob = Account("bob");
            env.fund(XRP(100) + reserve(env, 2) + (fee * 2), ann, bob);
            env.close();

            env(trust(ann, usd(200)));
            env(trust(bob, usd(200)));
            env.close();

            env(pay(gw1, bob, usd(125)));
            env.close();

            // bob offers to sell USD(100) for XRP.  alice takes bob's offer.
            // Notice that although bob only offered USD(100), USD(125) was
            // removed from his account due to the gateway fee.
            //
            // A comparable payment would look like this:
            //   env (pay (bob, alice, USD(100)), sendmax(USD(125)))
            env(offer(bob, XRP(1), usd(100)));
            env.close();

            env(offer(ann, usd(100), XRP(1)));
            env.close();

            env.require(Balance(ann, usd(100)));
            env.require(Balance(ann, XRP(99) + reserve(env, 2)));
            env.require(offers(ann, 0));

            env.require(Balance(bob, usd(0)));
            env.require(Balance(bob, XRP(101) + reserve(env, 2)));
            env.require(offers(bob, 0));
        }
        {
            // Reverse the order, so the offer in the books is to sell XRP
            // in return for USD.  Gateway rate should still apply identically.
            auto const che = Account("che");
            auto const deb = Account("deb");
            env.fund(XRP(100) + reserve(env, 2) + (fee * 2), che, deb);
            env.close();

            env(trust(che, usd(200)));
            env(trust(deb, usd(200)));
            env.close();

            env(pay(gw1, deb, usd(125)));
            env.close();

            env(offer(che, usd(100), XRP(1)));
            env.close();

            env(offer(deb, XRP(1), usd(100)));
            env.close();

            env.require(Balance(che, usd(100)));
            env.require(Balance(che, XRP(99) + reserve(env, 2)));
            env.require(offers(che, 0));

            env.require(Balance(deb, usd(0)));
            env.require(Balance(deb, XRP(101) + reserve(env, 2)));
            env.require(offers(deb, 0));
        }
        {
            auto const eve = Account("eve");
            auto const fyn = Account("fyn");

            env.fund(XRP(20000) + (fee * 2), eve, fyn);
            env.close();

            env(trust(eve, usd(1000)));
            env(trust(fyn, usd(1000)));
            env.close();

            env(pay(gw1, eve, usd(100)));
            env(pay(gw1, fyn, usd(100)));
            env.close();

            // This test verifies that the amount removed from an offer
            // accounts for the transfer fee that is removed from the
            // account but not from the remaining offer.
            env(offer(eve, usd(10), XRP(4000)));
            env.close();
            std::uint32_t const eveOfferSeq = env.seq(eve) - 1;

            env(offer(fyn, XRP(2000), usd(5)));
            env.close();

            env.require(Balance(eve, usd(105)));
            env.require(Balance(eve, XRP(18000)));
            auto const evesOffers = offersOnAccount(env, eve);
            BEAST_EXPECT(evesOffers.size() == 1);
            if (!evesOffers.empty())
            {
                auto const& evesOffer = *(evesOffers.front());
                BEAST_EXPECT(evesOffer[sfLedgerEntryType] == ltOFFER);
                BEAST_EXPECT(evesOffer[sfTakerGets] == XRP(2000));
                BEAST_EXPECT(evesOffer[sfTakerPays] == usd(5));
            }
            env(offerCancel(eve, eveOfferSeq));  // For later tests

            env.require(Balance(fyn, usd(93.75)));
            env.require(Balance(fyn, XRP(22000)));
            env.require(offers(fyn, 0));
        }
        // Start messing with two non-native currencies.
        auto const gw2 = Account("gateway2");
        auto const eur = gw2["EUR"];

        env.fund(XRP(100000), gw2);
        env.close();

        env(rate(gw2, 1.5));
        {
            // Remove XRP from the equation.  Give the two currencies two
            // different transfer rates so we can see both transfer rates
            // apply in the same transaction.
            auto const gay = Account("gay");
            auto const hal = Account("hal");
            env.fund(reserve(env, 3) + (fee * 3), gay, hal);
            env.close();

            env(trust(gay, usd(200)));
            env(trust(gay, eur(200)));
            env(trust(hal, usd(200)));
            env(trust(hal, eur(200)));
            env.close();

            env(pay(gw1, gay, usd(125)));
            env(pay(gw2, hal, eur(150)));
            env.close();

            env(offer(gay, eur(100), usd(100)));
            env.close();

            env(offer(hal, usd(100), eur(100)));
            env.close();

            env.require(Balance(gay, usd(0)));
            env.require(Balance(gay, eur(100)));
            env.require(Balance(gay, reserve(env, 3)));
            env.require(offers(gay, 0));

            env.require(Balance(hal, usd(100)));
            env.require(Balance(hal, eur(0)));
            env.require(Balance(hal, reserve(env, 3)));
            env.require(offers(hal, 0));
        }
        {
            // A trust line's QualityIn should not affect offer crossing.
            auto const ivy = Account("ivy");
            auto const joe = Account("joe");
            env.fund(reserve(env, 3) + (fee * 3), ivy, joe);
            env.close();

            env(trust(ivy, usd(400)), QualityInPercent(90));
            env(trust(ivy, eur(400)), QualityInPercent(80));
            env(trust(joe, usd(400)), QualityInPercent(70));
            env(trust(joe, eur(400)), QualityInPercent(60));
            env.close();

            env(pay(gw1, ivy, usd(270)), Sendmax(usd(500)));
            env(pay(gw2, joe, eur(150)), Sendmax(eur(300)));
            env.close();
            env.require(Balance(ivy, usd(300)));
            env.require(Balance(joe, eur(250)));

            env(offer(ivy, eur(100), usd(200)));
            env.close();

            env(offer(joe, usd(200), eur(100)));
            env.close();

            env.require(Balance(ivy, usd(50)));
            env.require(Balance(ivy, eur(100)));
            env.require(Balance(ivy, reserve(env, 3)));
            env.require(offers(ivy, 0));

            env.require(Balance(joe, usd(200)));
            env.require(Balance(joe, eur(100)));
            env.require(Balance(joe, reserve(env, 3)));
            env.require(offers(joe, 0));
        }
        {
            // A trust line's QualityOut should not affect offer crossing.
            auto const kim = Account("kim");
            auto const kBux = kim["BUX"];
            auto const lex = Account("lex");
            auto const meg = Account("meg");
            auto const ned = Account("ned");
            auto const nBux = ned["BUX"];

            // Verify trust line QualityOut affects payments.
            env.fund(reserve(env, 4) + (fee * 4), kim, lex, meg, ned);
            env.close();

            env(trust(lex, kBux(400)));
            env(trust(lex, nBux(200)), QualityOutPercent(120));
            env(trust(meg, nBux(100)));
            env.close();
            env(pay(ned, lex, nBux(100)));
            env.close();
            env.require(Balance(lex, nBux(100)));

            env(pay(kim, meg, nBux(60)), Path(lex, ned), Sendmax(kBux(200)));
            env.close();

            env.require(Balance(kim, kBux(kNone)));
            env.require(Balance(kim, nBux(kNone)));
            env.require(Balance(lex, kBux(72)));
            env.require(Balance(lex, nBux(40)));
            env.require(Balance(meg, kBux(kNone)));
            env.require(Balance(meg, nBux(60)));
            env.require(Balance(ned, kBux(kNone)));
            env.require(Balance(ned, nBux(kNone)));

            // Now verify that offer crossing is unaffected by QualityOut.
            env(offer(lex, kBux(30), nBux(30)));
            env.close();

            env(offer(kim, nBux(30), kBux(30)));
            env.close();

            env.require(Balance(kim, kBux(kNone)));
            env.require(Balance(kim, nBux(30)));
            env.require(Balance(lex, kBux(102)));
            env.require(Balance(lex, nBux(10)));
            env.require(Balance(meg, kBux(kNone)));
            env.require(Balance(meg, nBux(60)));
            env.require(Balance(ned, kBux(-30)));
            env.require(Balance(ned, nBux(kNone)));
        }
        {
            // Make sure things work right when we're auto-bridging as well.
            auto const ova = Account("ova");
            auto const pat = Account("pat");
            auto const qae = Account("qae");
            env.fund(XRP(2) + reserve(env, 3) + (fee * 3), ova, pat, qae);
            env.close();

            //   o ova has USD but wants XRP.
            //   o pat has XRP but wants EUR.
            //   o qae has EUR but wants USD.
            env(trust(ova, usd(200)));
            env(trust(ova, eur(200)));
            env(trust(pat, usd(200)));
            env(trust(pat, eur(200)));
            env(trust(qae, usd(200)));
            env(trust(qae, eur(200)));
            env.close();

            env(pay(gw1, ova, usd(125)));
            env(pay(gw2, qae, eur(150)));
            env.close();

            env(offer(ova, XRP(2), usd(100)));
            env(offer(pat, eur(100), XRP(2)));
            env.close();

            env(offer(qae, usd(100), eur(100)));
            env.close();

            env.require(Balance(ova, usd(0)));
            env.require(Balance(ova, eur(0)));
            env.require(Balance(ova, XRP(4) + reserve(env, 3)));

            // In pre-flow code ova's offer is left empty in the ledger.
            auto const ovasOffers = offersOnAccount(env, ova);
            if (!ovasOffers.empty())
            {
                BEAST_EXPECT(ovasOffers.size() == 1);
                auto const& ovasOffer = *(ovasOffers.front());

                BEAST_EXPECT(ovasOffer[sfLedgerEntryType] == ltOFFER);
                BEAST_EXPECT(ovasOffer[sfTakerGets] == usd(0));
                BEAST_EXPECT(ovasOffer[sfTakerPays] == XRP(0));
            }

            env.require(Balance(pat, usd(0)));
            env.require(Balance(pat, eur(100)));
            env.require(Balance(pat, XRP(0) + reserve(env, 3)));
            env.require(offers(pat, 0));

            env.require(Balance(qae, usd(100)));
            env.require(Balance(qae, eur(0)));
            env.require(Balance(qae, XRP(2) + reserve(env, 3)));
            env.require(offers(qae, 0));
        }
    }

    void
    testSelfCrossOffer1(FeatureBitset features)
    {
        // The following test verifies some correct but slightly surprising
        // behavior in offer crossing.  The scenario:
        //
        //  o An entity has created one or more offers.
        //  o The entity creates another offer that can be directly crossed
        //    (not autobridged) by the previously created offer(s).
        //  o Rather than self crossing the offers, delete the old offer(s).
        //
        // See a more complete explanation in the comments for
        // BookOfferCrossingStep::limitSelfCrossQuality().
        //
        // Note that, in this particular example, one offer causes several
        // crossable offers (worth considerably more than the new offer)
        // to be removed from the book.
        using namespace jtx;

        auto const gw = Account("gateway");
        auto const usd = gw["USD"];

        Env env{*this, features};

        // The fee that's charged for transactions.
        auto const fee = env.current()->fees().base;
        auto const startBalance = XRP(1000000);

        env.fund(startBalance + (fee * 4), gw);
        env.close();

        env(offer(gw, usd(60), XRP(600)));
        env.close();
        env(offer(gw, usd(60), XRP(600)));
        env.close();
        env(offer(gw, usd(60), XRP(600)));
        env.close();

        env.require(Owners(gw, 3));
        env.require(Balance(gw, startBalance + fee));

        auto gwOffers = offersOnAccount(env, gw);
        BEAST_EXPECT(gwOffers.size() == 3);
        for (auto const& offerPtr : gwOffers)
        {
            auto const& offer = *offerPtr;
            BEAST_EXPECT(offer[sfLedgerEntryType] == ltOFFER);
            BEAST_EXPECT(offer[sfTakerGets] == XRP(600));
            BEAST_EXPECT(offer[sfTakerPays] == usd(60));
        }

        // Since this offer crosses the first offers, the previous offers
        // will be deleted and this offer will be put on the order book.
        env(offer(gw, XRP(1000), usd(100)));
        env.close();
        env.require(Owners(gw, 1));
        env.require(offers(gw, 1));
        env.require(Balance(gw, startBalance));

        gwOffers = offersOnAccount(env, gw);
        BEAST_EXPECT(gwOffers.size() == 1);
        for (auto const& offerPtr : gwOffers)
        {
            auto const& offer = *offerPtr;
            BEAST_EXPECT(offer[sfLedgerEntryType] == ltOFFER);
            BEAST_EXPECT(offer[sfTakerGets] == usd(100));
            BEAST_EXPECT(offer[sfTakerPays] == XRP(1000));
        }
    }

    void
    testSelfCrossOffer2(FeatureBitset features)
    {
        using namespace jtx;

        auto const gw1 = Account("gateway1");
        auto const gw2 = Account("gateway2");
        auto const alice = Account("alice");
        auto const usd = gw1["USD"];
        auto const eur = gw2["EUR"];

        Env env{*this, features};

        env.fund(XRP(1000000), gw1, gw2);
        env.close();

        // The fee that's charged for transactions.
        auto const f = env.current()->fees().base;

        // Test cases
        struct TestData
        {
            std::string acct;    // Account operated on
            STAmount fundXRP;    // XRP acct funded with
            STAmount fundUSD;    // USD acct funded with
            STAmount fundEUR;    // EUR acct funded with
            TER firstOfferTec;   // tec code on first offer
            TER secondOfferTec;  // tec code on second offer
        };

        // clang-format off
        TestData const tests[]{
            // acct                 fundXRP   fundUSD    fundEUR            firstOfferTec           secondOfferTec
            {.acct="ann", .fundXRP=reserve(env, 3) + f * 4, .fundUSD=usd(1000), .fundEUR=eur(1000),             .firstOfferTec=tesSUCCESS,             .secondOfferTec=tesSUCCESS},
            {.acct="bev", .fundXRP=reserve(env, 3) + f * 4, .fundUSD=usd(   1), .fundEUR=eur(1000),             .firstOfferTec=tesSUCCESS,             .secondOfferTec=tesSUCCESS},
            {.acct="cam", .fundXRP=reserve(env, 3) + f * 4, .fundUSD=usd(1000), .fundEUR=eur(   1),             .firstOfferTec=tesSUCCESS,             .secondOfferTec=tesSUCCESS},
            {.acct="deb", .fundXRP=reserve(env, 3) + f * 4, .fundUSD=usd(   0), .fundEUR=eur(   1),             .firstOfferTec=tesSUCCESS,      .secondOfferTec=tecUNFUNDED_OFFER},
            {.acct="eve", .fundXRP=reserve(env, 3) + f * 4, .fundUSD=usd(   1), .fundEUR=eur(   0),      .firstOfferTec=tecUNFUNDED_OFFER,             .secondOfferTec=tesSUCCESS},
            {.acct="flo", .fundXRP=reserve(env, 3) +     0, .fundUSD=usd(1000), .fundEUR=eur(1000), .firstOfferTec=tecINSUF_RESERVE_OFFER, .secondOfferTec=tecINSUF_RESERVE_OFFER},
        };
        //clang-format on

        for (auto const& t : tests)
        {
            auto const acct = Account{t.acct};
            env.fund(t.fundXRP, acct);
            env.close();

            env(trust(acct, usd(1000)));
            env(trust(acct, eur(1000)));
            env.close();

            if (t.fundUSD > usd(0))
                env(pay(gw1, acct, t.fundUSD));
            if (t.fundEUR > eur(0))
                env(pay(gw2, acct, t.fundEUR));
            env.close();

            env(offer(acct, usd(500), eur(600)), Ter(t.firstOfferTec));
            env.close();
            std::uint32_t const firstOfferSeq = env.seq(acct) - 1;

            int offerCount = isTesSuccess(t.firstOfferTec) ? 1 : 0;
            env.require(Owners(acct, 2 + offerCount));
            env.require(Balance(acct, t.fundUSD));
            env.require(Balance(acct, t.fundEUR));

            auto acctOffers = offersOnAccount(env, acct);
            BEAST_EXPECT(acctOffers.size() == offerCount);
            for (auto const& offerPtr : acctOffers)
            {
                auto const& offer = *offerPtr;
                BEAST_EXPECT(offer[sfLedgerEntryType] == ltOFFER);
                BEAST_EXPECT(offer[sfTakerGets] == eur(600));
                BEAST_EXPECT(offer[sfTakerPays] == usd(500));
            }

            env(offer(acct, eur(600), usd(500)), Ter(t.secondOfferTec));
            env.close();
            std::uint32_t const secondOfferSeq = env.seq(acct) - 1;

            offerCount = isTesSuccess(t.secondOfferTec) ? 1 : offerCount;
            env.require(Owners(acct, 2 + offerCount));
            env.require(Balance(acct, t.fundUSD));
            env.require(Balance(acct, t.fundEUR));

            acctOffers = offersOnAccount(env, acct);
            BEAST_EXPECT(acctOffers.size() == offerCount);
            for (auto const& offerPtr : acctOffers)
            {
                auto const& offer = *offerPtr;
                BEAST_EXPECT(offer[sfLedgerEntryType] == ltOFFER);
                if (offer[sfSequence] == firstOfferSeq)
                {
                    BEAST_EXPECT(offer[sfTakerGets] == eur(600));
                    BEAST_EXPECT(offer[sfTakerPays] == usd(500));
                }
                else
                {
                    BEAST_EXPECT(offer[sfTakerGets] == usd(500));
                    BEAST_EXPECT(offer[sfTakerPays] == eur(600));
                }
            }

            // Remove any offers from acct for the next pass.
            env(offerCancel(acct, firstOfferSeq));
            env.close();
            env(offerCancel(acct, secondOfferSeq));
            env.close();
        }
    }

    void
    testSelfCrossOffer(FeatureBitset features)
    {
        testcase("Self Cross Offer");
        testSelfCrossOffer1(features);
        testSelfCrossOffer2(features);
    }

    void
    testSelfIssueOffer(FeatureBitset features)
    {
        // Folks who issue their own currency have, in effect, as many
        // funds as they are trusted for.  This test used to fail because
        // self-issuing was not properly checked.  Verify that it works
        // correctly now.
        using namespace jtx;

        Env env{*this, features};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const usd = bob["USD"];
        auto const f = env.current()->fees().base;

        env.fund(XRP(50000) + f, alice, bob);
        env.close();

        env(offer(alice, usd(5000), XRP(50000)));
        env.close();

        // This offer should take alice's offer up to Alice's reserve.
        env(offer(bob, XRP(50000), usd(5000)));
        env.close();

        // alice's offer should have been removed, since she's down to her
        // XRP reserve.
        env.require(Balance(alice, XRP(250)));
        env.require(Owners(alice, 1));
        env.require(lines(alice, 1));

        // However bob's offer should be in the ledger, since it was not
        // fully crossed.
        auto const bobOffers = offersOnAccount(env, bob);
        BEAST_EXPECT(bobOffers.size() == 1);
        for (auto const& offerPtr : bobOffers)
        {
            auto const& offer = *offerPtr;
            BEAST_EXPECT(offer[sfLedgerEntryType] == ltOFFER);
            BEAST_EXPECT(offer[sfTakerGets] == usd(25));
            BEAST_EXPECT(offer[sfTakerPays] == XRP(250));
        }
    }

    void
    testBadPathAssert(FeatureBitset features)
    {
        // At one point in the past this invalid path caused an assert.  It
        // should not be possible for user-supplied data to cause an assert.
        // Make sure the assert is gone.
        testcase("Bad path assert");

        using namespace jtx;

        Env env{*this, features};

        // The fee that's charged for transactions.
        auto const fee = env.current()->fees().base;
        {
            // A trust line's QualityOut should not affect offer crossing.
            auto const ann = Account("ann");
            auto const aBux = ann["BUX"];
            auto const bob = Account("bob");
            auto const cam = Account("cam");
            auto const dan = Account("dan");
            auto const dBux = dan["BUX"];

            // Verify trust line QualityOut affects payments.
            env.fund(reserve(env, 4) + (fee * 4), ann, bob, cam, dan);
            env.close();

            env(trust(bob, aBux(400)));
            env(trust(bob, dBux(200)), QualityOutPercent(120));
            env(trust(cam, dBux(100)));
            env.close();
            env(pay(dan, bob, dBux(100)));
            env.close();
            env.require(Balance(bob, dBux(100)));

            env(pay(ann, cam, dBux(60)), Path(bob, dan), Sendmax(aBux(200)));
            env.close();

            env.require(Balance(ann, aBux(kNone)));
            env.require(Balance(ann, dBux(kNone)));
            env.require(Balance(bob, aBux(72)));
            env.require(Balance(bob, dBux(40)));
            env.require(Balance(cam, aBux(kNone)));
            env.require(Balance(cam, dBux(60)));
            env.require(Balance(dan, aBux(kNone)));
            env.require(Balance(dan, dBux(kNone)));

            env(offer(bob, aBux(30), dBux(30)));
            env.close();

            env(trust(ann, dBux(100)));
            env.close();

            // This payment caused the assert.
            env(pay(ann, ann, dBux(30)),
                Path(aBux, dBux),
                Sendmax(aBux(30)),
                Ter(temBAD_PATH));
            env.close();

            env.require(Balance(ann, aBux(kNone)));
            env.require(Balance(ann, dBux(0)));
            env.require(Balance(bob, aBux(72)));
            env.require(Balance(bob, dBux(40)));
            env.require(Balance(cam, aBux(kNone)));
            env.require(Balance(cam, dBux(60)));
            env.require(Balance(dan, aBux(0)));
            env.require(Balance(dan, dBux(kNone)));
        }
    }

    void
    testDirectToDirectPath(FeatureBitset features)
    {
        // The offer crossing code expects that a DirectStep is always
        // preceded by a BookStep.  In one instance the default path
        // was not matching that assumption.  Here we recreate that case
        // so we can prove the bug stays fixed.
        testcase("Direct to Direct path");

        using namespace jtx;

        Env env{*this, features};

        auto const ann = Account("ann");
        auto const bob = Account("bob");
        auto const cam = Account("cam");
        auto const aBux = ann["BUX"];
        auto const bBux = bob["BUX"];

        auto const fee = env.current()->fees().base;
        env.fund(reserve(env, 4) + (fee * 5), ann, bob, cam);
        env.close();

        env(trust(ann, bBux(40)));
        env(trust(cam, aBux(40)));
        env(trust(cam, bBux(40)));
        env.close();

        env(pay(ann, cam, aBux(35)));
        env(pay(bob, cam, bBux(35)));

        env(offer(bob, aBux(30), bBux(30)));
        env.close();

        // cam puts an offer on the books that her upcoming offer could cross.
        // But this offer should be deleted, not crossed, by her upcoming
        // offer.
        env(offer(cam, aBux(29), bBux(30), tfPassive));
        env.close();
        env.require(Balance(cam, aBux(35)));
        env.require(Balance(cam, bBux(35)));
        env.require(offers(cam, 1));

        // This offer caused the assert.
        env(offer(cam, bBux(30), aBux(30)));
        env.close();

        env.require(Balance(bob, aBux(30)));
        env.require(Balance(cam, aBux(5)));
        env.require(Balance(cam, bBux(65)));
        env.require(offers(cam, 0));
    }

    void
    testSelfCrossLowQualityOffer(FeatureBitset features)
    {
        // The Flow offer crossing code used to assert if an offer was made
        // for more XRP than the offering account held.  This unit test
        // reproduces that failing case.
        testcase("Self crossing low quality offer");

        using namespace jtx;

        Env env{*this, features};

        auto const ann = Account("ann");
        auto const gw = Account("gateway");
        auto const btc = gw["BTC"];

        auto const fee = env.current()->fees().base;
        env.fund(reserve(env, 2) + drops(9999640) + (fee), ann);
        env.fund(reserve(env, 2) + (fee * 4), gw);
        env.close();

        env(rate(gw, 1.002));
        env(trust(ann, btc(10)));
        env.close();

        env(pay(gw, ann, btc(2.856)));
        env.close();

        env(offer(ann, drops(365611702030), btc(5.713)));
        env.close();

        // This offer caused the assert.
        env(offer(ann, btc(0.687), drops(20000000000)),
            Ter(tecINSUF_RESERVE_OFFER));
    }

    void
    testOfferInScaling(FeatureBitset features)
    {
        // The Flow offer crossing code had a case where it was not rounding
        // the offer crossing correctly after a partial crossing.  The
        // failing case was found on the network.  Here we add the case to
        // the unit tests.
        testcase("Offer In Scaling");

        using namespace jtx;

        Env env{*this, features};

        auto const gw = Account("gateway");
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const cny = gw["CNY"];

        auto const fee = env.current()->fees().base;
        env.fund(reserve(env, 2) + drops(400000000000) + (fee), alice, bob);
        env.fund(reserve(env, 2) + (fee * 4), gw);
        env.close();

        env(trust(bob, cny(500)));
        env.close();

        env(pay(gw, bob, cny(300)));
        env.close();

        env(offer(bob, drops(5400000000), cny(216.054)));
        env.close();

        // This offer did not round result of partial crossing correctly.
        env(offer(alice, cny(13562.0001), drops(339000000000)));
        env.close();

        auto const aliceOffers = offersOnAccount(env, alice);
        BEAST_EXPECT(aliceOffers.size() == 1);
        for (auto const& offerPtr : aliceOffers)
        {
            auto const& offer = *offerPtr;
            BEAST_EXPECT(offer[sfLedgerEntryType] == ltOFFER);
            BEAST_EXPECT(offer[sfTakerGets] == drops(333599446582));
            BEAST_EXPECT(offer[sfTakerPays] == cny(13345.9461));
        }
    }

    void
    testOfferInScalingWithXferRate(FeatureBitset features)
    {
        // After adding the previous case, there were still failing rounding
        // cases in Flow offer crossing.  This one was because the gateway
        // transfer rate was not being correctly handled.
        testcase("Offer In Scaling With Xfer Rate");

        using namespace jtx;

        Env env{*this, features};

        auto const gw = Account("gateway");
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const btc = gw["BTC"];
        auto const jpy = gw["JPY"];

        auto const fee = env.current()->fees().base;
        env.fund(reserve(env, 2) + drops(400000000000) + (fee), alice, bob);
        env.fund(reserve(env, 2) + (fee * 4), gw);
        env.close();

        env(rate(gw, 1.002));
        env(trust(alice, jpy(4000)));
        env(trust(bob, btc(2)));
        env.close();

        env(pay(gw, alice, jpy(3699.034802280317)));
        env(pay(gw, bob, btc(1.156722559140311)));
        env.close();

        env(offer(bob, jpy(1241.913390770747), btc(0.01969825690469254)));
        env.close();

        // This offer did not round result of partial crossing correctly.
        env(offer(alice, btc(0.05507568706427876), jpy(3472.696773391072)));
        env.close();

        auto const aliceOffers = offersOnAccount(env, alice);
        BEAST_EXPECT(aliceOffers.size() == 1);
        for (auto const& offerPtr : aliceOffers)
        {
            auto const& offer = *offerPtr;
            BEAST_EXPECT(offer[sfLedgerEntryType] == ltOFFER);
            BEAST_EXPECT(
                offer[sfTakerGets] ==
                STAmount(jpy, std::uint64_t(2230682446713524ul), -12));
            BEAST_EXPECT(offer[sfTakerPays] == btc(0.035378));
        }
    }

    void
    testOfferThresholdWithReducedFunds(FeatureBitset features)
    {
        // Another instance where Flow offer crossing was not always
        // working right was if the Taker had fewer funds than the Offer
        // was offering.  The basis for this test came off the network.
        testcase("Offer Threshold With Reduced Funds");

        using namespace jtx;

        Env env{*this, features};

        auto const gw1 = Account("gw1");
        auto const gw2 = Account("gw2");
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const usd = gw1["USD"];
        auto const jpy = gw2["JPY"];

        auto const fee = env.current()->fees().base;
        env.fund(reserve(env, 2) + drops(400000000000) + (fee), alice, bob);
        env.fund(reserve(env, 2) + (fee * 4), gw1, gw2);
        env.close();

        env(rate(gw1, 1.002));
        env(trust(alice, usd(1000)));
        env(trust(bob, jpy(100000)));
        env.close();

        env(
            pay(gw1,
                alice,
                STAmount{usd, std::uint64_t(2185410179555600), -14}));
        env(
            pay(gw2,
                bob,
                STAmount{jpy, std::uint64_t(6351823459548956), -12}));
        env.close();

        env(offer(
            bob,
            STAmount{usd, std::uint64_t(4371257532306000), -17},
            STAmount{jpy, std::uint64_t(4573216636606000), -15}));
        env.close();

        // This offer did not partially cross correctly.
        env(offer(
            alice,
            STAmount{jpy, std::uint64_t(2291181510070762), -12},
            STAmount{usd, std::uint64_t(2190218999914694), -14}));
        env.close();

        auto const aliceOffers = offersOnAccount(env, alice);
        BEAST_EXPECT(aliceOffers.size() == 1);
        for (auto const& offerPtr : aliceOffers)
        {
            auto const& offer = *offerPtr;
            BEAST_EXPECT(offer[sfLedgerEntryType] == ltOFFER);
            BEAST_EXPECT(
                offer[sfTakerGets] ==
                STAmount(usd, std::uint64_t(2185847305256635), -14));
            BEAST_EXPECT(
                offer[sfTakerPays] ==
                STAmount(jpy, std::uint64_t(2286608293434156), -12));
        }
    }

    void
    testTinyOffer(FeatureBitset features)
    {
        testcase("Tiny Offer");

        using namespace jtx;

        Env env{*this, features};

        auto const gw = Account("gw");
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const cny = gw["CNY"];
        auto const fee = env.current()->fees().base;
        auto const startXrpBalance = drops(400000000000) + (fee * 2);

        env.fund(startXrpBalance, gw, alice, bob);
        env.close();

        env(trust(bob, cny(100000)));
        env.close();

        // Place alice's tiny offer in the book first.  Let's see what happens
        // when a reasonable offer crosses it.
        STAmount const aliceCnyOffer{
            cny, std::uint64_t(4926000000000000), -23};

        env(offer(alice, aliceCnyOffer, drops(1), tfPassive));
        env.close();

        // bob places an ordinary offer
        STAmount const bobCnyStartBalance{
            cny, std::uint64_t(3767479960090235), -15};
        env(pay(gw, bob, bobCnyStartBalance));
        env.close();

        env(offer(
            bob,
            drops(203),
            STAmount{cny, std::uint64_t(1000000000000000), -20}));
        env.close();

        env.require(Balance(alice, aliceCnyOffer));
        env.require(Balance(alice, startXrpBalance - fee - drops(1)));
        env.require(Balance(bob, bobCnyStartBalance - aliceCnyOffer));
        env.require(Balance(bob, startXrpBalance - (fee * 2) + drops(1)));
    }

    void
    testSelfPayXferFeeOffer(FeatureBitset features)
    {
        testcase("Self Pay Xfer Fee");
        // The old offer crossing code does not charge a transfer fee
        // if alice pays alice.  That's different from how payments work.
        // Payments always charge a transfer fee even if the money is staying
        // in the same hands.
        //
        // What's an example where alice pays alice?  There are three actors:
        // gw, alice, and bob.
        //
        //  1. gw issues BTC and USD.  qw charges a 0.2% transfer fee.
        //
        //  2. alice makes an offer to buy XRP and sell USD.
        //  3. bob makes an offer to buy BTC and sell XRP.
        //
        //  4. alice now makes an offer to sell BTC and buy USD.
        //
        // This last offer crosses using auto-bridging.
        //  o alice's last offer sells BTC to...
        //  o bob' offer which takes alice's BTC and sells XRP to...
        //  o alice's first offer which takes bob's XRP and sells USD to...
        //  o alice's last offer.
        //
        // So alice sells USD to herself.
        //
        // There are six cases that we need to test:
        //  o alice crosses her own offer on the first leg (BTC).
        //  o alice crosses her own offer on the second leg (USD).
        //  o alice crosses her own offers on both legs.
        // All three cases need to be tested:
        //  o In reverse (alice has enough BTC to cover her offer) and
        //  o Forward (alice owns less BTC than is in her final offer.
        //
        // It turns out that two of the forward cases fail for a different
        // reason.  They are therefore commented out here, But they are
        // revisited in the testSelfPayUnlimitedFunds() unit test.

        using namespace jtx;

        Env env{*this, features};
        auto const baseFee = env.current()->fees().base.drops();

        auto const gw = Account("gw");
        auto const btc = gw["BTC"];
        auto const usd = gw["USD"];
        auto const startXrpBalance = XRP(4000000);

        env.fund(startXrpBalance, gw);
        env.close();

        env(rate(gw, 1.25));
        env.close();

        // Test cases
        struct Actor
        {
            Account acct;
            int offers;        // offers on account after crossing
            PrettyAmount xrp;  // final expected after crossing
            PrettyAmount btc;  // final expected after crossing
            PrettyAmount usd;  // final expected after crossing
        };
        struct TestData
        {
            // The first three three integers give the *index* in actors
            // to assign each of the three roles.  By using indices it is
            // easy for alice to own the offer in the first leg, the second
            // leg, or both.
            std::size_t self;
            std::size_t leg0;
            std::size_t leg1;
            PrettyAmount btcStart;
            std::vector<Actor> actors;
        };

        // clang-format off
        TestData const tests[]{
            //        btcStart   --------------------- actor[0] ---------------------    -------------------- actor[1] -------------------
            {.self=0, .leg0=0, .leg1=1, .btcStart=btc(20), .actors={{"ann", 0, drops(3900000'000000 - (4 * baseFee)), btc(20.0), usd(3000)}, {"abe", 0, drops(4100000'000000 - (3 * baseFee)), btc( 0), usd(750)}}},  // no BTC xfer fee
            {.self=0, .leg0=1, .leg1=0, .btcStart=btc(20), .actors={{"bev", 0, drops(4100000'000000 - (4 * baseFee)), btc( 7.5), usd(2000)}, {"bob", 0, drops(3900000'000000 - (3 * baseFee)), btc(10), usd(  0)}}},  // no USD xfer fee
            {.self=0, .leg0=0, .leg1=0, .btcStart=btc(20), .actors={{"cam", 0, drops(4000000'000000 - (5 * baseFee)), btc(20.0), usd(2000)}                                                     }},  // no xfer fee
            {.self=0, .leg0=1, .leg1=0, .btcStart=btc( 5), .actors={{"deb", 1, drops(4040000'000000 - (4 * baseFee)), btc( 0.0), usd(2000)}, {"dan", 1, drops(3960000'000000 - (3 * baseFee)), btc( 4), usd(  0)}}},  // no USD xfer fee
        };
        // clang-format on

        for (auto const& t : tests)
        {
            Account const& self = t.actors[t.self].acct;
            Account const& leg0 = t.actors[t.leg0].acct;
            Account const& leg1 = t.actors[t.leg1].acct;

            for (auto const& actor : t.actors)
            {
                env.fund(XRP(4000000), actor.acct);
                env.close();

                env(trust(actor.acct, btc(40)));
                env(trust(actor.acct, usd(8000)));
                env.close();
            }

            env(pay(gw, self, t.btcStart));
            env(pay(gw, self, usd(2000)));
            if (self.id() != leg1.id())
                env(pay(gw, leg1, usd(2000)));
            env.close();

            // Get the initial offers in place.  Remember their sequences
            // so we can delete them later.
            env(offer(leg0, btc(10), XRP(100000), tfPassive));
            env.close();
            std::uint32_t const leg0OfferSeq = env.seq(leg0) - 1;

            env(offer(leg1, XRP(100000), usd(1000), tfPassive));
            env.close();
            std::uint32_t const leg1OfferSeq = env.seq(leg1) - 1;

            // This is the offer that matters.
            env(offer(self, usd(1000), btc(10)));
            env.close();
            std::uint32_t const selfOfferSeq = env.seq(self) - 1;

            // Verify results.
            for (auto const& actor : t.actors)
            {
                // Sometimes Taker crossing gets lazy about deleting offers.
                // Treat an empty offer as though it is deleted.
                auto actorOffers = offersOnAccount(env, actor.acct);
                auto const offerCount = std::distance(
                    actorOffers.begin(),
                    std::ranges::remove_if(actorOffers, [](std::shared_ptr<SLE const>& offer) {
                        return (*offer)[sfTakerGets].signum() == 0;
                    }).begin());
                BEAST_EXPECT(offerCount == actor.offers);

                env.require(Balance(actor.acct, actor.xrp));
                env.require(Balance(actor.acct, actor.btc));
                env.require(Balance(actor.acct, actor.usd));
            }
            // Remove any offers that might be left hanging around.  They
            // could bollix up later loops.
            env(offerCancel(leg0, leg0OfferSeq));
            env.close();
            env(offerCancel(leg1, leg1OfferSeq));
            env.close();
            env(offerCancel(self, selfOfferSeq));
            env.close();
        }
    }

    void
    testSelfPayUnlimitedFunds(FeatureBitset features)
    {
        testcase("Self Pay Unlimited Funds");
        // The Taker offer crossing code recognized when Alice was paying
        // Alice the same denomination.  In this case, as long as Alice
        // has a little bit of that denomination, it treats Alice as though
        // she has unlimited funds in that denomination.
        //
        // Huh?  What kind of sense does that make?
        //
        // One way to think about it is to break a single payment into a
        // series of very small payments executed sequentially but very
        // quickly.  Alice needs to pay herself 1 USD, but she only has
        // 0.01 USD.  Alice says, "Hey Alice, let me pay you a penny."
        // Alice does this, taking the penny out of her pocket and then
        // putting it back in her pocket.  Then she says, "Hey Alice,
        // I found another penny.  I can pay you another penny."  Repeat
        // these steps 100 times and Alice has paid herself 1 USD even though
        // she only owns 0.01 USD.
        //
        // That's all very nice, but the payment code does not support this
        // optimization.  In part that's because the payment code can
        // operate on a whole batch of offers.  As a matter of fact, it can
        // deal in two consecutive batches of offers.  It would take a great
        // deal of sorting out to figure out which offers in the two batches
        // had the same owner and give them special processing.  And,
        // honestly, it's a weird little corner case.
        //
        // So, since Flow offer crossing uses the payments engine, Flow
        // offer crossing no longer supports this optimization.
        //
        // The following test shows the difference in the behaviors between
        // Taker offer crossing and Flow offer crossing.

        using namespace jtx;

        Env env{*this, features};
        auto const baseFee = env.current()->fees().base.drops();

        auto const gw = Account("gw");
        auto const btc = gw["BTC"];
        auto const usd = gw["USD"];
        auto const startXrpBalance = XRP(4000000);

        env.fund(startXrpBalance, gw);
        env.close();

        env(rate(gw, 1.25));
        env.close();

        // Test cases
        struct Actor
        {
            Account acct;
            int offers;        // offers on account after crossing
            PrettyAmount xrp;  // final expected after crossing
            PrettyAmount btc;  // final expected after crossing
            PrettyAmount usd;  // final expected after crossing
        };
        struct TestData
        {
            // The first three three integers give the *index* in actors
            // to assign each of the three roles.  By using indices it is
            // easy for alice to own the offer in the first leg, the second
            // leg, or both.
            std::size_t self;
            std::size_t leg0;
            std::size_t leg1;
            PrettyAmount btcStart;
            std::vector<Actor> actors;
        };

        // clang-format off
        TestData const tests[]{
            //         btcStart    ------------------- actor[0] --------------------    ------------------- actor[1] --------------------
            {.self=0, .leg0=0, .leg1=1, .btcStart=btc(5), .actors={{"gay", 1, drops(3950000'000000 - (4 * baseFee)), btc(5), usd(2500)}, {"gar", 1, drops(4050000'000000 - (3 * baseFee)), btc(0), usd(1375)}}}, // no BTC xfer fee
            {.self=0, .leg0=0, .leg1=0, .btcStart=btc(5), .actors={{"hye", 2, drops(4000000'000000 - (5 * baseFee)), btc(5), usd(2000)}                                                     }}  // no xfer fee
        };
        // clang-format on

        for (auto const& t : tests)
        {
            Account const& self = t.actors[t.self].acct;
            Account const& leg0 = t.actors[t.leg0].acct;
            Account const& leg1 = t.actors[t.leg1].acct;

            for (auto const& actor : t.actors)
            {
                env.fund(XRP(4000000), actor.acct);
                env.close();

                env(trust(actor.acct, btc(40)));
                env(trust(actor.acct, usd(8000)));
                env.close();
            }

            env(pay(gw, self, t.btcStart));
            env(pay(gw, self, usd(2000)));
            if (self.id() != leg1.id())
                env(pay(gw, leg1, usd(2000)));
            env.close();

            // Get the initial offers in place.  Remember their sequences
            // so we can delete them later.
            env(offer(leg0, btc(10), XRP(100000), tfPassive));
            env.close();
            std::uint32_t const leg0OfferSeq = env.seq(leg0) - 1;

            env(offer(leg1, XRP(100000), usd(1000), tfPassive));
            env.close();
            std::uint32_t const leg1OfferSeq = env.seq(leg1) - 1;

            // This is the offer that matters.
            env(offer(self, usd(1000), btc(10)));
            env.close();
            std::uint32_t const selfOfferSeq = env.seq(self) - 1;

            // Verify results.
            for (auto const& actor : t.actors)
            {
                // Sometimes Taker offer crossing gets lazy about deleting
                // offers.  Treat an empty offer as though it is deleted.
                auto actorOffers = offersOnAccount(env, actor.acct);
                auto const offerCount = std::distance(
                    actorOffers.begin(),
                    std::ranges::remove_if(actorOffers, [](std::shared_ptr<SLE const>& offer) {
                        return (*offer)[sfTakerGets].signum() == 0;
                    }).begin());
                BEAST_EXPECT(offerCount == actor.offers);

                env.require(Balance(actor.acct, actor.xrp));
                env.require(Balance(actor.acct, actor.btc));
                env.require(Balance(actor.acct, actor.usd));
            }
            // Remove any offers that might be left hanging around.  They
            // could bollix up later loops.
            env(offerCancel(leg0, leg0OfferSeq));
            env.close();
            env(offerCancel(leg1, leg1OfferSeq));
            env.close();
            env(offerCancel(self, selfOfferSeq));
            env.close();
        }
    }

    void
    testRequireAuth(FeatureBitset features)
    {
        testcase("lsfRequireAuth");

        using namespace jtx;

        Env env{*this, features};

        auto const gw = Account("gw");
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gwUSD = gw["USD"];
        auto const aliceUSD = alice["USD"];
        auto const bobUSD = bob["USD"];

        env.fund(XRP(400000), gw, alice, bob);
        env.close();

        // GW requires authorization for holders of its IOUs
        env(fset(gw, asfRequireAuth));
        env.close();

        // Properly set trust and have gw authorize bob and alice
        env(trust(gw, bobUSD(100)), Txflags(tfSetfAuth));
        env(trust(bob, gwUSD(100)));
        env(trust(gw, aliceUSD(100)), Txflags(tfSetfAuth));
        env(trust(alice, gwUSD(100)));
        // Alice is able to place the offer since the GW has authorized her
        env(offer(alice, gwUSD(40), XRP(4000)));
        env.close();

        env.require(offers(alice, 1));
        env.require(Balance(alice, gwUSD(0)));

        env(pay(gw, bob, gwUSD(50)));
        env.close();

        env.require(Balance(bob, gwUSD(50)));

        // Bob's offer should cross Alice's
        env(offer(bob, XRP(4000), gwUSD(40)));
        env.close();

        env.require(offers(alice, 0));
        env.require(Balance(alice, gwUSD(40)));

        env.require(offers(bob, 0));
        env.require(Balance(bob, gwUSD(10)));
    }

    void
    testMissingAuth(FeatureBitset features)
    {
        testcase("Missing Auth");
        // 1. alice creates an offer to acquire USD/gw, an asset for which
        //    she does not have a trust line.  At some point in the future,
        //    gw adds lsfRequireAuth.  Then, later, alice's offer is crossed.
        //     Alice's offer is deleted, not consumed, since alice is not
        //     authorized to hold USD/gw.
        //
        // 2. alice tries to create an offer for USD/gw, now that gw has
        //    lsfRequireAuth set.  This time the offer create fails because
        //    alice is not authorized to hold USD/gw.
        //
        // 3. Next, gw creates a trust line to alice, but does not set
        //    tfSetfAuth on that trust line.  alice attempts to create an
        //    offer and again fails.
        //
        // 4. Finally, gw sets tsfSetAuth on the trust line authorizing
        //    alice to own USD/gw.  At this point alice successfully
        //    creates and crosses an offer for USD/gw.

        using namespace jtx;

        Env env{*this, features};

        auto const gw = Account("gw");
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gwUSD = gw["USD"];
        auto const aliceUSD = alice["USD"];
        auto const bobUSD = bob["USD"];

        env.fund(XRP(400000), gw, alice, bob);
        env.close();

        env(offer(alice, gwUSD(40), XRP(4000)));
        env.close();

        env.require(offers(alice, 1));
        env.require(Balance(alice, gwUSD(kNone)));
        env(fset(gw, asfRequireAuth));
        env.close();

        env(trust(gw, bobUSD(100)), Txflags(tfSetfAuth));
        env.close();
        env(trust(bob, gwUSD(100)));
        env.close();

        env(pay(gw, bob, gwUSD(50)));
        env.close();
        env.require(Balance(bob, gwUSD(50)));

        // gw now requires authorization and bob has gwUSD(50).  Let's see if
        // bob can cross alice's offer.
        //
        // Bob's offer shouldn't cross and alice's unauthorized offer should be
        // deleted.
        env(offer(bob, XRP(4000), gwUSD(40)));
        env.close();
        std::uint32_t const bobOfferSeq = env.seq(bob) - 1;

        env.require(offers(alice, 0));
        // alice's unauthorized offer is deleted & bob's offer not crossed.
        env.require(Balance(alice, gwUSD(kNone)));
        env.require(offers(bob, 1));
        env.require(Balance(bob, gwUSD(50)));

        // See if alice can create an offer without authorization.  alice
        // should not be able to create the offer and bob's offer should be
        // untouched.
        env(offer(alice, gwUSD(40), XRP(4000)), Ter(tecNO_LINE));
        env.close();

        env.require(offers(alice, 0));
        env.require(Balance(alice, gwUSD(kNone)));

        env.require(offers(bob, 1));
        env.require(Balance(bob, gwUSD(50)));

        // Set up a trust line for alice, but don't authorize it.  alice
        // should still not be able to create an offer for USD/gw.
        env(trust(gw, aliceUSD(100)));
        env.close();

        env(offer(alice, gwUSD(40), XRP(4000)), Ter(tecNO_AUTH));
        env.close();

        env.require(offers(alice, 0));
        env.require(Balance(alice, gwUSD(0)));

        env.require(offers(bob, 1));
        env.require(Balance(bob, gwUSD(50)));

        // Delete bob's offer so alice can create an offer without crossing.
        env(offerCancel(bob, bobOfferSeq));
        env.close();
        env.require(offers(bob, 0));

        // Finally, set up an authorized trust line for alice.  Now alice's
        // offer should succeed.  Note that, since this is an offer rather
        // than a payment, alice does not need to set a trust line limit.
        env(trust(gw, aliceUSD(100)), Txflags(tfSetfAuth));
        env.close();

        env(offer(alice, gwUSD(40), XRP(4000)));
        env.close();

        env.require(offers(alice, 1));

        // Now bob creates his offer again.  alice's offer should cross.
        env(offer(bob, XRP(4000), gwUSD(40)));
        env.close();

        env.require(offers(alice, 0));
        env.require(Balance(alice, gwUSD(40)));

        env.require(offers(bob, 0));
        env.require(Balance(bob, gwUSD(10)));
    }

    void
    testRCSmoketest(FeatureBitset features)
    {
        testcase("RippleConnect Smoketest payment flow");
        using namespace jtx;

        Env env{*this, features};

        // This test mimics a payment flow. The players:
        //   A USD gateway with hot and cold wallets
        //   A EUR gateway with hot and cold walllets
        //   A MM gateway that will provide offers from USD->EUR and EUR->USD
        // A path from hot US to cold EUR is found and then used to send
        // USD for EUR that goes through the market maker

        auto const hotUS = Account("hotUS");
        auto const coldUS = Account("coldUS");
        auto const hotEU = Account("hotEU");
        auto const coldEU = Account("coldEU");
        auto const mm = Account("mm");

        auto const usd = coldUS["USD"];
        auto const eur = coldEU["EUR"];

        env.fund(XRP(100000), hotUS, coldUS, hotEU, coldEU, mm);
        env.close();

        // Cold wallets require trust but will ripple by default
        for (auto const& cold : {coldUS, coldEU})
        {
            env(fset(cold, asfRequireAuth));
            env(fset(cold, asfDefaultRipple));
        }
        env.close();

        // Each hot wallet trusts the related cold wallet for a large amount
        env(trust(hotUS, usd(10000000)), Txflags(tfSetNoRipple));
        env(trust(hotEU, eur(10000000)), Txflags(tfSetNoRipple));
        // Market maker trusts both cold wallets for a large amount
        env(trust(mm, usd(10000000)), Txflags(tfSetNoRipple));
        env(trust(mm, eur(10000000)), Txflags(tfSetNoRipple));
        env.close();

        // Gateways authorize the trustlines of hot and market maker
        env(trust(coldUS, usd(0), hotUS, tfSetfAuth));
        env(trust(coldEU, eur(0), hotEU, tfSetfAuth));
        env(trust(coldUS, usd(0), mm, tfSetfAuth));
        env(trust(coldEU, eur(0), mm, tfSetfAuth));
        env.close();

        // Issue currency from cold wallets to hot and market maker
        env(pay(coldUS, hotUS, usd(5000000)));
        env(pay(coldEU, hotEU, eur(5000000)));
        env(pay(coldUS, mm, usd(5000000)));
        env(pay(coldEU, mm, eur(5000000)));
        env.close();

        // MM places offers
        float const rate = 0.9f;  // 0.9 USD = 1 EUR
        env(offer(mm, eur(4000000 * rate), usd(4000000)), Json(jss::Flags, tfSell));

        float const reverseRate = 1.0f / rate * 1.00101f;
        env(offer(mm, usd(4000000 * reverseRate), eur(4000000)), Json(jss::Flags, tfSell));
        env.close();

        // There should be a path available from hot US to cold EUR
        {
            json::Value jvParams;
            jvParams[jss::destination_account] = coldEU.human();
            jvParams[jss::destination_amount][jss::issuer] = coldEU.human();
            jvParams[jss::destination_amount][jss::currency] = "EUR";
            jvParams[jss::destination_amount][jss::value] = 10;
            jvParams[jss::source_account] = hotUS.human();

            json::Value const jrr{
                env.rpc("json", "ripple_path_find", to_string(jvParams))[jss::result]};

            BEAST_EXPECT(jrr[jss::status] == "success");
            BEAST_EXPECT(jrr[jss::alternatives].isArray() && jrr[jss::alternatives].size() > 0);
        }
        // Send the payment using the found path.
        env(pay(hotUS, coldEU, eur(10)), Sendmax(usd(11.1223326)));
    }

    void
    testSelfAuth(FeatureBitset features)
    {
        testcase("Self Auth");

        using namespace jtx;

        Env env{*this, features};

        auto const gw = Account("gw");
        auto const alice = Account("alice");
        auto const gwUSD = gw["USD"];
        auto const aliceUSD = alice["USD"];

        env.fund(XRP(400000), gw, alice);
        env.close();

        // Test that gw can create an offer to buy gw's currency.
        env(offer(gw, gwUSD(40), XRP(4000)));
        env.close();
        std::uint32_t const gwOfferSeq = env.seq(gw) - 1;
        env.require(offers(gw, 1));

        // Since gw has an offer out, gw should not be able to set RequireAuth.
        env(fset(gw, asfRequireAuth), Ter(tecOWNERS));
        env.close();

        // Cancel gw's offer so we can set RequireAuth.
        env(offerCancel(gw, gwOfferSeq));
        env.close();
        env.require(offers(gw, 0));

        // gw now requires authorization for holders of its IOUs
        env(fset(gw, asfRequireAuth));
        env.close();

        // Before DepositPreauth an account with lsfRequireAuth set could not
        // create an offer to buy their own currency.  After DepositPreauth
        // they can.
        env(offer(gw, gwUSD(40), XRP(4000)), Ter(tesSUCCESS));
        env.close();

        env.require(offers(gw, 1));

        // Set up an authorized trust line and pay alice gwUSD 50.
        env(trust(gw, aliceUSD(100)), Txflags(tfSetfAuth));
        env(trust(alice, gwUSD(100)));
        env.close();

        env(pay(gw, alice, gwUSD(50)));
        env.close();

        env.require(Balance(alice, gwUSD(50)));

        // alice's offer should cross gw's
        env(offer(alice, XRP(4000), gwUSD(40)));
        env.close();

        env.require(offers(alice, 0));
        env.require(Balance(alice, gwUSD(10)));

        env.require(offers(gw, 0));
    }

    void
    testDeletedOfferIssuer(FeatureBitset features)
    {
        // Show that an offer who's issuer has been deleted cannot be crossed.
        using namespace jtx;

        testcase("Deleted offer issuer");

        auto trustLineExists = [](jtx::Env const& env,
                                  jtx::Account const& src,
                                  jtx::Account const& dst,
                                  Currency const& cur) -> bool {
            return bool(env.le(keylet::line(src, dst, cur)));
        };

        Account const alice("alice");
        Account const becky("becky");
        Account const carol("carol");
        Account const gw("gateway");
        auto const usd = gw["USD"];
        auto const bux = alice["BUX"];

        Env env{*this, features};

        env.fund(XRP(10000), alice, becky, carol, noripple(gw));
        env.close();
        env.trust(usd(1000), becky);
        env(pay(gw, becky, usd(5)));
        env.close();
        BEAST_EXPECT(trustLineExists(env, gw, becky, usd.currency));

        // Make offers that produce USD and can be crossed two ways:
        // direct XRP -> USD
        // direct BUX -> USD
        env(offer(becky, XRP(2), usd(2)), Txflags(tfPassive));
        std::uint32_t const beckyBuxUsdSeq{env.seq(becky)};
        env(offer(becky, bux(3), usd(3)), Txflags(tfPassive));
        env.close();

        // becky keeps the offers, but removes the trustline.
        env(pay(becky, gw, usd(5)));
        env.trust(usd(0), becky);
        env.close();
        BEAST_EXPECT(!trustLineExists(env, gw, becky, usd.currency));
        BEAST_EXPECT(isOffer(env, becky, XRP(2), usd(2)));
        BEAST_EXPECT(isOffer(env, becky, bux(3), usd(3)));

        // Delete gw's account.
        {
            // The ledger sequence needs to far enough ahead of the account
            // sequence before the account can be deleted.
            int const delta = [&env, &gw, openLedgerSeq = env.current()->seq()]() -> int {
                std::uint32_t const gwSeq{env.seq(gw)};
                if (gwSeq + 255 > openLedgerSeq)
                    return gwSeq - openLedgerSeq + 255;
                return 0;
            }();

            for (int i = 0; i < delta; ++i)
                env.close();

            // Account deletion has a high fee.  Account for that.
            env(acctdelete(gw, alice), Fee(drops(env.current()->fees().increment)));
            env.close();

            // Verify that gw's account root is gone from the ledger.
            BEAST_EXPECT(!env.closed()->exists(keylet::account(gw.id())));
        }

        // alice crosses becky's first offer.  The offer create fails because
        // the USD issuer is not in the ledger.
        env(offer(alice, usd(2), XRP(2)), Ter(tecNO_ISSUER));
        env.close();
        env.require(offers(alice, 0));
        BEAST_EXPECT(isOffer(env, becky, XRP(2), usd(2)));
        BEAST_EXPECT(isOffer(env, becky, bux(3), usd(3)));

        // alice crosses becky's second offer.  Again, the offer create fails
        // because the USD issuer is not in the ledger.
        env(offer(alice, usd(3), bux(3)), Ter(tecNO_ISSUER));
        env.require(offers(alice, 0));
        BEAST_EXPECT(isOffer(env, becky, XRP(2), usd(2)));
        BEAST_EXPECT(isOffer(env, becky, bux(3), usd(3)));

        // Cancel becky's BUX -> USD offer so we can try auto-bridging.
        env(offerCancel(becky, beckyBuxUsdSeq));
        env.close();
        BEAST_EXPECT(!isOffer(env, becky, bux(3), usd(3)));

        // alice creates an offer that can be auto-bridged with becky's
        // remaining offer.
        env.trust(bux(1000), carol);
        env(pay(alice, carol, bux(2)));

        env(offer(alice, bux(2), XRP(2)));
        env.close();

        // carol attempts the auto-bridge.  Again, the offer create fails
        // because the USD issuer is not in the ledger.
        env(offer(carol, usd(2), bux(2)), Ter(tecNO_ISSUER));
        env.close();
        BEAST_EXPECT(isOffer(env, alice, bux(2), XRP(2)));
        BEAST_EXPECT(isOffer(env, becky, XRP(2), usd(2)));
    }

    void
    testTickSize(FeatureBitset features)
    {
        testcase("Tick Size");

        using namespace jtx;

        // Try to set tick size out of range
        {
            Env env{*this, features};
            auto const gw = Account{"gateway"};
            env.fund(XRP(10000), gw);
            env.close();

            auto txn = noop(gw);
            txn[sfTickSize.fieldName] = Quality::kMinTickSize - 1;
            env(txn, Ter(temBAD_TICK_SIZE));

            txn[sfTickSize.fieldName] = Quality::kMinTickSize;
            env(txn);
            BEAST_EXPECT((*env.le(gw))[sfTickSize] == Quality::kMinTickSize);

            txn = noop(gw);
            txn[sfTickSize.fieldName] = Quality::kMaxTickSize;
            env(txn);
            BEAST_EXPECT(!env.le(gw)->isFieldPresent(sfTickSize));

            txn = noop(gw);
            txn[sfTickSize.fieldName] = Quality::kMaxTickSize - 1;
            env(txn);
            BEAST_EXPECT((*env.le(gw))[sfTickSize] == Quality::kMaxTickSize - 1);

            txn = noop(gw);
            txn[sfTickSize.fieldName] = Quality::kMaxTickSize + 1;
            env(txn, Ter(temBAD_TICK_SIZE));

            txn[sfTickSize.fieldName] = 0;
            env(txn);
            BEAST_EXPECT(!env.le(gw)->isFieldPresent(sfTickSize));
        }

        Env env{*this, features};
        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const xts = gw["XTS"];
        auto const xxx = gw["XXX"];

        env.fund(XRP(10000), gw, alice);
        env.close();

        {
            // Gateway sets its tick size to 5
            auto txn = noop(gw);
            txn[sfTickSize.fieldName] = 5;
            env(txn);
            BEAST_EXPECT((*env.le(gw))[sfTickSize] == 5);
        }

        env(trust(alice, xts(1000)));
        env(trust(alice, xxx(1000)));

        env(pay(gw, alice, alice["XTS"](100)));
        env(pay(gw, alice, alice["XXX"](100)));

        env(offer(alice, xts(10), xxx(30)));
        env(offer(alice, xts(30), xxx(10)));
        env(offer(alice, xts(10), xxx(30)), Json(jss::Flags, tfSell));
        env(offer(alice, xts(30), xxx(10)), Json(jss::Flags, tfSell));

        std::map<std::uint32_t, std::pair<STAmount, STAmount>> offers;
        forEachItem(*env.current(), alice, [&](std::shared_ptr<SLE const> const& sle) {
            if (sle->getType() == ltOFFER)
            {
                offers.emplace(
                    (*sle)[sfSequence], std::make_pair((*sle)[sfTakerPays], (*sle)[sfTakerGets]));
            }
        });

        // first offer
        auto it = offers.begin();
        BEAST_EXPECT(it != offers.end());
        BEAST_EXPECT(
            it->second.first == xts(10) && it->second.second < xxx(30) &&
            it->second.second > xxx(29.9994));

        // second offer
        ++it;
        BEAST_EXPECT(it != offers.end());
        BEAST_EXPECT(it->second.first == xts(30) && it->second.second == xxx(10));

        // third offer
        ++it;
        BEAST_EXPECT(it != offers.end());
        BEAST_EXPECT(it->second.first == xts(10.0002) && it->second.second == xxx(30));

        // fourth offer
        // exact TakerPays is XTS(1/.033333)
        ++it;
        BEAST_EXPECT(it != offers.end());
        BEAST_EXPECT(it->second.first == xts(30) && it->second.second == xxx(10));

        BEAST_EXPECT(++it == offers.end());
    }

    // Helper function that returns offers on an account sorted by sequence.
    static std::vector<std::shared_ptr<SLE const>>
    sortedOffersOnAccount(jtx::Env& env, jtx::Account const& acct)
    {
        std::vector<std::shared_ptr<SLE const>> offers{offersOnAccount(env, acct)};
        std::ranges::sort(
            offers,
            [](std::shared_ptr<SLE const> const& rhs, std::shared_ptr<SLE const> const& lhs) {
                return (*rhs)[sfSequence] < (*lhs)[sfSequence];
            });
        return offers;
    }

    void
    testTicketOffer(FeatureBitset features)
    {
        testcase("Ticket Offers");

        using namespace jtx;

        // Two goals for this test.
        //
        //  o Verify that offers can be created using tickets.
        //
        //  o Show that offers in the _same_ order book remain in
        //    chronological order regardless of sequence/ticket numbers.
        Env env{*this, features};
        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const usd = gw["USD"];

        env.fund(XRP(10000), gw, alice, bob);
        env.close();

        env(trust(alice, usd(1000)));
        env(trust(bob, usd(1000)));
        env.close();

        env(pay(gw, alice, usd(200)));
        env.close();

        // Create four offers from the same account with identical quality
        // so they go in the same order book.  Each offer goes in a different
        // ledger so the chronology is clear.
        std::uint32_t const offerId0{env.seq(alice)};
        env(offer(alice, XRP(50), usd(50)));
        env.close();

        // Create two tickets.
        std::uint32_t const ticketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, 2));
        env.close();

        // Create another sequence-based offer.
        std::uint32_t const offerId1{env.seq(alice)};
        BEAST_EXPECT(offerId1 == offerId0 + 4);
        env(offer(alice, XRP(50), usd(50)));
        env.close();

        // Create two ticket based offers in reverse order.
        std::uint32_t const offerId2{ticketSeq + 1};
        env(offer(alice, XRP(50), usd(50)), ticket::Use(offerId2));
        env.close();

        // Create the last offer.
        std::uint32_t const offerId3{ticketSeq};
        env(offer(alice, XRP(50), usd(50)), ticket::Use(offerId3));
        env.close();

        // Verify that all of alice's offers are present.
        {
            auto offers = sortedOffersOnAccount(env, alice);
            BEAST_EXPECT(offers.size() == 4);
            BEAST_EXPECT(offers[0]->getFieldU32(sfSequence) == offerId0);
            BEAST_EXPECT(offers[1]->getFieldU32(sfSequence) == offerId3);
            BEAST_EXPECT(offers[2]->getFieldU32(sfSequence) == offerId2);
            BEAST_EXPECT(offers[3]->getFieldU32(sfSequence) == offerId1);
            env.require(Balance(alice, usd(200)));
            env.require(Owners(alice, 5));
        }

        // Cross alice's first offer.
        env(offer(bob, usd(50), XRP(50)));
        env.close();

        // Verify that the first offer alice created was consumed.
        {
            auto offers = sortedOffersOnAccount(env, alice);
            BEAST_EXPECT(offers.size() == 3);
            BEAST_EXPECT(offers[0]->getFieldU32(sfSequence) == offerId3);
            BEAST_EXPECT(offers[1]->getFieldU32(sfSequence) == offerId2);
            BEAST_EXPECT(offers[2]->getFieldU32(sfSequence) == offerId1);
        }

        // Cross alice's second offer.
        env(offer(bob, usd(50), XRP(50)));
        env.close();

        // Verify that the second offer alice created was consumed.
        {
            auto offers = sortedOffersOnAccount(env, alice);
            BEAST_EXPECT(offers.size() == 2);
            BEAST_EXPECT(offers[0]->getFieldU32(sfSequence) == offerId3);
            BEAST_EXPECT(offers[1]->getFieldU32(sfSequence) == offerId2);
        }

        // Cross alice's third offer.
        env(offer(bob, usd(50), XRP(50)));
        env.close();

        // Verify that the third offer alice created was consumed.
        {
            auto offers = sortedOffersOnAccount(env, alice);
            BEAST_EXPECT(offers.size() == 1);
            BEAST_EXPECT(offers[0]->getFieldU32(sfSequence) == offerId3);
        }

        // Cross alice's last offer.
        env(offer(bob, usd(50), XRP(50)));
        env.close();

        // Verify that the third offer alice created was consumed.
        {
            auto offers = sortedOffersOnAccount(env, alice);
            BEAST_EXPECT(offers.empty());
        }
        env.require(Balance(alice, usd(0)));
        env.require(Owners(alice, 1));
        env.require(Balance(bob, usd(200)));
        env.require(Owners(bob, 1));
    }

    void
    testTicketCancelOffer(FeatureBitset features)
    {
        testcase("Ticket Cancel Offers");

        using namespace jtx;

        // Verify that offers created with or without tickets can be canceled
        // by transactions with or without tickets.
        Env env{*this, features};
        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const usd = gw["USD"];

        env.fund(XRP(10000), gw, alice);
        env.close();

        env(trust(alice, usd(1000)));
        env.close();
        env.require(Owners(alice, 1), tickets(alice, 0));

        env(pay(gw, alice, usd(200)));
        env.close();

        // Create the first of four offers using a sequence.
        std::uint32_t const offerSeqId0{env.seq(alice)};
        env(offer(alice, XRP(50), usd(50)));
        env.close();
        env.require(Owners(alice, 2), tickets(alice, 0));

        // Create four tickets.
        std::uint32_t const ticketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, 4));
        env.close();
        env.require(Owners(alice, 6), tickets(alice, 4));

        // Create the second (also sequence-based) offer.
        std::uint32_t const offerSeqId1{env.seq(alice)};
        BEAST_EXPECT(offerSeqId1 == offerSeqId0 + 6);
        env(offer(alice, XRP(50), usd(50)));
        env.close();

        // Create the third (ticket-based) offer.
        std::uint32_t const offerTixId0{ticketSeq + 1};
        env(offer(alice, XRP(50), usd(50)), ticket::Use(offerTixId0));
        env.close();

        // Create the last offer.
        std::uint32_t const offerTixId1{ticketSeq};
        env(offer(alice, XRP(50), usd(50)), ticket::Use(offerTixId1));
        env.close();

        // Verify that all of alice's offers are present.
        {
            auto offers = sortedOffersOnAccount(env, alice);
            BEAST_EXPECT(offers.size() == 4);
            BEAST_EXPECT(offers[0]->getFieldU32(sfSequence) == offerSeqId0);
            BEAST_EXPECT(offers[1]->getFieldU32(sfSequence) == offerTixId1);
            BEAST_EXPECT(offers[2]->getFieldU32(sfSequence) == offerTixId0);
            BEAST_EXPECT(offers[3]->getFieldU32(sfSequence) == offerSeqId1);
            env.require(Balance(alice, usd(200)));
            env.require(Owners(alice, 7));
        }

        // Use a ticket to cancel an offer created with a sequence.
        env(offerCancel(alice, offerSeqId0), ticket::Use(ticketSeq + 2));
        env.close();

        // Verify that offerSeqId_0 was canceled.
        {
            auto offers = sortedOffersOnAccount(env, alice);
            BEAST_EXPECT(offers.size() == 3);
            BEAST_EXPECT(offers[0]->getFieldU32(sfSequence) == offerTixId1);
            BEAST_EXPECT(offers[1]->getFieldU32(sfSequence) == offerTixId0);
            BEAST_EXPECT(offers[2]->getFieldU32(sfSequence) == offerSeqId1);
        }

        // Use a ticket to cancel an offer created with a ticket.
        env(offerCancel(alice, offerTixId0), ticket::Use(ticketSeq + 3));
        env.close();

        // Verify that offerTixId_0 was canceled.
        {
            auto offers = sortedOffersOnAccount(env, alice);
            BEAST_EXPECT(offers.size() == 2);
            BEAST_EXPECT(offers[0]->getFieldU32(sfSequence) == offerTixId1);
            BEAST_EXPECT(offers[1]->getFieldU32(sfSequence) == offerSeqId1);
        }

        // All of alice's tickets should now be used up.
        env.require(Owners(alice, 3), tickets(alice, 0));

        // Use a sequence to cancel an offer created with a ticket.
        env(offerCancel(alice, offerTixId1));
        env.close();

        // Verify that offerTixId_1 was canceled.
        {
            auto offers = sortedOffersOnAccount(env, alice);
            BEAST_EXPECT(offers.size() == 1);
            BEAST_EXPECT(offers[0]->getFieldU32(sfSequence) == offerSeqId1);
        }

        // Use a sequence to cancel an offer created with a sequence.
        env(offerCancel(alice, offerSeqId1));
        env.close();

        // Verify that offerSeqId_1 was canceled.
        // All of alice's tickets should now be used up.
        env.require(Owners(alice, 1), tickets(alice, 0), offers(alice, 0));
    }

    void
    testFalseAssert()
    {
        // An assert was falsely triggering when computing rates for offers.
        // This unit test would trigger that assert (which has been removed).
        testcase("incorrect assert fixed");
        using namespace jtx;

        Env env{*this};
        auto const alice = Account("alice");
        auto const usd = alice["USD"];

        env.fund(XRP(10000), alice);
        env.close();
        env(offer(alice, XRP(100000000000), usd(100000000)));
        pass();
    }

    void
    testFillOrKill(FeatureBitset features)
    {
        testcase("fixFillOrKill");
        using namespace jtx;
        Env env(*this, features);
        Account const issuer("issuer");
        Account const maker("maker");
        Account const taker("taker");
        auto const usd = issuer["USD"];
        auto const eur = issuer["EUR"];

        env.fund(XRP(1'000), issuer);
        env.fund(XRP(1'000), maker, taker);
        env.close();

        env.trust(usd(1'000), maker, taker);
        env.trust(eur(1'000), maker, taker);
        env.close();

        env(pay(issuer, maker, usd(1'000)));
        env(pay(issuer, taker, usd(1'000)));
        env(pay(issuer, maker, eur(1'000)));
        env.close();

        auto makerUSDBalance = env.balance(maker, usd).value();
        auto takerUSDBalance = env.balance(taker, usd).value();
        auto makerEURBalance = env.balance(maker, eur).value();
        auto takerEURBalance = env.balance(taker, eur).value();
        auto makerXRPBalance = env.balance(maker, XRP).value();
        auto takerXRPBalance = env.balance(taker, XRP).value();

        // tfFillOrKill, TakerPays must be filled
        {
            TER const err = features[fixFillOrKill] ? TER(tesSUCCESS) : tecKILLED;

            env(offer(maker, XRP(100), usd(100)));
            env.close();

            env(offer(taker, usd(100), XRP(101)), Txflags(tfFillOrKill), Ter(err));
            env.close();

            makerXRPBalance -= txFee(env, 1);
            takerXRPBalance -= txFee(env, 1);
            if (isTesSuccess(err))
            {
                makerUSDBalance -= usd(100);
                takerUSDBalance += usd(100);
                makerXRPBalance += XRP(100).value();
                takerXRPBalance -= XRP(100).value();
            }
            BEAST_EXPECT(expectOffers(env, taker, 0));

            env(offer(maker, usd(100), XRP(100)));
            env.close();

            env(offer(taker, XRP(100), usd(101)), Txflags(tfFillOrKill), Ter(err));
            env.close();

            makerXRPBalance -= txFee(env, 1);
            takerXRPBalance -= txFee(env, 1);
            if (isTesSuccess(err))
            {
                makerUSDBalance += usd(100);
                takerUSDBalance -= usd(100);
                makerXRPBalance -= XRP(100).value();
                takerXRPBalance += XRP(100).value();
            }
            BEAST_EXPECT(expectOffers(env, taker, 0));

            env(offer(maker, usd(100), eur(100)));
            env.close();

            env(offer(taker, eur(100), usd(101)), Txflags(tfFillOrKill), Ter(err));
            env.close();

            makerXRPBalance -= txFee(env, 1);
            takerXRPBalance -= txFee(env, 1);
            if (isTesSuccess(err))
            {
                makerUSDBalance += usd(100);
                takerUSDBalance -= usd(100);
                makerEURBalance -= eur(100);
                takerEURBalance += eur(100);
            }
            BEAST_EXPECT(expectOffers(env, taker, 0));
        }

        // tfFillOrKill + tfSell, TakerGets must be filled
        {
            env(offer(maker, XRP(101), usd(101)));
            env.close();

            env(offer(taker, usd(100), XRP(101)), Txflags(tfFillOrKill | tfSell));
            env.close();

            makerUSDBalance -= usd(101);
            takerUSDBalance += usd(101);
            makerXRPBalance += XRP(101).value() - txFee(env, 1);
            takerXRPBalance -= XRP(101).value() + txFee(env, 1);
            BEAST_EXPECT(expectOffers(env, taker, 0));

            env(offer(maker, usd(101), XRP(101)));
            env.close();

            env(offer(taker, XRP(100), usd(101)), Txflags(tfFillOrKill | tfSell));
            env.close();

            makerUSDBalance += usd(101);
            takerUSDBalance -= usd(101);
            makerXRPBalance -= XRP(101).value() + txFee(env, 1);
            takerXRPBalance += XRP(101).value() - txFee(env, 1);
            BEAST_EXPECT(expectOffers(env, taker, 0));

            env(offer(maker, usd(101), eur(101)));
            env.close();

            env(offer(taker, eur(100), usd(101)), Txflags(tfFillOrKill | tfSell));
            env.close();

            makerUSDBalance += usd(101);
            takerUSDBalance -= usd(101);
            makerEURBalance -= eur(101);
            takerEURBalance += eur(101);
            makerXRPBalance -= txFee(env, 1);
            takerXRPBalance -= txFee(env, 1);
            BEAST_EXPECT(expectOffers(env, taker, 0));
        }

        // Fail regardless of fixFillOrKill amendment
        for (auto const flags : {tfFillOrKill, tfFillOrKill + tfSell})
        {
            env(offer(maker, XRP(100), usd(100)));
            env.close();

            env(offer(taker, usd(100), XRP(99)), Txflags(flags), Ter(tecKILLED));
            env.close();

            makerXRPBalance -= txFee(env, 1);
            takerXRPBalance -= txFee(env, 1);
            BEAST_EXPECT(expectOffers(env, taker, 0));

            env(offer(maker, usd(100), XRP(100)));
            env.close();

            env(offer(taker, XRP(100), usd(99)), Txflags(flags), Ter(tecKILLED));
            env.close();

            makerXRPBalance -= txFee(env, 1);
            takerXRPBalance -= txFee(env, 1);
            BEAST_EXPECT(expectOffers(env, taker, 0));

            env(offer(maker, usd(100), eur(100)));
            env.close();

            env(offer(taker, eur(100), usd(99)), Txflags(flags), Ter(tecKILLED));
            env.close();

            makerXRPBalance -= txFee(env, 1);
            takerXRPBalance -= txFee(env, 1);
            BEAST_EXPECT(expectOffers(env, taker, 0));
        }

        BEAST_EXPECT(
            env.balance(maker, usd) == makerUSDBalance &&
            env.balance(taker, usd) == takerUSDBalance &&
            env.balance(maker, eur) == makerEURBalance &&
            env.balance(taker, eur) == takerEURBalance &&
            env.balance(maker, XRP) == makerXRPBalance &&
            env.balance(taker, XRP) == takerXRPBalance);
    }

    void
    testAll(FeatureBitset features)
    {
        testCanceledOffer(features);
        testRmFundedOffer(features);
        testTinyPayment(features);
        testXRPTinyPayment(features);
        testEnforceNoRipple(features);
        testInsufficientReserve(features);
        testFillModes(features);
        testMalformed(features);
        testExpiration(features);
        testUnfundedCross(features);
        testSelfCross(false, features);
        testSelfCross(true, features);
        testNegativeBalance(features);
        testOfferCrossWithXRP(true, features);
        testOfferCrossWithXRP(false, features);
        testOfferCrossWithLimitOverride(features);
        testOfferAcceptThenCancel(features);
        testOfferCancelPastAndFuture(features);
        testCurrencyConversionEntire(features);
        testCurrencyConversionIntoDebt(features);
        testCurrencyConversionInParts(features);
        testCrossCurrencyStartXRP(features);
        testCrossCurrencyEndXRP(features);
        testCrossCurrencyBridged(features);
        testBridgedSecondLegDry(features);
        testOfferFeesConsumeFunds(features);
        testOfferCreateThenCross(features);
        testSellFlagBasic(features);
        testSellFlagExceedLimit(features);
        testGatewayCrossCurrency(features);
        testPartialCross(features);
        testXRPDirectCross(features);
        testDirectCross(features);
        testBridgedCross(features);
        testSellOffer(features);
        testSellWithFillOrKill(features);
        testTransferRateOffer(features);
        testSelfCrossOffer(features);
        testSelfIssueOffer(features);
        testBadPathAssert(features);
        testDirectToDirectPath(features);
        testSelfCrossLowQualityOffer(features);
        testOfferInScaling(features);
        testOfferInScalingWithXferRate(features);
        testOfferThresholdWithReducedFunds(features);
        testTinyOffer(features);
        testSelfPayXferFeeOffer(features);
        testSelfPayUnlimitedFunds(features);
        testRequireAuth(features);
        testMissingAuth(features);
        testRCSmoketest(features);
        testSelfAuth(features);
        testDeletedOfferIssuer(features);
        testTickSize(features);
        testTicketOffer(features);
        testTicketCancelOffer(features);
        testRmSmallIncreasedQOffersXRP(features);
        testRmSmallIncreasedQOffersIOU(features);
        testFillOrKill(features);
    }

    FeatureBitset const allFeatures{jtx::testableAmendments()};

    void
    run() override
    {
        testAll(allFeatures - featurePermissionedDEX);
        testFalseAssert();
    }
};

class OfferWOSmallQOffers_test : public OfferBaseUtil_test
{
    void
    run() override
    {
        testAll(allFeatures - fixFillOrKill - featurePermissionedDEX);
    }
};

class OfferAllFeatures_test : public OfferBaseUtil_test
{
    void
    run() override
    {
        testAll(allFeatures);
    }
};

class Offer_manual_test : public OfferBaseUtil_test
{
    void
    run() override
    {
        using namespace jtx;
        FeatureBitset const all{testableAmendments()};
        FeatureBitset const fillOrKill{fixFillOrKill};
        FeatureBitset const permDEX{featurePermissionedDEX};

        testAll(all - fillOrKill - permDEX);
        testAll(all - permDEX);
        testAll(all);
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(OfferBaseUtil, app, xrpl, 2);
BEAST_DEFINE_TESTSUITE_PRIO(OfferWOSmallQOffers, app, xrpl, 2);
BEAST_DEFINE_TESTSUITE_PRIO(OfferAllFeatures, app, xrpl, 2);
BEAST_DEFINE_TESTSUITE_MANUAL_PRIO(Offer_manual, app, xrpl, 20);

}  // namespace xrpl::test
