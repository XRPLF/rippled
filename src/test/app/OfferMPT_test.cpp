#include <test/jtx/Env.h>
#include <test/jtx/PathSet.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/WSClient.h>
#include <test/jtx/acctdelete.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/fee.h>
#include <test/jtx/jtx_json.h>
#include <test/jtx/mpt.h>
#include <test/jtx/noop.h>
#include <test/jtx/offer.h>
#include <test/jtx/owners.h>
#include <test/jtx/paths.h>
#include <test/jtx/require.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/tags.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>
#include <test/jtx/txflags.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
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
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace xrpl::test {

class OfferMPT_test : public beast::unit_test::Suite
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

public:
    void
    testRmFundedOffer(FeatureBitset features)
    {
        testcase("Incorrect Removal of Funded Offers");

        // We need at least two paths. One at good quality and one at bad
        // quality.  The bad quality path needs two offer books in a row.
        // Each offer book should have two offers at the same quality, the
        // offers should be completely consumed, and the payment should
        // require both offers to be satisfied. The first offer must
        // be "taker gets" XRP. Old, broken would remove the first
        // "taker gets" xrp offer, even though the offer is still funded and
        // not used for the payment.

        using namespace jtx;
        auto const gw = Account{"gateway"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};

        auto test = [&](auto&& issue1, auto&& issue2) {
            Env env{*this, features};

            env.fund(XRP(10'000), alice, bob, carol, gw);
            env.close();

            auto const usd =
                issue1({.env = env, .token = "USD", .issuer = gw, .holders = {alice, bob, carol}});
            auto const btc =
                issue2({.env = env, .token = "BTC", .issuer = gw, .holders = {alice, bob, carol}});

            env(pay(gw, alice, btc(1'000)));

            env(pay(gw, carol, usd(1'000)));
            env(pay(gw, carol, btc(1'000)));

            // Must be two offers at the same quality
            // "taker gets" must be XRP
            // (Different amounts, so I can distinguish the offers)
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
                Sendmax(btc(1'000)),
                Txflags(tfPartialPayment));

            env.require(Balance(bob, usd(100)));
            BEAST_EXPECT(
                !isOffer(env, carol, btc(1), usd(100)) && isOffer(env, carol, btc(49), XRP(49)));
        };
        testHelper2TokensMix(test);
    }

    void
    testCanceledOffer(FeatureBitset features)
    {
        testcase("Removing Canceled Offers");

        using namespace jtx;
        Env env{*this, features};

        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};

        env.fund(XRP(10'000), alice, gw);
        env.close();

        MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice}});

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
            Ter(TER{tecEXPIRED}));
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

        auto test = [&](auto&& issue1, auto&& issue2) {
            Env env{*this, features};

            env.fund(XRP(10'000), alice, bob, carol, gw);
            env.close();

            auto const usd = issue1(
                {.env = env,
                 .token = "USD",
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .limit = 400'000'000});
            auto const eur = issue2(
                {.env = env,
                 .token = "EUR",
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .limit = 400'000'000});

            env(pay(gw, alice, usd(100'000'000)));
            env(pay(gw, carol, eur(100'000'000)));

            // Create more offers than the loop max count in DeliverNodeReverse
            // Note: the DeliverNodeReverse code has been removed; however since
            // this is a regression test the original test is being left as-is
            // for now.
            for (int i = 0; i < 101; ++i)
                env(offer(carol, usd(1'000'000), eur(2'000'000)));

            // Original Offer test sends EUR(10**-81). MPT is integral,
            // therefore and integral value is sent respecting the exchange
            // rate. I.e. if EUR(1) is sent then it'll result in USD(0).
            env(pay(alice, bob, eur(2)), Path(~eur), Sendmax(usd(100)));
        };
        testHelper2TokensMix(test);
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

        Env env{*this, features};

        env.fund(XRP(10'000), alice, bob, carol, dan, erin, gw);
        env.close();

        MPT const usd = MPTTester(
            {.env = env,
             .issuer = gw,
             .holders = {alice, bob, carol, dan, erin},
             .pay = std::nullopt});
        env(pay(gw, carol, usd(99'999)));
        env(pay(gw, dan, usd(100'000)));
        env(pay(gw, erin, usd(100'000)));
        env.close();

        // Carol doesn't quite have enough funds for this offer
        // The amount left after this offer is taken will cause
        // STAmount to incorrectly round to zero when the next offer
        // (at a good quality) is considered. (when the now removed
        // stAmountCalcSwitchover2 patch was inactive)
        env(offer(carol, drops(1), usd(99'999)));
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
        env(offer(erin, drops(2), usd(100'000)));

        env(pay(alice, bob, usd(100'000)),
            Path(~usd),
            Sendmax(XRP(102)),
            Txflags(tfNoRippleDirect | tfPartialPayment));

        env.require(offers(carol, 0), offers(dan, 1));

        // offer was correctly consumed. There is still some
        // liquidity left on that offer.
        env.require(Balance(erin, usd(99'999)), offers(erin, 1));
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

        // Test offer crossing
        for (auto crossBothOffers : {false, true})
        {
            Env env{*this, features};

            env.fund(XRP(10'000), alice, bob, carol, gw);

            MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice, bob, carol}});
            // underfund carol's offer
            auto initialCarolUSD = usd(499);
            env(pay(gw, carol, initialCarolUSD));
            env(pay(gw, bob, usd(100'000)));
            env.close();
            // This offer is underfunded
            env(offer(carol, drops(1), usd(1'000)));
            env.close();
            // offer at a lower quality
            env(offer(bob, drops(2), usd(1'000), tfPassive));
            env.close();
            env.require(offers(bob, 1), offers(carol, 1));

            // alice places an offer that crosses carol's; depending on
            // "crossBothOffers" it may cross bob's as well
            auto aliceTakerGets = crossBothOffers ? drops(2) : drops(1);
            env(offer(alice, usd(1'000), aliceTakerGets));
            env.close();

            env.require(
                offers(carol, 0),
                Balance(
                    carol,
                    initialCarolUSD));  // offer is removed but not taken
            if (crossBothOffers)
            {
                env.require(
                    offers(alice, 0), Balance(alice, usd(1'000)));  // alice's offer is crossed
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

            env.fund(XRP(10'000), alice, bob, carol, gw);
            env.close();

            MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice, bob, carol}});
            auto const initialCarolUSD = usd(999);
            env(pay(gw, carol, initialCarolUSD));
            env.close();
            env(pay(gw, bob, usd(100'000)));
            env.close();
            env(offer(carol, drops(1), usd(1'000)));
            env.close();
            env(offer(bob, drops(2), usd(2'000), tfPassive));
            env.close();
            env.require(offers(bob, 1), offers(carol, 1));

            std::uint32_t const flags =
                partialPayment ? (tfNoRippleDirect | tfPartialPayment) : tfNoRippleDirect;

            TER const expectedTer = partialPayment ? TER{tesSUCCESS} : TER{tecPATH_PARTIAL};

            env(pay(alice, bob, usd(5'000)),
                Path(~usd),
                Sendmax(XRP(1)),
                Txflags(flags),
                Ter(expectedTer));
            env.close();

            if (expectedTer == tesSUCCESS)
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
    testRmSmallIncreasedQOffersMPT(FeatureBitset features)
    {
        testcase("Rm small increased q offers MPT");

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

        auto test = [&](auto&& issue1, auto&& issue2) {
            auto tinyAmount = [&]<typename T>(T const& token) -> PrettyAmount {
                if constexpr (std::is_same_v<T, IOU>)
                {
                    STAmount const amt(
                        token,
                        /*mantissa*/ 1,
                        /*exponent*/ -81);
                    return PrettyAmount(amt, token.account.name());
                }
                else
                {
                    STAmount const amt(
                        token,
                        /*mantissa*/ 1,
                        /*exponent*/ 0);
                    return PrettyAmount(amt, "MPT");
                }
            };

            // Test offer crossing
            for (auto crossBothOffers : {false, true})
            {
                Env env{*this, features};

                env.fund(XRP(10'000), alice, bob, carol, gw);
                env.close();

                auto const usd = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 100'000'000});
                auto const eur = issue2(
                    {.env = env,
                     .token = "EUR",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 100'000'000});
                // underfund carol's offer
                auto initialCarolUSD = tinyAmount(usd);
                env(pay(gw, carol, initialCarolUSD));
                env(pay(gw, bob, usd(100'000)));
                env(pay(gw, alice, eur(100'000)));
                env.close();
                // This offer is underfunded
                env(offer(carol, eur(10), usd(10'000)));
                env.close();
                // offer at a lower quality
                env(offer(bob, eur(10), usd(5'000), tfPassive));
                env.close();
                env.require(offers(bob, 1), offers(carol, 1));

                // alice places an offer that crosses carol's; depending on
                // "crossBothOffers" it may cross bob's as well
                // Whatever
                auto aliceTakerGets = crossBothOffers ? eur(2) : eur(1);
                env(offer(alice, usd(1'000), aliceTakerGets));
                env.close();

                // carol's offer can be partially crossed when EUR is IOU:
                // 10e-3EUR/1USD
                using tEUR = std::decay_t<decltype(eur)>;
                static constexpr bool kIsEuriou = std::is_same_v<tEUR, IOU>;
                // partially crossed if IOU, removed but not taken if MPT
                auto const balanceCarolUSD = kIsEuriou ? usd(0) : initialCarolUSD;

                env.require(offers(carol, 0), Balance(carol, balanceCarolUSD));
                if (crossBothOffers)
                {
                    env.require(
                        offers(alice, 0), Balance(alice, usd(1'000)));  // alice's offer is crossed
                }
                else
                {
                    // partially crossed if IOU, not crossed if MPT
                    auto const balanceAliceUSD = kIsEuriou ? usd(1) : usd(0);
                    env.require(offers(alice, 1), Balance(alice, balanceAliceUSD));
                }
            }

            // Test payments
            for (auto partialPayment : {false, true})
            {
                Env env{*this, features};

                env.fund(XRP(10'000), alice, bob, carol, gw);
                env.close();

                auto const usd = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 100'000'000});
                auto const eur = issue2(
                    {.env = env,
                     .token = "EUR",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 100'000'000});
                // underfund carol's offer
                auto const initialCarolUSD = tinyAmount(usd);
                env(pay(gw, carol, initialCarolUSD));
                env(pay(gw, bob, usd(100'000)));
                env(pay(gw, alice, eur(100'000)));
                env.close();
                // This offer is underfunded
                env(offer(carol, eur(10), usd(2'000)));
                env.close();
                env(offer(bob, eur(20), usd(4'000), tfPassive));
                env.close();
                env.require(offers(bob, 1), offers(carol, 1));

                std::uint32_t const flags =
                    partialPayment ? (tfNoRippleDirect | tfPartialPayment) : tfNoRippleDirect;

                TER const expectedTer = partialPayment ? TER{tesSUCCESS} : TER{tecPATH_PARTIAL};

                env(pay(alice, bob, usd(5'000)),
                    Path(~usd),
                    Sendmax(eur(100)),
                    Txflags(flags),
                    Ter(expectedTer));
                env.close();

                if (expectedTer == tesSUCCESS)
                {
                    // carol's offer can be partially crossed when EUR is IOU:
                    // 10e-3EUR/1USD
                    using tEUR = std::decay_t<decltype(eur)>;
                    static constexpr bool kIsEuriou = std::is_same_v<tEUR, IOU>;
                    // partially crossed if IOU, removed but not taken if MPT
                    auto const balanceCarolUSD = kIsEuriou ? usd(0) : initialCarolUSD;
                    env.require(offers(carol, 0));
                    env.require(Balance(carol, balanceCarolUSD));
                }
                else
                {
                    // TODO: Offers are not removed when payments fail
                    // If that is addressed, the test should show that carol's
                    // offer is removed but not taken, as in the other branch of
                    // this if statement
                }
            }
        };
        testHelper2TokensMix(test);
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

        auto const xrpOffer = XRP(1'000);

        // No crossing:
        {
            Env env{*this, features};

            env.fund(XRP(1'000'000), gw);

            auto const f = env.current()->fees().base;
            auto const r = reserve(env, 0);

            env.fund(r + f, alice);

            MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice}});

            auto const usdOffer = usd(1'000);

            env(pay(gw, alice, usdOffer), Ter(tesSUCCESS));
            env(offer(alice, xrpOffer, usdOffer), Ter(tecINSUF_RESERVE_OFFER));

            env.require(Balance(alice, r - f), Owners(alice, 1));
        }

        // Partial cross:
        {
            Env env{*this, features};

            env.fund(XRP(1'000'000), gw);

            auto const f = env.current()->fees().base;
            auto const r = reserve(env, 0);

            env.fund(r + f, alice);
            env.fund(r + 2 * f + xrpOffer, bob);

            MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}});

            auto const usdOffer = usd(1'000);
            auto const usdOffer2 = usd(500);
            auto const xrpOffer2 = XRP(500);

            env(offer(bob, usdOffer2, xrpOffer2), Ter(tesSUCCESS));

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
        // if an offer were added. Attempt to sell MPTs to
        // buy XRP. If it fully crosses, we succeed.
        {
            Env env{*this, features};

            env.fund(XRP(1'000'000), gw);

            auto const f = env.current()->fees().base;
            auto const r = reserve(env, 0);

            env.fund(r + f, alice);

            MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice}});

            auto const usdOffer = usd(1'000);
            auto const usdOffer2 = usd(500);
            auto const xrpOffer2 = XRP(500);

            env.fund(r + f + xrpOffer, bob, carol);
            env(offer(bob, usdOffer2, xrpOffer2), Ter(tesSUCCESS));
            env(offer(carol, usdOffer, xrpOffer), Ter(tesSUCCESS));

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
    static std::vector<SLE::const_pointer>
    offersOnAccount(jtx::Env& env, jtx::Account account)
    {
        std::vector<SLE::const_pointer> result;
        forEachItem(*env.current(), account, [&result](SLE::const_ref sle) {
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

        auto const startBalance = XRP(1'000'000);
        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};

        // Fill or Kill - unless we fully cross, just charge a fee and don't
        // place the offer on the books.  But also clean up expired offers
        // that are discovered along the way.
        //
        {
            Env env{*this, features};

            auto const f = env.current()->fees().base;

            env.fund(startBalance, gw, alice, bob);

            MPTTester musd({.env = env, .issuer = gw});
            MPT const usd = musd["USD"];

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

            musd.authorize({.account = alice});
            env(pay(gw, alice, usd(1'000)), Ter(tesSUCCESS));

            // Order that can't be filled but will remove bob's expired offer:
            env(offer(alice, XRP(1'000), usd(1'000)), Txflags(tfFillOrKill), Ter(tecKILLED));

            env.require(
                Balance(alice, startBalance - (f * 2)),
                Balance(alice, usd(1'000)),
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

            MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice}});

            env(pay(gw, alice, usd(1'000)), Ter(tesSUCCESS));

            // No cross:
            {
                env(offer(alice, XRP(1'000), usd(1000)),
                    Txflags(tfImmediateOrCancel),
                    Ter(tecKILLED));
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

            MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {bob}});

            env(pay(gw, bob, usd(1'000)));
            env.close();

            env(offer(alice, usd(1'000), XRP(2'000)));
            env.close();

            auto const aliceOffers = offersOnAccount(env, alice);
            BEAST_EXPECT(aliceOffers.size() == 1);
            for (auto const& offerPtr : aliceOffers)
            {
                auto const& offer = *offerPtr;
                BEAST_EXPECT(offer[sfTakerGets] == XRP(2'000));
                BEAST_EXPECT(offer[sfTakerPays] == usd(1'000));
            }

            // bob creates a passive offer that could cross alice's.
            // bob's offer should stay in the ledger.
            env(offer(bob, XRP(2'000), usd(1'000), tfPassive));
            env.close();
            env.require(offers(alice, 1));

            auto const bobOffers = offersOnAccount(env, bob);
            BEAST_EXPECT(bobOffers.size() == 1);
            for (auto const& offerPtr : bobOffers)
            {
                auto const& offer = *offerPtr;
                BEAST_EXPECT(offer[sfTakerGets] == usd(1'000));
                BEAST_EXPECT(offer[sfTakerPays] == XRP(2'000));
            }

            // It should be possible for gw to cross both of those offers.
            env(offer(gw, XRP(2'000), usd(1'000)));
            env.close();
            env.require(offers(alice, 0));
            env.require(offers(gw, 0));
            env.require(offers(bob, 1));

            env(offer(gw, usd(1'000), XRP(2'000)));
            env.close();
            env.require(offers(bob, 0));
            env.require(offers(gw, 0));
        }

        // tfPassive -- cross only offers of better quality.
        {
            Env env(*this, features);

            env.fund(startBalance, gw, "alice", "bob");
            env.close();

            MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {bob}});

            env(pay(gw, "bob", usd(10'000)));
            env(offer("alice", usd(5'000), XRP(1'001)));
            env.close();

            env(offer("alice", usd(5'000), XRP(1'000)));
            env.close();

            auto const aliceOffers = offersOnAccount(env, "alice");
            BEAST_EXPECT(aliceOffers.size() == 2);

            // bob creates a passive offer.  That offer should cross one
            // of alice's (the one with better quality) and leave alice's
            // other offer untouched.
            env(offer("bob", XRP(2'000), usd(10'000), tfPassive));
            env.close();
            env.require(offers("alice", 1));

            auto const bobOffers = offersOnAccount(env, "bob");
            BEAST_EXPECT(bobOffers.size() == 1);
            for (auto const& offerPtr : bobOffers)
            {
                auto const& offer = *offerPtr;
                BEAST_EXPECT(offer[sfTakerGets] == usd(4'995));
                BEAST_EXPECT(offer[sfTakerPays] == XRP(999));
            }
        }
    }

    void
    testMalformed(FeatureBitset features)
    {
        testcase("Malformed Detection");

        using namespace jtx;

        auto const startBalance = XRP(1'000'000);
        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};

        Env env{*this, features};

        env.fund(startBalance, gw, alice);

        MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice}});

        // Sell and buy the same asset
        {
            // Alice tries an MPT to MPT order:
            env(pay(gw, alice, usd(1'000)), Ter(tesSUCCESS));
            env(offer(alice, usd(1'000), usd(1'000)), Ter(temREDUNDANT));
            env.require(Owners(alice, 1), offers(alice, 0));
        }

        // Offers with negative amounts
        {
            env(offer(alice, -usd(1'000), XRP(1'000)), Ter(temBAD_AMOUNT));
            env.require(Owners(alice, 1), offers(alice, 0));
        }

        // Bad MPT
        {
            auto const bad = MPT(badMPT());

            env(offer(alice, XRP(1'000), bad(1'000)), Ter(temBAD_CURRENCY));
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

        auto const startBalance = XRP(1'000'000);
        auto const xrpOffer = XRP(1'000);

        Env env{*this, features};

        env.fund(startBalance, gw, alice, bob);
        env.close();

        auto const f = env.current()->fees().base;

        MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice}});
        auto const usdOffer = usd(1'000);

        env(pay(gw, alice, usdOffer), Ter(tesSUCCESS));
        env.close();
        env.require(
            Balance(alice, startBalance - f),
            Balance(alice, usdOffer),
            offers(alice, 0),
            Owners(alice, 1));

        // Place an offer that should have already expired.
        env(offer(alice, xrpOffer, usdOffer),
            Json(sfExpiration.fieldName, lastClose(env)),
            Ter(TER{tecEXPIRED}));

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

        auto const xrpOffer = XRP(1'000);

        Env env{*this, features};

        env.fund(XRP(1'000'000), gw);

        // The fee that's charged for transactions
        auto const f = env.current()->fees().base;

        // Account is at the reserve, and will dip below once
        // fees are subtracted.
        env.fund(reserve(env, 0), "alice");
        MPT const usd = MPTTester({.env = env, .issuer = gw});
        auto const usdOffer = usd(1'000);
        env(offer("alice", usdOffer, xrpOffer), Ter(tecUNFUNDED_OFFER));
        env.require(Balance("alice", reserve(env, 0) - f), Owners("alice", 0));

        // Account has just enough for the reserve and the
        // fee.
        env.fund(reserve(env, 0) + f, "bob");
        env(offer("bob", usdOffer, xrpOffer), Ter(tecUNFUNDED_OFFER));
        env.require(Balance("bob", reserve(env, 0)), Owners("bob", 0));

        // Account has enough for the reserve, the fee and
        // the offer, and a bit more, but not enough for the
        // reserve after the offer is placed.
        env.fund(reserve(env, 0) + f + XRP(1), "carol");
        env(offer("carol", usdOffer, xrpOffer), Ter(tecINSUF_RESERVE_OFFER));
        env.require(Balance("carol", reserve(env, 0) + XRP(1)), Owners("carol", 0));

        // Account has enough for the reserve plus one
        // offer, and the fee.
        env.fund(reserve(env, 1) + f, "dan");
        env(offer("dan", usdOffer, xrpOffer), Ter(tesSUCCESS));
        env.require(Balance("dan", reserve(env, 1)), Owners("dan", 1));

        // Account has enough for the reserve plus one
        // offer, the fee and the entire offer amount.
        env.fund(reserve(env, 1) + f + xrpOffer, "eve");
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

        auto test = [&](auto&& issue1, auto&& issue2) {
            Env env{*this, features};
            env.close();

            env.fund(XRP(10'000), gw);
            auto const usd = issue1({.env = env, .token = "USD", .issuer = gw});
            auto const btc = issue2({.env = env, .token = "BTC", .issuer = gw});
            using tUSD = std::decay_t<decltype(usd)>;
            using tBTC = std::decay_t<decltype(btc)>;
            if (usePartner)
            {
                env.fund(XRP(10'000), partner);
                if constexpr (std::is_same_v<tUSD, IOU>)
                {
                    env(trust(partner, usd(100)));
                }
                else
                {
                    MPTTester musd(env, gw, usd);
                    musd.authorize({.account = partner});
                }
                if constexpr (std::is_same_v<tBTC, IOU>)
                {
                    env(trust(partner, btc(500)));
                }
                else
                {
                    MPTTester mbtc(env, gw, btc);
                    mbtc.authorize({.account = partner});
                }
                env(pay(gw, partner, usd(100)));
                env(pay(gw, partner, btc(500)));
            }
            auto const& accountToTest = usePartner ? partner : gw;

            env.close();
            env.require(offers(accountToTest, 0));

            // PART 1:
            // we will make two offers that can be used to bridge BTC to USD
            // through XRP
            env(offer(accountToTest, btc(250), XRP(1'000)));
            env.require(offers(accountToTest, 1));

            // validate that the book now shows a BTC for XRP offer
            BEAST_EXPECT(isOffer(env, accountToTest, btc(250), XRP(1'000)));

            auto const secondLegSeq = env.seq(accountToTest);
            env(offer(accountToTest, XRP(1'000), usd(50)));
            env.require(offers(accountToTest, 2));

            // validate that the book also shows a XRP for USD offer
            BEAST_EXPECT(isOffer(env, accountToTest, XRP(1'000), usd(50)));

            // now make an offer that will cross and auto-bridge, meaning
            // the outstanding offers will be taken leaving us with none
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

                // No stale offers
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
            // simple direct crossing  BTC to USD and then USD to BTC which
            // causes the first offer to be replaced
            env(offer(accountToTest, btc(250), usd(50)));
            env.require(offers(accountToTest, 1));

            // validate that the book shows one BTC for USD offer and no USD for
            // BTC offers
            BEAST_EXPECT(isOffer(env, accountToTest, btc(250), usd(50)));

            jrr = getBookOffers(env, usd, btc);
            BEAST_EXPECT(jrr[jss::offers].isArray());
            BEAST_EXPECT(jrr[jss::offers].size() == 0);

            // this second offer would self-cross directly, so it causes the
            // first offer by the same owner/taker to be removed
            env(offer(accountToTest, usd(50), btc(250)));
            env.require(offers(accountToTest, 1));

            // validate that we now have just the second offer...the first
            // was removed
            jrr = getBookOffers(env, btc, usd);
            BEAST_EXPECT(jrr[jss::offers].isArray());
            BEAST_EXPECT(jrr[jss::offers].size() == 0);

            BEAST_EXPECT(isOffer(env, accountToTest, usd(50), btc(250)));
        };
        testHelper2TokensMix(test);
    }

    void
    testNegativeBalance(FeatureBitset features)
    {
        // This test creates an offer test for negative balance
        // with transfer fees and miniscule funds.
        testcase("Negative Balance");

        using namespace jtx;
        FeatureBitset const localFeatures = features | fixReducedOffersV2;

        Env env{*this, localFeatures};

        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};

        // these *interesting* amounts were taken
        // from the original JS test that was ported here
        auto const gwInitialBalance = drops(1'149'999'730);
        auto const aliceInitialBalance = drops(499'946'999'680);
        auto const bobInitialBalance = drops(10'199'999'920);

        env.fund(gwInitialBalance, gw);
        env.fund(aliceInitialBalance, alice);
        env.fund(bobInitialBalance, bob);

        MPTTester const musd(
            {.env = env, .issuer = gw, .holders = {alice, bob}, .transferFee = 5'000});
        MPT const usd = musd;
        auto const smallAmount = STAmount{usd, 1};

        env(pay(gw, alice, usd(50)));
        env(pay(gw, bob, smallAmount));

        env(offer(alice, usd(50), XRP(150'000)));

        // unfund the offer
        env(pay(alice, gw, usd(50)));

        // verify balances
        auto jrr = ledgerEntryMPT(env, alice, usd);
        // this represents 0 since MPTAmount is a default field
        BEAST_EXPECT(!jrr[jss::node].isMember(sfMPTAmount.fieldName));

        jrr = ledgerEntryMPT(env, bob, usd);
        BEAST_EXPECT(jrr[jss::node][sfMPTAmount.fieldName] == "1");

        // create crossing offer
        std::uint32_t const bobOfferSeq = env.seq(bob);
        env(offer(bob, XRP(2000), usd(1)));

        // With the rounding introduced by fixReducedOffersV2, bob's
        // offer does not cross alice's offer and goes straight into
        // the ledger.
        jrr = ledgerEntryMPT(env, bob, usd);
        BEAST_EXPECT(jrr[jss::node][sfMPTAmount.fieldName] == "1");

        json::Value const bobOffer = ledgerEntryOffer(env, bob, bobOfferSeq)[jss::node];
        BEAST_EXPECT(bobOffer[sfTakerGets.jsonName][jss::value] == "1");
        BEAST_EXPECT(bobOffer[sfTakerPays.jsonName] == "2000000000");
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

        env.fund(XRP(10'000), gw, alice, bob);

        MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}});

        env(pay(gw, alice, usd(500)));

        if (reverseOrder)
            env(offer(bob, usd(1), XRP(4'000)));

        env(offer(alice, XRP(150'000), usd(50)));

        if (!reverseOrder)
            env(offer(bob, usd(1), XRP(4000)));

        // Existing offer pays better than this wants.
        // Fully consume existing offer.
        // Pay 1 USD, get 4000 XRP.

        auto jrr = ledgerEntryMPT(env, bob, usd);
        BEAST_EXPECT(jrr[jss::node][sfMPTAmount.fieldName] == "1");
        jrr = ledgerEntryRoot(env, bob);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            to_string(
                (XRP(10000) - XRP(reverseOrder ? 4000 : 3000) - env.current()->fees().base * 2)
                    .xrp()));

        jrr = ledgerEntryMPT(env, alice, usd);
        BEAST_EXPECT(jrr[jss::node][sfMPTAmount.fieldName] == "499");
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

        env.fund(XRP(100000), gw, alice, bob);

        MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice}});

        env(pay(gw, alice, usd(500)));

        env(offer(alice, XRP(150'000), usd(50)));
        env(offer(bob, usd(1), XRP(3'000)));

        auto jrr = ledgerEntryMPT(env, bob, usd);
        BEAST_EXPECT(jrr[jss::node][sfMPTAmount.fieldName] == "1");
        jrr = ledgerEntryRoot(env, bob);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            to_string((XRP(100'000) - XRP(3'000) - env.current()->fees().base * 1).xrp()));

        jrr = ledgerEntryMPT(env, alice, usd);
        BEAST_EXPECT(jrr[jss::node][sfMPTAmount.fieldName] == "499");
        jrr = ledgerEntryRoot(env, alice);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            to_string((XRP(100'000) + XRP(3'000) - env.current()->fees().base * 2).xrp()));
    }

    void
    testOfferAcceptThenCancel(FeatureBitset features)
    {
        testcase("Offer Accept then Cancel.");

        using namespace jtx;

        Env env{*this, features};

        MPT const usd = MPTTester({.env = env, .issuer = env.master});

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
    testCurrencyConversionEntire(FeatureBitset features)
    {
        testcase("Currency Conversion: Entire Offer");

        using namespace jtx;

        Env env{*this, features};

        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};

        env.fund(XRP(10'000), gw, alice, bob);
        env.require(Owners(bob, 0));

        MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}});

        env.require(Owners(alice, 1), Owners(bob, 1));

        env(pay(gw, alice, usd(100)));
        auto const bobOfferSeq = env.seq(bob);
        env(offer(bob, usd(100), XRP(500)));

        env.require(Owners(alice, 1), Owners(bob, 2));
        auto jro = ledgerEntryOffer(env, bob, bobOfferSeq);
        BEAST_EXPECT(jro[jss::node][jss::TakerGets] == XRP(500).value().getText());
        BEAST_EXPECT(
            jro[jss::node][jss::TakerPays] == usd(100).value().getJson(JsonOptions::Values::None));

        env(pay(alice, alice, XRP(500)), Sendmax(usd(100)));

        auto jrr = ledgerEntryMPT(env, alice, usd);
        BEAST_EXPECT(!jrr[jss::node].isMember(sfMPTAmount.fieldName));
        jrr = ledgerEntryRoot(env, alice);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            to_string((XRP(10'000) + XRP(500) - env.current()->fees().base * 2).xrp()));

        jrr = ledgerEntryMPT(env, bob, usd);
        BEAST_EXPECT(jrr[jss::node][sfMPTAmount.fieldName] == "100");

        jro = ledgerEntryOffer(env, bob, bobOfferSeq);
        BEAST_EXPECT(jro[jss::error] == "entryNotFound");

        env.require(Owners(alice, 1), Owners(bob, 1));
    }

    void
    testCurrencyConversionIntoDebt(FeatureBitset features)
    {
        testcase("Currency Conversion: Offerer Into Debt");

        using namespace jtx;
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const carol = Account{"carol"};

        auto test = [&](auto&& issue1, auto&& issue2, auto&& issue3) {
            Env env{*this, features};

            env.fund(XRP(10'000), alice, bob, carol);

            auto const usd =
                issue1({.env = env, .token = "USD", .issuer = alice, .holders = {bob}});
            auto const eurc =
                issue2({.env = env, .token = "EUC", .issuer = carol, .holders = {alice}});
            auto const eurb =
                issue3({.env = env, .token = "EUB", .issuer = bob, .holders = {carol}});

            auto const bobOfferSeq = env.seq(bob);
            env(offer(bob, usd(50), eurc(200)), Ter(tecUNFUNDED_OFFER));

            env(offer(alice, eurc(200), usd(50)));

            auto jro = ledgerEntryOffer(env, bob, bobOfferSeq);
            BEAST_EXPECT(jro[jss::error] == "entryNotFound");
        };
        testHelper3TokensMix(test);
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

        env.fund(XRP(10'000), gw, alice, bob);

        MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}});

        env(pay(gw, alice, usd(200)));

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
        auto jrr = ledgerEntryMPT(env, alice, usd);
        BEAST_EXPECT(jrr[jss::node][sfMPTAmount.fieldName] == "160");
        // alice now has 200 more XRP from the payment
        jrr = ledgerEntryRoot(env, alice);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            to_string((XRP(10'000) + XRP(200) - env.current()->fees().base * 2).xrp()));

        // bob got 40 USD from partial consumption of the offer
        jrr = ledgerEntryMPT(env, bob, usd);
        BEAST_EXPECT(jrr[jss::node][sfMPTAmount.fieldName] == "40");

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
        // only 300 XRP should have been payed since that's all
        // that remained in the offer from bob. The alice balance is now
        // 100 USD because another 60 USD were transferred to bob in the second
        // payment
        jrr = ledgerEntryMPT(env, alice, usd);
        BEAST_EXPECT(jrr[jss::node][sfMPTAmount.fieldName] == "100");
        jrr = ledgerEntryRoot(env, alice);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            to_string((XRP(10'000) + XRP(200) + XRP(300) - env.current()->fees().base * 4).xrp()));

        // bob now has 100 USD - 40 from the first payment and 60 from the
        // second (partial) payment
        jrr = ledgerEntryMPT(env, bob, usd);
        BEAST_EXPECT(jrr[jss::node][sfMPTAmount.fieldName] == "100");
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

        env.fund(XRP(10'000), gw, alice, bob, carol);

        MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {carol, bob}});

        env(pay(gw, carol, usd(500)));

        auto const carolOfferSeq = env.seq(carol);
        env(offer(carol, XRP(500), usd(50)));

        env(pay(alice, bob, usd(25)), Sendmax(XRP(333)));

        auto jrr = ledgerEntryMPT(env, bob, usd);
        BEAST_EXPECT(jrr[jss::node][sfMPTAmount.fieldName] == "25");

        jrr = ledgerEntryMPT(env, carol, usd);
        BEAST_EXPECT(jrr[jss::node][sfMPTAmount.fieldName] == "475");

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

        env.fund(XRP(10'000), gw, alice, bob, carol);

        MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice, carol}});

        env(pay(gw, alice, usd(500)));

        auto const carolOfferSeq = env.seq(carol);
        env(offer(carol, usd(50), XRP(500)));

        env(pay(alice, bob, XRP(250)), Sendmax(usd(333)));

        auto jrr = ledgerEntryMPT(env, alice, usd);
        BEAST_EXPECT(jrr[jss::node][sfMPTAmount.fieldName] == "475");

        jrr = ledgerEntryMPT(env, carol, usd);
        BEAST_EXPECT(jrr[jss::node][sfMPTAmount.fieldName] == "25");

        jrr = ledgerEntryRoot(env, bob);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            std::to_string(XRP(10'000).value().mantissa() + XRP(250).value().mantissa()));

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
        auto const gw1 = Account{"gateway_1"};
        auto const gw2 = Account{"gateway_2"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const carol = Account{"carol"};
        auto const dan = Account{"dan"};

        auto test = [&](auto&& issue1, auto&& issue2) {
            Env env{*this, features};

            env.fund(XRP(10'000), gw1, gw2, alice, bob, carol, dan);

            auto const usd =
                issue1({.env = env, .token = "USD", .issuer = gw1, .holders = {alice, carol}});
            auto const eur =
                issue1({.env = env, .token = "EUR", .issuer = gw2, .holders = {bob, dan}});

            env(pay(gw1, alice, usd(500)));
            env(pay(gw2, dan, eur(400)));

            auto const carolOfferSeq = env.seq(carol);
            env(offer(carol, usd(50), XRP(500)));

            auto const danOfferSeq = env.seq(dan);
            env(offer(dan, XRP(500), eur(50)));

            json::Value jtp{json::ValueType::Array};
            jtp[0u][0u][jss::currency] = "XRP";
            env(pay(alice, bob, eur(30)), Json(jss::Paths, jtp), Sendmax(usd(333)));

            BEAST_EXPECT(env.balance(alice, usd) == usd(470));
            BEAST_EXPECT(env.balance(bob, eur) == eur(30));
            BEAST_EXPECT(env.balance(carol, usd) == usd(30));
            BEAST_EXPECT(env.balance(dan, eur) == eur(370));

            auto jro = ledgerEntryOffer(env, carol, carolOfferSeq);
            BEAST_EXPECT(jro[jss::node][jss::TakerGets] == XRP(200).value().getText());
            BEAST_EXPECT(
                jro[jss::node][jss::TakerPays] ==
                usd(20).value().getJson(JsonOptions::Values::None));

            jro = ledgerEntryOffer(env, dan, danOfferSeq);
            BEAST_EXPECT(
                jro[jss::node][jss::TakerGets] ==
                eur(20).value().getJson(JsonOptions::Values::None));
            BEAST_EXPECT(jro[jss::node][jss::TakerPays] == XRP(200).value().getText());
        };
        testHelper2TokensMix(test);
    }

    void
    testBridgedSecondLegDry(FeatureBitset features)
    {
        // At least with Taker bridging, a sensitivity was identified if the
        // second leg goes dry before the first one.  This test exercises that
        // case.
        testcase("Auto Bridged Second Leg Dry");

        using namespace jtx;
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const gw{"gateway"};

        auto test = [&](auto&& issue1, auto&& issue2) {
            Env env(*this, features);

            env.fund(XRP(100'000'000), alice, bob, carol, gw);
            env.close();

            auto const usd =
                issue1({.env = env, .token = "USD", .issuer = gw, .holders = {alice, carol}});
            auto const eur = issue1({.env = env, .token = "EUR", .issuer = gw, .holders = {bob}});

            env(pay(gw, alice, usd(10)));
            env(pay(gw, carol, usd(3)));

            env(offer(alice, eur(2), XRP(1)));
            env(offer(alice, eur(2), XRP(1)));

            env(offer(alice, XRP(1), usd(4)));
            env(offer(carol, XRP(1), usd(3)));
            env.close();

            // Bob offers to buy 10 USD for 10 EUR.
            //  1. He spends 2 EUR taking Alice's auto-bridged offers and
            //     gets 4 USD for that.
            //  2. He spends another 2 EUR taking Alice's last EUR->XRP offer
            //  and
            //     Carol's XRP-USD offer.  He gets 3 USD for that.
            // The key for this test is that Alice's XRP->USD leg goes dry
            // before Alice's EUR->XRP.  The XRP->USD leg is the second leg
            // which showed some sensitivity.
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
        };
        testHelper2TokensMix(test);
    }

    void
    testOfferFeesConsumeFunds(FeatureBitset features)
    {
        testcase("Offer Fees Consume Funds");

        using namespace jtx;
        auto const gw1 = Account{"gateway_1"};
        auto const gw2 = Account{"gateway_2"};
        auto const gw3 = Account{"gateway_3"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};

        auto test = [&](auto&& issue1, auto&& issue2, auto&& issue3) {
            Env env{*this, features};

            // Provide micro amounts to compensate for fees to make results
            // round nice. reserve: Alice has 3 entries in the ledger, via trust
            // lines fees:
            //  1 for each trust limit == 3 (alice < mtgox/amazon/bitstamp) +
            //  1 for payment          == 4
            auto const base = env.current()->fees().base;
            auto const startingXrp = XRP(100) + env.current()->fees().accountReserve(3) + base * 4;

            env.fund(startingXrp, gw1, gw2, gw3, alice, bob);
            env.close();

            auto const usD1 =
                issue1({.env = env, .token = "US1", .issuer = gw1, .holders = {alice, bob}});
            auto const usD2 =
                issue2({.env = env, .token = "US2", .issuer = gw2, .holders = {alice, bob}});
            auto const usD3 =
                issue3({.env = env, .token = "US3", .issuer = gw3, .holders = {alice}});

            env(pay(gw1, bob, usD1(500)));

            env(offer(bob, XRP(200), usD1(200)));
            // Alice has 350 fees - a reserve of 50 = 250 reserve = 100
            // available. Ask for more than available to prove reserve works.
            env(offer(alice, usD1(200), XRP(200)));

            BEAST_EXPECT(env.balance(alice, usD1) == usD1(100));
            BEAST_EXPECT(env.balance(alice) == STAmount(env.current()->fees().accountReserve(3)));

            BEAST_EXPECT(env.balance(bob, usD1) == usD1(400));
        };
        testHelper3TokensMix(test);
    }

    void
    testOfferCreateThenCross(FeatureBitset features)
    {
        testcase("Offer Create, then Cross");

        using namespace jtx;

        Env env{*this, features};
        env.enableFeature(fixUniversalNumber);

        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};

        env.fund(XRP(10'000), gw, alice, bob);

        MPT const cur =
            MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}, .transferFee = 5'000});

        env(pay(gw, bob, cur(100)));

        env(offer(alice, cur(50'000), XRP(150'000)));
        env(offer(bob, XRP(100), cur(100)));

        auto jrr = ledgerEntryMPT(env, alice, cur);
        BEAST_EXPECT(jrr[jss::node][sfMPTAmount.fieldName] == "34");

        jrr = ledgerEntryMPT(env, bob, cur);
        BEAST_EXPECT(jrr[jss::node][sfMPTAmount.fieldName] == "64");
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

        auto const startingXrp =
            XRP(100) + env.current()->fees().accountReserve(1) + env.current()->fees().base * 2;

        env.fund(startingXrp, gw, alice, bob);

        MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}});

        env(pay(gw, bob, usd(500)));

        env(offer(bob, XRP(200), usd(200)), Json(jss::Flags, tfSell));
        // Alice has 350 + fees - a reserve of 50 = 250 reserve = 100 available.
        // Alice has 350 + fees - a reserve of 50 = 250 reserve = 100 available.
        // Ask for more than available to prove reserve works.
        env(offer(alice, usd(200), XRP(200)), Json(jss::Flags, tfSell));

        auto jrr = ledgerEntryMPT(env, alice, usd);
        BEAST_EXPECT(jrr[jss::node][sfMPTAmount.fieldName] == "100");
        jrr = ledgerEntryRoot(env, alice);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            STAmount(env.current()->fees().accountReserve(1)).getText());

        jrr = ledgerEntryMPT(env, bob, usd);
        BEAST_EXPECT(jrr[jss::node][sfMPTAmount.fieldName] == "400");
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

        auto const startingXrp =
            XRP(100) + env.current()->fees().accountReserve(1) + env.current()->fees().base * 2;

        env.fund(startingXrp, gw, alice, bob);

        MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}});

        env(pay(gw, bob, usd(500)));

        env(offer(bob, XRP(100), usd(200)));
        // Alice has 350 fees - a reserve of 50 = 250 reserve = 100 available.
        // Ask for more than available to prove reserve works.
        // Taker pays 100 USD for 100 XRP.
        // Selling XRP.
        // Will sell all 100 XRP and get more USD than asked for.
        env(offer(alice, usd(100), XRP(100)), Json(jss::Flags, tfSell));

        auto jrr = ledgerEntryMPT(env, alice, usd);
        BEAST_EXPECT(jrr[jss::node][sfMPTAmount.fieldName] == "200");
        jrr = ledgerEntryRoot(env, alice);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            STAmount(env.current()->fees().accountReserve(1)).getText());

        jrr = ledgerEntryMPT(env, bob, usd);
        BEAST_EXPECT(jrr[jss::node][sfMPTAmount.fieldName] == "300");
    }

    void
    testGatewayCrossCurrency(FeatureBitset features)
    {
        testcase("Client Issue #535: Gateway Cross Currency");

        using namespace jtx;
        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};

        auto test = [&](auto&& issue1, auto&& issue2) {
            Env env{*this, features};

            auto const base = env.current()->fees().base;
            auto const startingXrp =
                XRP(100.1) + env.current()->fees().accountReserve(1) + base * 2;

            env.fund(startingXrp, gw, alice, bob);
            env.close();

            auto const xts =
                issue1({.env = env, .token = "XTS", .issuer = gw, .holders = {alice, bob}});
            auto const xxx =
                issue2({.env = env, .token = "XXX", .issuer = gw, .holders = {alice, bob}});
            env.close();

            env(pay(gw, alice, xts(1'000)));
            env(pay(gw, alice, xxx(100)));
            env(pay(gw, bob, xts(1'000)));
            env(pay(gw, bob, xxx(100)));

            env(offer(alice, xts(1'000), xxx(100)));

            // WS client is used here because the RPC client could not
            // be convinced to pass the build_path argument
            auto wsc = makeWSClient(env.app().config());
            json::Value payment;
            payment[jss::secret] = toBase58(generateSeed("bob"));
            payment[jss::id] = env.seq(bob);
            payment[jss::build_path] = true;
            payment[jss::tx_json] = pay(bob, bob, xxx(1));
            payment[jss::tx_json][jss::Sequence] =
                env.current()->read(keylet::account(bob.id()))->getFieldU32(sfSequence);
            payment[jss::tx_json][jss::Fee] = to_string(env.current()->fees().base);
            payment[jss::tx_json][jss::SendMax] =
                xts(15).value().getJson(JsonOptions::Values::None);
            auto jrr = wsc->invoke("submit", payment);
            BEAST_EXPECT(jrr[jss::status] == "success");
            BEAST_EXPECT(jrr[jss::result][jss::engine_result] == "tesSUCCESS");
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(jrr.isMember(jss::jsonrpc) && jrr[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(jrr.isMember(jss::ripplerpc) && jrr[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jrr.isMember(jss::id) && jrr[jss::id] == 5);
            }

            BEAST_EXPECT(env.balance(alice, xts) == xts(1010));
            BEAST_EXPECT(env.balance(alice, xxx) == xxx(99));

            BEAST_EXPECT(env.balance(bob, xts) == xts(990));
            BEAST_EXPECT(env.balance(bob, xxx) == xxx(101));
        };
        testHelper2TokensMix(test);
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

        Env env{*this, features};

        env.fund(XRP(10'000'000), gw);
        env.close();

        auto musd = MPTTester({.env = env, .issuer = gw});
        MPT const usd = musd;

        // The fee that's charged for transactions
        auto const f = env.current()->fees().base;

        // To keep things simple all offers are 1 : 1 for XRP : USD.
        enum class PreAuthType { NoPreAuth, AcctPreAuth };
        struct TestData
        {
            std::string account;      // Account operated on
            STAmount fundXrp;         // Account funded with
            int bookAmount;           // USD -> XRP offer on the books
            PreAuthType preAuth;      // If true, pre-auth MPToken
            int offerAmount;          // Account offers this much XRP -> USD
            TER tec;                  // Returned tec code
            STAmount spentXrp;        // Amount removed from fundXrp
            PrettyAmount balanceUsd;  // Balance on account end
            int offers;               // Offers on account
            int owners;               // Owners on account
            int scale = 1;            // Scale MPT
        };

        // clang-format off
        TestData const tests[]{
            // acct                     fundXrp        bookAmt   preTrust  offerAmt                   tec     spentXrp       balanceUSD offers  owners scale
            {.account="ann",             .fundXrp=reserve(env, 0) + 0 * f,    .bookAmount=1,   .preAuth=PreAuthType::NoPreAuth, .offerAmount=1000,      .tec=tecUNFUNDED_OFFER,               .spentXrp=f, .balanceUsd=usd(      0),    .offers=0, .owners=0},  // Account is at the reserve, and will dip below once fees are subtracted.
            {.account="bev",             .fundXrp=reserve(env, 0) + 1 * f,    .bookAmount=1,   .preAuth=PreAuthType::NoPreAuth, .offerAmount=1000,      .tec=tecUNFUNDED_OFFER,               .spentXrp=f, .balanceUsd=usd(      0),    .offers=0, .owners=0},  // Account has just enough for the reserve and the fee.
            {.account="cam",             .fundXrp=reserve(env, 0) + 2 * f,    .bookAmount=0,   .preAuth=PreAuthType::NoPreAuth, .offerAmount=1000, .tec=tecINSUF_RESERVE_OFFER,               .spentXrp=f, .balanceUsd=usd(      0),    .offers=0, .owners=0},  // Account has enough for the reserve, the fee and the offer, and a bit more, but not enough for the reserve after the offer is placed.
            {.account="deb",             .fundXrp=reserve(env, 0) + 2 * f,    .bookAmount=1,   .preAuth=PreAuthType::NoPreAuth, .offerAmount=1000,             .tec=tesSUCCESS,           .spentXrp=2 * f, .balanceUsd=usd(      1),    .offers=0, .owners=1, .scale=100000},  // Account has enough to buy a little USD then the offer runs dry.
            {.account="eve",             .fundXrp=reserve(env, 1) + 0 * f,    .bookAmount=0,   .preAuth=PreAuthType::NoPreAuth, .offerAmount=1000,             .tec=tesSUCCESS,               .spentXrp=f, .balanceUsd=usd(      0),    .offers=1, .owners=1},  // No offer to cross
            {.account="flo",             .fundXrp=reserve(env, 1) + 0 * f,    .bookAmount=1,   .preAuth=PreAuthType::NoPreAuth, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP(   1)   + f, .balanceUsd=usd(      1),    .offers=0, .owners=1},
            {.account="gay",             .fundXrp=reserve(env, 1) + 1 * f, .bookAmount=1000,   .preAuth=PreAuthType::NoPreAuth, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP(  50)   + f, .balanceUsd=usd(     50),    .offers=0, .owners=1},
            {.account="hye", .fundXrp=XRP(1000)                   + 1 * f, .bookAmount=1000,   .preAuth=PreAuthType::NoPreAuth, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP( 800)   + f, .balanceUsd=usd(    800),    .offers=0, .owners=1},
            {.account="ivy", .fundXrp=XRP(   1) + reserve(env, 1) + 1 * f,    .bookAmount=1,   .preAuth=PreAuthType::NoPreAuth, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP(   1)   + f, .balanceUsd=usd(      1),    .offers=0, .owners=1},
            {.account="joy", .fundXrp=XRP(   1) + reserve(env, 2) + 1 * f,    .bookAmount=1,   .preAuth=PreAuthType::NoPreAuth, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP(   1)   + f, .balanceUsd=usd(      1),    .offers=1, .owners=2},
            {.account="kim", .fundXrp=XRP( 900) + reserve(env, 2) + 1 * f,  .bookAmount=999,   .preAuth=PreAuthType::NoPreAuth, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP( 999)   + f, .balanceUsd=usd(    999),    .offers=0, .owners=1},
            {.account="liz", .fundXrp=XRP( 998) + reserve(env, 0) + 1 * f,  .bookAmount=999,   .preAuth=PreAuthType::NoPreAuth, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP( 998)   + f, .balanceUsd=usd(    998),    .offers=0, .owners=1},
            {.account="meg", .fundXrp=XRP( 998) + reserve(env, 1) + 1 * f,  .bookAmount=999,   .preAuth=PreAuthType::NoPreAuth, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP( 999)   + f, .balanceUsd=usd(    999),    .offers=0, .owners=1},
            {.account="nia", .fundXrp=XRP( 998) + reserve(env, 2) + 1 * f,  .bookAmount=999,   .preAuth=PreAuthType::NoPreAuth, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP( 999)   + f, .balanceUsd=usd(    999),    .offers=1, .owners=2},
            {.account="ova", .fundXrp=XRP( 999) + reserve(env, 0) + 1 * f, .bookAmount=1000,   .preAuth=PreAuthType::NoPreAuth, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP( 999)   + f, .balanceUsd=usd(    999),    .offers=0, .owners=1},
            {.account="pam", .fundXrp=XRP( 999) + reserve(env, 1) + 1 * f, .bookAmount=1000,   .preAuth=PreAuthType::NoPreAuth, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP(1000)   + f, .balanceUsd=usd(   1000),    .offers=0, .owners=1},
            {.account="rae", .fundXrp=XRP( 999) + reserve(env, 2) + 1 * f, .bookAmount=1000,   .preAuth=PreAuthType::NoPreAuth, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP(1000)   + f, .balanceUsd=usd(   1000),    .offers=0, .owners=1},
            {.account="sue", .fundXrp=XRP(1000) + reserve(env, 2) + 1 * f,    .bookAmount=0,   .preAuth=PreAuthType::NoPreAuth, .offerAmount=1000,             .tec=tesSUCCESS,               .spentXrp=f, .balanceUsd=usd(      0),    .offers=1, .owners=1},
            //---------------- Pre-created MPT ---------------------
            // Unlike from IOU, an issuer can't pre-create MPToken for an account (see similar tests in Offer_test.cpp)
            {.account="ned",             .fundXrp=reserve(env, 1) + 0 * f,    .bookAmount=1, .preAuth=PreAuthType::AcctPreAuth, .offerAmount=1000,      .tec=tecUNFUNDED_OFFER,           .spentXrp=2 * f, .balanceUsd=usd(      0),    .offers=0, .owners=1},
            {.account="ole",             .fundXrp=reserve(env, 1) + 1 * f,    .bookAmount=1, .preAuth=PreAuthType::AcctPreAuth, .offerAmount=1000,      .tec=tecUNFUNDED_OFFER,           .spentXrp=2 * f, .balanceUsd=usd(      0),    .offers=0, .owners=1},
            {.account="pat",             .fundXrp=reserve(env, 1) + 2 * f,    .bookAmount=0, .preAuth=PreAuthType::AcctPreAuth, .offerAmount=1000,      .tec=tecUNFUNDED_OFFER,           .spentXrp=2 * f, .balanceUsd=usd(      0),    .offers=0, .owners=1},
            {.account="quy",             .fundXrp=reserve(env, 1) + 2 * f,    .bookAmount=1, .preAuth=PreAuthType::AcctPreAuth, .offerAmount=1000,      .tec=tecUNFUNDED_OFFER,           .spentXrp=2 * f, .balanceUsd=usd(      0),    .offers=0, .owners=1},
            {.account="ron",             .fundXrp=reserve(env, 1) + 3 * f,    .bookAmount=0, .preAuth=PreAuthType::AcctPreAuth, .offerAmount=1000, .tec=tecINSUF_RESERVE_OFFER,           .spentXrp=2 * f, .balanceUsd=usd(      0),    .offers=0, .owners=1},
            {.account="syd",             .fundXrp=reserve(env, 1) + 3 * f,    .bookAmount=1, .preAuth=PreAuthType::AcctPreAuth, .offerAmount=1000,             .tec=tesSUCCESS,           .spentXrp=3 * f, .balanceUsd=usd(      1),    .offers=0, .owners=1, .scale=100000},
            {.account="ted", .fundXrp=XRP(  20) + reserve(env, 1) + 2 * f, .bookAmount=1000, .preAuth=PreAuthType::AcctPreAuth, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP(20) + 2 * f, .balanceUsd=usd(     20),    .offers=0, .owners=1},
            {.account="uli",             .fundXrp=reserve(env, 2) + 0 * f,    .bookAmount=0, .preAuth=PreAuthType::AcctPreAuth, .offerAmount=1000, .tec=tecINSUF_RESERVE_OFFER,           .spentXrp=2 * f, .balanceUsd=usd(      0),    .offers=0, .owners=1},
            {.account="vic",             .fundXrp=reserve(env, 2) + 0 * f,    .bookAmount=1, .preAuth=PreAuthType::AcctPreAuth, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP( 1) + 2 * f, .balanceUsd=usd(      1),    .offers=0, .owners=1},
            {.account="wes",             .fundXrp=reserve(env, 2) + 1 * f,    .bookAmount=0, .preAuth=PreAuthType::AcctPreAuth, .offerAmount=1000,             .tec=tesSUCCESS,           .spentXrp=2 * f, .balanceUsd=usd(      0),    .offers=1, .owners=2},
            {.account="xan",             .fundXrp=reserve(env, 2) + 1 * f,    .bookAmount=1, .preAuth=PreAuthType::AcctPreAuth, .offerAmount=1000,             .tec=tesSUCCESS, .spentXrp=XRP( 1) + 2 * f, .balanceUsd=usd(      1),    .offers=1, .owners=2},
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
                env(offer(gw, XRP(book), usd(book * t.scale)));
            env.close();
            std::uint32_t const gwOfferSeq = env.seq(gw) - 1;

            // Optionally pre-authorize MPT for acct.
            // Note this is not really part of the test, so we expect there
            // to be enough XRP reserve for acct to create the trust line.
            if (t.preAuth == PreAuthType::AcctPreAuth)
                musd.authorize({.account = acct});
            env.close();

            {
                // Acct creates an offer.  This is the heart of the test.
                auto const acctOffer = t.offerAmount;
                env(offer(acct, usd(acctOffer * t.scale), XRP(acctOffer)), Ter(t.tec));
                env.close();
            }
            std::uint32_t const acctOfferSeq = env.seq(acct) - 1;

            auto const expBalanceUsd = [&]() {
                if (t.scale == 1)
                    return t.balanceUsd;
                // crossed offer has XRP available balance of 1 fee
                // mpt to XRP ratio is 10
                return usd(f.value() / 10);
            }();
            BEAST_EXPECT(env.balance(acct, usd) == expBalanceUsd);
            BEAST_EXPECT(env.balance(acct, xrpIssue()) == t.fundXrp - t.spentXrp);
            env.require(offers(acct, t.offers));
            env.require(Owners(acct, t.owners));

            auto acctOffers = offersOnAccount(env, acct);
            BEAST_EXPECT(acctOffers.size() == t.offers);
            if (!acctOffers.empty() && t.offers != 0)
            {
                auto const& acctOffer = *(acctOffers.front());

                auto const leftover = t.offerAmount - t.bookAmount;
                BEAST_EXPECT(acctOffer[sfTakerGets] == XRP(leftover));
                BEAST_EXPECT(acctOffer[sfTakerPays] == usd(leftover));
            }

            if (t.preAuth == PreAuthType::NoPreAuth)
            {
                if (t.balanceUsd.value().signum() != 0)
                {
                    // Verify the correct contents of MPT
                    BEAST_EXPECT(env.balance(acct, usd) == expBalanceUsd);
                }
                else
                {
                    // Verify that no MPT was created.
                    auto const sle = env.le(keylet::mptoken(usd.issuanceID, acct));
                    BEAST_EXPECT(!sle);
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

        Env env{*this, features};

        env.fund(XRP(1'000'000), gw, bob);
        env.close();

        // The fee that's charged for transactions.
        auto const fee = env.current()->fees().base;

        // alice's account has enough for the reserve, one trust line plus two
        // offers, and two fees.
        env.fund(reserve(env, 2) + fee * 2, alice);
        env.close();

        MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice}});

        auto const usdOffer = usd(1'000);
        auto const xrpOffer = XRP(1'000);

        env(pay(gw, alice, usdOffer));
        env.close();
        env.require(Balance(alice, usdOffer), offers(alice, 0), offers(bob, 0));

        // The scenario:
        //   o alice has USD but wants XRP.
        //   o bob has XRP but wants USD.
        auto const aliceXRP = env.balance(alice);
        auto const bobsXRP = env.balance(bob);

        env(offer(alice, xrpOffer, usdOffer));
        env.close();
        env(offer(bob, usdOffer, xrpOffer));

        env.close();
        env.require(
            Balance(alice, usd(0)),
            Balance(bob, usdOffer),
            Balance(alice, aliceXRP + xrpOffer - fee),
            Balance(bob, bobsXRP - xrpOffer - fee),
            offers(alice, 0),
            offers(bob, 0));

        BEAST_EXPECT(env.balance(bob, usd) == usdOffer);

        // Make two more offers that leave one of the offers non-dry.
        env(offer(alice, usd(999), XRP(999)));
        env(offer(bob, xrpOffer, usdOffer));

        env.close();
        env.require(Balance(alice, usd(999)));
        env.require(Balance(bob, usd(1)));
        env.require(offers(alice, 0));
        BEAST_EXPECT(env.balance(bob, usd) == usd(1));
        {
            auto const bobsOffers = offersOnAccount(env, bob);
            BEAST_EXPECT(bobsOffers.size() == 1);
            auto const& bobsOffer = *(bobsOffers.front());

            BEAST_EXPECT(bobsOffer[sfLedgerEntryType] == ltOFFER);
            BEAST_EXPECT(bobsOffer[sfTakerGets] == usd(1));
            BEAST_EXPECT(bobsOffer[sfTakerPays] == XRP(1));
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

        auto test = [&](auto&& issue1, auto&& issue2) {
            Env env{*this, features};

            env.fund(XRP(1000000), gw);
            env.close();

            // The fee that's charged for transactions.
            auto const fee = env.current()->fees().base;

            // Each account has enough for the reserve, two MPT's, one
            // offer, and two fees.
            env.fund(reserve(env, 3) + fee * 3, alice);
            env.fund(reserve(env, 3) + fee * 2, bob);
            env.close();

            auto const usd = issue1({.env = env, .token = "USD", .issuer = gw, .holders = {alice}});
            auto const eur = issue2({.env = env, .token = "EUR", .issuer = gw, .holders = {bob}});

            auto const usdOffer = usd(1'000);
            auto const eurOffer = eur(1'000);

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
            BEAST_EXPECT(env.balance(alice, eur) == eurOffer);
            BEAST_EXPECT(env.balance(bob, usd) == usdOffer);

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
                auto bobsOffers = offersOnAccount(env, bob);
                if (BEAST_EXPECT(bobsOffers.size() == 1))
                {
                    auto const& bobsOffer = *(bobsOffers.front());

                    BEAST_EXPECT(bobsOffer[sfTakerGets] == usd(1));
                    BEAST_EXPECT(bobsOffer[sfTakerPays] == eur(1));
                }
            }

            // alice makes one more offer that cleans out bob's offer.
            env(offer(alice, usd(1), eur(1)));
            env.close();

            env.require(Balance(alice, usd(1'000)));
            env.require(Balance(alice, eur(kNone)));
            env.require(Balance(bob, usd(kNone)));
            env.require(Balance(bob, eur(1'000)));
            env.require(offers(alice, 0));
            env.require(offers(bob, 0));

            // The two MPT that were generated by the offers still here
            // Unlike IOU, MPToken is not automatically deleted
            if constexpr (std::is_same_v<std::decay_t<decltype(eur)>, MPT>)
            {
                BEAST_EXPECT(env.le(keylet::mptoken(eur.issuanceID, alice)));
                auto meur = MPTTester(env, gw, eur, {bob});
                // Delete created MPToken to free up reserve
                meur.authorize({.account = alice, .flags = tfMPTUnauthorize});
            }
            if constexpr (std::is_same_v<std::decay_t<decltype(usd)>, MPT>)
            {
                BEAST_EXPECT(env.le(keylet::mptoken(usd.issuanceID, bob)));
                auto musd = MPTTester(env, gw, usd, {alice});
                // Delete created MPToken to free up reserve
                musd.authorize({.account = bob, .flags = tfMPTUnauthorize});
            }

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
            env.require(Balance(bob, usd(1'000)));
            env.require(Balance(bob, eur(1)));
        };
        testHelper2TokensMix(test);
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

        auto test = [&](auto&& issue1, auto&& issue2) {
            Env env{*this, features};

            env.fund(XRP(1'000'000), gw, alice, bob, carol);
            env.close();

            auto const usd = issue1({.env = env, .token = "USD", .issuer = gw, .holders = {alice}});
            auto const eur = issue2({.env = env, .token = "EUR", .issuer = gw, .holders = {carol}});

            auto const usdOffer = usd(1'000);
            auto const eurOffer = eur(1'000);

            env(pay(gw, alice, usdOffer));
            env(pay(gw, carol, eurOffer));
            env.close();

            // The scenario:
            //   o alice has USD but wants XRP.
            //   o bob has XRP but wants EUR.
            //   o carol has EUR but wants USD.
            // Note that carol's offer must come last.  If carol's offer is
            // placed before bob's or alice's, then autobridging will not occur.
            env(offer(alice, XRP(1'000), usdOffer));
            env(offer(bob, eurOffer, XRP(1'000)));
            auto const bobXrpBalance = env.balance(bob);
            env.close();

            // carol makes an offer that partially consumes alice and bob's
            // offers.
            env(offer(carol, usd(400), eur(400)));
            env.close();

            env.require(
                Balance(alice, usd(600)),
                Balance(bob, eur(400)),
                Balance(carol, usd(400)),
                Balance(bob, bobXrpBalance - XRP(400)),
                offers(carol, 0));
            BEAST_EXPECT(env.balance(bob, eur) == eur(400));
            BEAST_EXPECT(env.balance(carol, usd) == usd(400));
            {
                auto const aliceOffers = offersOnAccount(env, alice);
                BEAST_EXPECT(aliceOffers.size() == 1);
                auto const& aliceOffer = *(aliceOffers.front());

                BEAST_EXPECT(aliceOffer[sfLedgerEntryType] == ltOFFER);
                BEAST_EXPECT(aliceOffer[sfTakerGets] == usd(600));
                BEAST_EXPECT(aliceOffer[sfTakerPays] == XRP(600));
            }
            {
                auto const bobsOffers = offersOnAccount(env, bob);
                BEAST_EXPECT(bobsOffers.size() == 1);
                auto const& bobsOffer = *(bobsOffers.front());

                BEAST_EXPECT(bobsOffer[sfLedgerEntryType] == ltOFFER);
                BEAST_EXPECT(bobsOffer[sfTakerGets] == XRP(600));
                BEAST_EXPECT(bobsOffer[sfTakerPays] == eur(600));
            }

            // carol makes an offer that exactly consumes alice and bob's
            // offers.
            env(offer(carol, usd(600), eur(600)));
            env.close();

            env.require(
                Balance(alice, usd(0)),
                Balance(bob, eurOffer),
                Balance(carol, usdOffer),
                Balance(bob, bobXrpBalance - XRP(1'000)),
                offers(bob, 0),
                offers(carol, 0));
            BEAST_EXPECT(env.balance(bob, eur) == eur(1'000));
            BEAST_EXPECT(env.balance(carol, usd) == usd(1'000));

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
        };
        testHelper2TokensMix(test);
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

        Env env{*this, features};

        env.fund(XRP(10'000'000), gw);

        auto musd = MPTTester({.env = env, .issuer = gw});
        MPT const usd = musd;

        // The fee that's charged for transactions
        auto const f = env.current()->fees().base;

        // To keep things simple all offers are 1 : 1 for XRP : USD.
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
                musd.authorize({.account = acct});
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

        Env env{*this, features};

        env.fund(XRP(10'000'000), gw, alice, bob);

        MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {bob}});

        // bob offers XRP for USD.
        env(pay(gw, bob, usd(100)));
        env.close();
        env(offer(bob, XRP(2'000), usd(20)));
        env.close();
        {
            // alice submits a tfSell | tfFillOrKill offer that does not cross.
            env(offer(alice, usd(21), XRP(2'100), tfSell | tfFillOrKill), Ter(tecKILLED));
            env.close();
            env.require(Balance(alice, usd(kNone)));
            env.require(offers(alice, 0));
            env.require(Balance(bob, usd(100)));
        }
        {
            // alice submits a tfSell | tfFillOrKill offer that crosses.
            // Even though tfSell is present it doesn't matter this time.
            env(offer(alice, usd(20), XRP(2'000), tfSell | tfFillOrKill));
            env.close();
            env.require(Balance(alice, usd(20)));
            env.require(offers(alice, 0));
            env.require(Balance(bob, usd(80)));
        }
        {
            // alice submits a tfSell | tfFillOrKill offer that crosses and
            // returns more than was asked for (because of the tfSell flag).
            env(offer(bob, XRP(2'000), usd(20)));
            env.close();
            env(offer(alice, usd(10), XRP(1'500), tfSell | tfFillOrKill));
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
            env(offer(alice, usd(1), XRP(501), tfSell | tfFillOrKill), Ter(tecKILLED));
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

        auto test = [&](auto&& issue1, auto&& issue2) {
            Env env{*this, features};

            // The fee that's charged for transactions.
            auto const fee = env.current()->fees().base;

            env.fund(XRP(100'000), gw1);
            env.close();

            auto const usd =
                issue1({.env = env, .token = "USD", .issuer = gw1, .transferFee = 25'000});
            using tUSD = std::decay_t<decltype(usd)>;
            {
                auto const ann = Account("ann");
                auto const bob = Account("bob");
                env.fund(XRP(100) + reserve(env, 2) + (fee * 2), ann, bob);
                env.close();

                if constexpr (std::is_same_v<tUSD, MPT>)
                {
                    auto musd = MPTTester(env, gw1, usd);
                    musd.authorize({.account = ann});
                    musd.authorize({.account = bob});
                }
                else
                {
                    env(trust(ann, usd(20'000)));
                    env(trust(bob, usd(20'000)));
                    env.close();
                }

                env(pay(gw1, bob, usd(12'500)));
                env.close();

                // bob offers to sell USD(100) for XRP.  alice takes bob's
                // offer. Notice that although bob only offered USD(100),
                // USD(125) was removed from his account due to the gateway fee.
                //
                // A comparable payment would look like this:
                //   env (pay (bob, alice, USD(100)), Sendmax(USD(125)))
                env(offer(bob, XRP(1), usd(10'000)));
                env.close();

                env(offer(ann, usd(10'000), XRP(1)));
                env.close();

                env.require(Balance(ann, usd(10'000)));
                env.require(Balance(ann, XRP(99) + reserve(env, 2)));
                env.require(offers(ann, 0));

                env.require(Balance(bob, usd(0)));
                env.require(Balance(bob, XRP(101) + reserve(env, 2)));
                env.require(offers(bob, 0));
            }
            {
                // Reverse the order, so the offer in the books is to sell XRP
                // in return for USD.  Gateway rate should still apply
                // identically.
                auto const che = Account("che");
                auto const deb = Account("deb");
                env.fund(XRP(100) + reserve(env, 2) + (fee * 2), che, deb);
                env.close();

                if constexpr (std::is_same_v<tUSD, MPT>)
                {
                    auto musd = MPTTester(env, gw1, usd);
                    musd.authorize({.account = che});
                    musd.authorize({.account = deb});
                }
                else
                {
                    env(trust(che, usd(20'000)));
                    env(trust(deb, usd(20'000)));
                    env.close();
                }

                env(pay(gw1, deb, usd(12'500)));
                env.close();

                env(offer(che, usd(10'000), XRP(1)));
                env.close();

                env(offer(deb, XRP(1), usd(10'000)));
                env.close();

                env.require(Balance(che, usd(10'000)));
                env.require(Balance(che, XRP(99) + reserve(env, 2)));
                env.require(offers(che, 0));

                env.require(Balance(deb, usd(0)));
                env.require(Balance(deb, XRP(101) + reserve(env, 2)));
                env.require(offers(deb, 0));
            }
            {
                auto const eve = Account("eve");
                auto const fyn = Account("fyn");

                env.fund(XRP(20'000) + (fee * 2), eve, fyn);
                env.close();

                if constexpr (std::is_same_v<tUSD, MPT>)
                {
                    auto musd = MPTTester(env, gw1, usd);
                    musd.authorize({.account = eve});
                    musd.authorize({.account = fyn});
                }
                else
                {
                    env(trust(eve, usd(20'000)));
                    env(trust(fyn, usd(20'000)));
                    env.close();
                }

                env(pay(gw1, eve, usd(10'000)));
                env(pay(gw1, fyn, usd(10'000)));
                env.close();

                // This test verifies that the amount removed from an offer
                // accounts for the transfer fee that is removed from the
                // account but not from the remaining offer.
                env(offer(eve, usd(1'000), XRP(4'000)));
                env.close();
                std::uint32_t const eveOfferSeq = env.seq(eve) - 1;

                env(offer(fyn, XRP(2'000), usd(500)));
                env.close();

                env.require(Balance(eve, usd(10'500)));
                env.require(Balance(eve, XRP(18'000)));
                auto const evesOffers = offersOnAccount(env, eve);
                BEAST_EXPECT(evesOffers.size() == 1);
                if (!evesOffers.empty())
                {
                    auto const& evesOffer = *(evesOffers.front());
                    BEAST_EXPECT(evesOffer[sfLedgerEntryType] == ltOFFER);
                    BEAST_EXPECT(evesOffer[sfTakerGets] == XRP(2'000));
                    BEAST_EXPECT(evesOffer[sfTakerPays] == usd(500));
                }
                env(offerCancel(eve, eveOfferSeq));  // For later tests

                env.require(Balance(fyn, usd(9'375)));
                env.require(Balance(fyn, XRP(22'000)));
                env.require(offers(fyn, 0));
            }
            // Start messing with two non-native currencies.
            auto const gw2 = Account("gateway2");

            env.fund(XRP(100'000), gw2);
            env.close();

            auto const eur =
                issue2({.env = env, .token = "EUR", .issuer = gw2, .transferFee = 50'000});
            using tEUR = std::decay_t<decltype(eur)>;
            {
                // Remove XRP from the equation.  Give the two currencies two
                // different transfer rates so we can see both transfer rates
                // apply in the same transaction.
                auto const gay = Account("gay");
                auto const hal = Account("hal");
                env.fund(reserve(env, 3) + (fee * 3), gay, hal);
                env.close();

                if constexpr (std::is_same_v<tUSD, MPT>)
                {
                    auto musd = MPTTester(env, gw1, usd);
                    musd.authorize({.account = gay});
                    musd.authorize({.account = hal});
                }
                else
                {
                    env(trust(gay, usd(20'000)));
                    env(trust(hal, usd(20'000)));
                    env.close();
                }
                if constexpr (std::is_same_v<tEUR, MPT>)
                {
                    auto meur = MPTTester(env, gw2, eur);
                    meur.authorize({.account = gay});
                    meur.authorize({.account = hal});
                }
                else
                {
                    env(trust(gay, eur(20'000)));
                    env(trust(hal, eur(20'000)));
                    env.close();
                }

                env(pay(gw1, gay, usd(12'500)));
                env(pay(gw2, hal, eur(150)));
                env.close();

                env(offer(gay, eur(100), usd(10'000)));
                env.close();

                env(offer(hal, usd(10'000), eur(100)));
                env.close();

                env.require(Balance(gay, usd(0)));
                env.require(Balance(gay, eur(100)));
                env.require(Balance(gay, reserve(env, 3)));
                env.require(offers(gay, 0));

                env.require(Balance(hal, usd(10'000)));
                env.require(Balance(hal, eur(0)));
                env.require(Balance(hal, reserve(env, 3)));
                env.require(offers(hal, 0));
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
                if constexpr (std::is_same_v<tUSD, MPT>)
                {
                    auto musd = MPTTester(env, gw1, usd);
                    musd.authorize({.account = ova});
                    musd.authorize({.account = pat});
                    musd.authorize({.account = qae});
                }
                else
                {
                    env(trust(ova, usd(20'000)));
                    env(trust(pat, usd(20'000)));
                    env(trust(qae, usd(20'000)));
                    env.close();
                }
                if constexpr (std::is_same_v<tEUR, MPT>)
                {
                    auto meur = MPTTester(env, gw2, eur);
                    meur.authorize({.account = ova});
                    meur.authorize({.account = pat});
                    meur.authorize({.account = qae});
                }
                else
                {
                    env(trust(ova, eur(20'000)));
                    env(trust(pat, eur(20'000)));
                    env(trust(qae, eur(20'000)));
                    env.close();
                }

                env(pay(gw1, ova, usd(12'500)));
                env(pay(gw2, qae, eur(150)));
                env.close();

                env(offer(ova, XRP(2), usd(10'000)));
                env(offer(pat, eur(100), XRP(2)));
                env.close();

                env(offer(qae, usd(10'000), eur(100)));
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

                env.require(Balance(qae, usd(10'000)));
                env.require(Balance(qae, eur(0)));
                env.require(Balance(qae, XRP(2) + reserve(env, 3)));
                env.require(offers(qae, 0));
            }
        };
        testHelper2TokensMix(test);

        // Payment trIn: MPT transfer fee must be charged when the payment
        // destination is the MPT issuer and MPT crosses the DEX (1-hop).
        // Bug: rate() returned parity because strandDst_ == MPT issuer.
        // Fix: parity only when this asset IS the final delivered asset.
        {
            auto const gw = Account("gw_tr1");
            auto const alice = Account("alice_tr1");
            auto const bob = Account("bob_tr1");

            Env env{*this, features};
            env.fund(XRP(10'000), gw, alice, bob);
            env.close();

            MPT const usd = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice, bob}, .transferFee = 25'000});

            // alice needs MPT(1250): MPT(1000) to bob's offer + MPT(250) transfer fee (25%)
            env(pay(gw, alice, usd(1'250)));
            // bob's offer: give XRP(1000), want MPT(1000)
            env(offer(bob, usd(1'000), XRP(1'000)));
            env.close();

            // alice pays gw (MPT issuer) XRP(1000) using MPT as source
            // strand: alice -> [MPT/XRP BookStep] -> gw
            // strandDst_ = gw = MPT issuer, strandDeliver_ = XRP
            // trIn = rate(MPT, gw): fix charges 25% (MPT != strandDeliver_)
            env(pay(alice, gw, XRP(1'000)), Path(~XRP), Sendmax(usd(1'250)));
            env.close();

            // alice consumed all MPT(1250): MPT(1000) to bob + MPT(250) fee
            BEAST_EXPECT(env.balance(alice, usd) == usd(0));
            // bob received MPT(1000) net
            BEAST_EXPECT(env.balance(bob, usd) == usd(1'000));
        }

        // Payment trIn (2-hop): MPT transfer fee must be charged when MPT is
        // intermediate and the destination is the MPT issuer.
        // BookStep2(MPT/XRP) prevStep=BookStep1 returns redeems direction
        // (ownerPaysTransferFee_=false for Payment), so trIn applies.
        // Bug: parity because strandDst_ == MPT issuer.
        // Fix: 25% fee because MPT != strandDeliver_(XRP).
        {
            auto const gw = Account("gw_tr2");
            auto const gw2 = Account("gw2_tr2");
            auto const alice = Account("alice_tr2");
            auto const bob = Account("bob_tr2");
            auto const carol = Account("carol_tr2");

            Env env{*this, features};
            env.fund(XRP(10'000), gw, gw2, alice, bob, carol);
            env.close();

            MPT const musd = MPTTester(
                {.env = env, .issuer = gw, .holders = {bob, carol}, .transferFee = 25'000});
            auto const gusd = gw2["USD"];

            env(trust(alice, gusd(10'000)));
            env(trust(bob, gusd(10'000)));
            env.close();

            env(pay(gw2, alice, gusd(1'000)));
            env(pay(gw, bob, musd(1'000)));
            env.close();

            // bob's offer: give MPT(1000), want GUSD(1000)
            env(offer(bob, gusd(1'000), musd(1'000)));
            // carol's offer: give XRP(800), want MPT(800)
            env(offer(carol, musd(800), XRP(800)));
            env.close();

            // Payment: alice GUSD -> [BookStep1: GUSD/MUSD] -> [BookStep2: MUSD/XRP] -> gw XRP
            // strandDst_ = gw = MPT issuer, strandDeliver_ = XRP
            // BookStep2 trIn: fix = 1.25 -> upstream needs MUSD(1000) for carol's MUSD(800) offer
            // => alice must provide full GUSD(1000) to bob's offer; without fix alice only pays
            // GUSD(800)
            env(pay(alice, gw, XRP(800)), Path(~musd), Sendmax(gusd(1'000)));
            env.close();

            // alice spent all GUSD(1000); bug would leave GUSD(200) unspent
            BEAST_EXPECT(env.balance(alice, gusd) == gusd(0));
            // bob gave MPT(1000) and received GUSD(1000)
            BEAST_EXPECT(env.balance(bob, musd) == musd(0));
            // carol received MPT(800) net (MPT(200) went to gw as fee)
            BEAST_EXPECT(env.balance(carol, musd) == musd(800));
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

        Env env{*this, features};

        // The fee that's charged for transactions.
        auto const fee = env.current()->fees().base;
        auto const startBalance = XRP(1'000'000);

        env.fund(startBalance + (fee * 5), gw);
        env.close();

        MPT const usd = MPTTester({.env = env, .issuer = gw});

        env(offer(gw, usd(60), XRP(600)));
        env.close();
        env(offer(gw, usd(60), XRP(600)));
        env.close();
        env(offer(gw, usd(60), XRP(600)));
        env.close();

        // three offers + MPTokenIssuance
        env.require(Owners(gw, 4));
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
        env(offer(gw, XRP(1'000), usd(100)));
        env.close();
        env.require(Owners(gw, 2));
        env.require(offers(gw, 1));
        env.require(Balance(gw, startBalance));

        gwOffers = offersOnAccount(env, gw);
        BEAST_EXPECT(gwOffers.size() == 1);
        for (auto const& offerPtr : gwOffers)
        {
            auto const& offer = *offerPtr;
            BEAST_EXPECT(offer[sfLedgerEntryType] == ltOFFER);
            BEAST_EXPECT(offer[sfTakerGets] == usd(100));
            BEAST_EXPECT(offer[sfTakerPays] == XRP(1'000));
        }
    }

    void
    testSelfCrossOffer2(FeatureBitset features)
    {
        using namespace jtx;

        auto const gw1 = Account("gateway1");
        auto const gw2 = Account("gateway2");
        auto const alice = Account("alice");

        auto test = [&](auto&& issue1, auto&& issue2) {
            Env env{*this, features};

            env.fund(XRP(1'000'000), gw1, gw2);
            env.close();

            auto const usd = issue1({.env = env, .token = "USD", .issuer = gw1});
            using tUSD = std::decay_t<decltype(usd)>;
            auto const eur = issue2({.env = env, .token = "EUR", .issuer = gw2});
            using tEUR = std::decay_t<decltype(eur)>;

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
                {"ann", reserve(env, 3) + f * 4, usd(1000), eur(1000),             tesSUCCESS,             tesSUCCESS},
                {"bev", reserve(env, 3) + f * 4, usd(   1), eur(1000),             tesSUCCESS,             tesSUCCESS},
                {"cam", reserve(env, 3) + f * 4, usd(1000), eur(   1),             tesSUCCESS,             tesSUCCESS},
                {"deb", reserve(env, 3) + f * 4, usd(   0), eur(   1),             tesSUCCESS,      tecUNFUNDED_OFFER},
                {"eve", reserve(env, 3) + f * 4, usd(   1), eur(   0),      tecUNFUNDED_OFFER,             tesSUCCESS},
                {"flo", reserve(env, 3) +     0, usd(1000), eur(1000), tecINSUF_RESERVE_OFFER, tecINSUF_RESERVE_OFFER},
            };
            //clang-format on

            for (auto const& t : tests)
            {
                auto const acct = Account{t.acct};
                env.fund(t.fundXRP, acct);
                env.close();

                if constexpr (std::is_same_v<tUSD, MPT>)
                {
                    auto musd = MPTTester(env, gw1, usd);
                    musd.authorize({.account = acct});
                }
                else
                {
                    env(trust(acct, usd(1'000)));
                    env.close();
                }
                if constexpr (std::is_same_v<tEUR, MPT>)
                {
                    auto meur = MPTTester(env, gw2, eur);
                    meur.authorize({.account = acct});
                }
                else
                {
                    env(trust(acct, eur(1'000)));
                    env.close();
                }

                if (t.fundUSD > usd(0))
                    env(pay(gw1, acct, t.fundUSD));
                if (t.fundEUR > eur(0))
                    env(pay(gw2, acct, t.fundEUR));
                env.close();

                env(offer(acct, usd(500), eur(600)), Ter(t.firstOfferTec));
                env.close();
                std::uint32_t const firstOfferSeq = env.seq(acct) - 1;

                int offerCount = t.firstOfferTec == tesSUCCESS ? 1 : 0;
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

                offerCount = t.secondOfferTec == tesSUCCESS ? 1 : offerCount;
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
        };
        testHelper2TokensMix(test);
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
        auto const f = env.current()->fees().base;

        env.fund(XRP(50'000) + f, alice, bob);
        env.close();

        MPT const usd = MPTTester({.env = env, .issuer = bob});

        env(offer(alice, usd(5'000), XRP(50'000)));
        env.close();

        // This offer should take alice's offer up to Alice's reserve.
        env(offer(bob, XRP(50'000), usd(5'000)));
        env.close();

        // alice's offer should have been removed, since she's down to her
        // XRP reserve.
        env.require(Balance(alice, XRP(250)));
        env.require(Owners(alice, 1));
        env.require(mptokens(alice, 1));

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
    testDirectToDirectPath(FeatureBitset features)
    {
        // The offer crossing code expects that a DirectStep is always
        // preceded by a BookStep.  In one instance the default path
        // was not matching that assumption.  Here we recreate that case
        // so we can prove the bug stays fixed.
        testcase("Direct to Direct path");

        using namespace jtx;
        auto const ann = Account("ann");
        auto const bob = Account("bob");
        auto const cam = Account("cam");

        auto test = [&](auto&& issue1, auto&& issue2) {
            Env env{*this, features};

            auto const fee = env.current()->fees().base;
            env.fund(reserve(env, 4) + (fee * 5), ann, bob, cam);
            env.close();

            auto const aBux = issue1(
                {.env = env, .token = "AUX", .issuer = ann, .holders = {cam}});
            auto const bBux = issue2(
                {.env = env,
                 .token = "BUX",
                 .issuer = bob,
                 .holders = {ann, cam}});

            env(pay(ann, cam, aBux(35)));
            env(pay(bob, cam, bBux(35)));

            env(offer(bob, aBux(30), bBux(30)));
            env.close();

            // cam puts an offer on the books that her upcoming offer could
            // cross. But this offer should be deleted, not crossed, by her
            // upcoming offer.
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
        };
        testHelper2TokensMix(test);
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

        auto const fee = env.current()->fees().base;
        env.fund(reserve(env, 2) + drops(9999640) + (fee), ann);
        env.fund(reserve(env, 2) + (fee * 4), gw);
        env.close();

        MPT const btc = MPTTester(
            {.env = env, .issuer = gw, .holders = {ann}, .transferFee = 2'000});

        env(pay(gw, ann, btc(2'856)));
        env.close();

        env(offer(ann, drops(365'611'702'030), btc(5'713)));
        env.close();

        // This offer caused the assert.
        env(offer(ann, btc(687), drops(20'000'000'000)),
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

        auto const fee = env.current()->fees().base;
        env.fund(reserve(env, 2) + drops(400'000'000'000) + (fee), alice, bob);
        env.fund(reserve(env, 2) + (fee * 4), gw);
        env.close();

        MPT const cny = MPTTester({.env = env, .issuer = gw, .holders = {bob}});

        env(pay(gw, bob, cny(3'000'000)));
        env.close();

        env(offer(bob, drops(5'400'000'000), cny(2'160'540)));
        env.close();

        // This offer did not round result of partial crossing correctly.
        env(offer(alice, cny(135'620'001), drops(339'000'000'000)));
        env.close();

        auto const aliceOffers = offersOnAccount(env, alice);
        BEAST_EXPECT(aliceOffers.size() == 1);
        for (auto const& offerPtr : aliceOffers)
        {
            auto const& offer = *offerPtr;
            BEAST_EXPECT(offer[sfLedgerEntryType] == ltOFFER);
            BEAST_EXPECT(offer[sfTakerGets] == drops(333'599'446'582));
            BEAST_EXPECT(offer[sfTakerPays] == cny(13'3459'461));
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
        auto const gw = Account("gateway");
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        auto test = [&](auto&& issue1, auto&& issue2) {
            Env env{*this, features};

            auto const fee = env.current()->fees().base;
            env.fund(
                reserve(env, 2) + drops(400'000'000'000) + fee,
                alice,
                bob);
            env.fund(reserve(env, 2) + (fee * 4), gw);
            env.close();

            auto const jpy = issue1(
                {.env = env,
                 .token = "JPY",
                 .issuer = gw,
                 .holders = {alice},
                 .limit = kMaxMpTokenAmount,
                 .transferFee = 2'000});
            auto const btc = issue2(
                {.env = env,
                 .token = "BTC",
                 .issuer = gw,
                 .holders = {bob},
                 .limit = kMaxMpTokenAmount,
                 .transferFee = 2'000});

            env(pay(gw, alice, jpy(3'699'034'802'280'317)));
            env(pay(gw, bob, btc(115'672'255'914'031'100)));
            env.close();

            env(offer(
                bob, jpy(1'241'913'390'770'747), btc(1'969'825'690'469'254)));
            env.close();

            // This offer did not round result of partial crossing correctly.
            env(offer(
                alice, btc(5'507'568'706'427'876), jpy(3'472'696'773'391'072)));
            env.close();

            auto const aliceOffers = offersOnAccount(env, alice);
            BEAST_EXPECT(aliceOffers.size() == 1);
            for (auto const& offerPtr : aliceOffers)
            {
                auto const& offer = *offerPtr;
                BEAST_EXPECT(offer[sfLedgerEntryType] == ltOFFER);
                // This test is similar to corresponding Offer_test, except
                // that JPY is scaled by 10**12 and BTC is scaled by 10**17.
                // There is a difference in the expected results.
                // Offer_test expects values
                //  takerGets:2230.682446713524, takerPays: 0.035378
                // MPT test has the same order of magnitude for the scaled
                // values and the first 5 digits match. Is the difference due to
                // int arithmetics?
                BEAST_EXPECT(offer[sfTakerGets] == jpy(2'230'659'191'281'247));
                BEAST_EXPECT(offer[sfTakerPays] == btc(3'537'743'015'958'622));
            }
        };
        testHelper2TokensMix(test);
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
        //  1. gw issues BTC and USD.  gw charges a 0.2% transfer fee.
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
        auto const gw = Account("gw");

        auto test = [&](auto&& issue1, auto&& issue2) {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base.drops();

            auto const startXrpBalance = XRP(4'000'000);

            env.fund(startXrpBalance, gw);
            env.close();

            auto const btc = issue1(
                {.env = env,
                 .token = "BTC",
                 .issuer = gw,
                 .transferFee = 25'000});
            using tBTC = std::decay_t<decltype(btc)>;
            env.close();
            auto const usd = issue2(
                {.env = env,
                 .token = "USD",
                 .issuer = gw,
                 .transferFee = 25'000});
            using tUSD = std::decay_t<decltype(usd)>;
            env.close();

            // Test cases
            struct Actor
            {
                Account acct;
                int offers{};      // offers on account after crossing
                PrettyAmount xrp;  // final expected after crossing
                PrettyAmount btc;  // final expected after crossing
                PrettyAmount usd;  // final expected after crossing
            };
            struct TestData
            {
                // The first three integers give the *index* in actors
                // to assign each of the three roles.  By using indices it is
                // easy for alice to own the offer in the first leg, the second
                // leg, or both.
                std::size_t self{};
                std::size_t leg0{};
                std::size_t leg1{};
                PrettyAmount btcStart;
                std::vector<Actor> actors;
            };

            // clang-format off
            TestData const tests[]{
                //        btcStart   --------------------- actor[0] ---------------------    -------------------- actor[1] -------------------
                {0, 0, 1, btc(200), {{"ann", 0, drops(3900000'000000 - (4 * baseFee)), btc(200), usd(3000)}, {"abe", 0, drops(4100000'000000 - (3 * baseFee)), btc( 0), usd(750)}}},  // no BTC xfer fee
                {0, 1, 0, btc(200), {{"bev", 0, drops(4100000'000000 - (4 * baseFee)), btc( 75), usd(2000)}, {"bob", 0, drops(3900000'000000 - (3 * baseFee)), btc(100), usd(  0)}}},  // no USD xfer fee
                {0, 0, 0, btc(200), {{"cam", 0, drops(4000000'000000 - (5 * baseFee)), btc(200), usd(2000)}                                                     }},  // no xfer fee
                {0, 1, 0, btc( 50), {{"deb", 1, drops(4040000'000000 - (4 * baseFee)), btc(  0), usd(2000)}, {"dan", 1, drops(3960000'000000 - (3 * baseFee)), btc( 40), usd(  0)}}},  // no USD xfer fee
            };
            // clang-format on

            for (auto const& t : tests)
            {
                Account const& self = t.actors[t.self].acct;
                Account const& leg0 = t.actors[t.leg0].acct;
                Account const& leg1 = t.actors[t.leg1].acct;

                for (auto const& actor : t.actors)
                {
                    env.fund(XRP(4'000'000), actor.acct);
                    env.close();

                    if constexpr (std::is_same_v<tBTC, MPT>)
                    {
                        auto mbtc = MPTTester(env, gw, btc);
                        mbtc.authorize({.account = actor.acct});
                    }
                    else
                    {
                        env(trust(actor.acct, btc(400)));
                        env.close();
                    }
                    if constexpr (std::is_same_v<tUSD, MPT>)
                    {
                        auto musd = MPTTester(env, gw, usd);
                        musd.authorize({.account = actor.acct});
                    }
                    else
                    {
                        env(trust(actor.acct, usd(8000)));
                        env.close();
                    }
                    env.close();
                }

                env(pay(gw, self, t.btcStart));
                env(pay(gw, self, usd(2'000)));
                if (self.id() != leg1.id())
                    env(pay(gw, leg1, usd(2'000)));
                env.close();

                // Get the initial offers in place.  Remember their sequences
                // so we can delete them later.
                env(offer(leg0, btc(100), XRP(100'000), tfPassive));
                env.close();
                std::uint32_t const leg0OfferSeq = env.seq(leg0) - 1;

                env(offer(leg1, XRP(100'000), usd(1'000), tfPassive));
                env.close();
                std::uint32_t const leg1OfferSeq = env.seq(leg1) - 1;

                // This is the offer that matters.
                env(offer(self, usd(1'000), btc(100)));
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
                        std::remove_if(
                            actorOffers.begin(), actorOffers.end(), [](SLE::const_pointer& offer) {
                                return (*offer)[sfTakerGets].signum() == 0;
                            }));
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
        };
        testHelper2TokensMix(test);
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
        auto const gw = Account("gw");

        auto test = [&](auto&& issue1, auto&& issue2) {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base.drops();

            auto const startXrpBalance = XRP(4'000'000);

            env.fund(startXrpBalance, gw);
            env.close();

            auto const btc = issue1(
                {.env = env, .token = "BTC", .issuer = gw, .limit = 40, .transferFee = 25'000});
            using tBTC = std::decay_t<decltype(btc)>;
            auto const usd = issue2(
                {.env = env, .token = "USD", .issuer = gw, .limit = 8'000, .transferFee = 25'000});
            using tUSD = std::decay_t<decltype(usd)>;
            env.close();

            // Test cases
            struct Actor
            {
                Account acct;
                int offers{};      // offers on account after crossing
                PrettyAmount xrp;  // final expected after crossing
                PrettyAmount btc;  // final expected after crossing
                PrettyAmount usd;  // final expected after crossing
            };
            struct TestData
            {
                // The first three integers give the *index* in actors
                // to assign each of the three roles.  By using indices it is
                // easy for alice to own the offer in the first leg, the second
                // leg, or both.
                std::size_t self{};
                std::size_t leg0{};
                std::size_t leg1{};
                PrettyAmount btcStart;
                std::vector<Actor> actors;
            };

            // clang-format off
            TestData const flowTests[]{
                //         btcStart    ------------------- actor[0] --------------------    ------------------- actor[1] --------------------
                {0, 0, 1, btc(5), {{"gay", 1, drops(3950000'000000 - (4 * baseFee)), btc(5), usd (2500)}, {"gar", 1, drops(4050000'000000 - (3 * baseFee)), btc(0), usd(1375)}}}, // no BTC xfer fee
                {0, 0, 0, btc(5), {{"hye", 2, drops(4000000'000000 - (5 * baseFee)), btc(5), usd (2000)}                                                     }}  // no xfer fee
            };
            // clang-format on

            for (auto const& t : flowTests)
            {
                Account const& self = t.actors[t.self].acct;
                Account const& leg0 = t.actors[t.leg0].acct;
                Account const& leg1 = t.actors[t.leg1].acct;

                for (auto const& actor : t.actors)
                {
                    env.fund(XRP(4'000'000), actor.acct);
                    env.close();

                    if constexpr (std::is_same_v<tBTC, MPT>)
                    {
                        auto mbtc = MPTTester(env, gw, btc);
                        mbtc.authorize({.account = actor.acct});
                    }
                    else
                    {
                        env(trust(actor.acct, btc(40)));
                        env.close();
                    }
                    if constexpr (std::is_same_v<tUSD, MPT>)
                    {
                        auto musd = MPTTester(env, gw, usd);
                        musd.authorize({.account = actor.acct});
                    }
                    else
                    {
                        env(trust(actor.acct, usd(8'000)));
                        env.close();
                    }
                }

                env(pay(gw, self, t.btcStart));
                env(pay(gw, self, usd(2'000)));
                if (self.id() != leg1.id())
                    env(pay(gw, leg1, usd(2'000)));
                env.close();

                // Get the initial offers in place.  Remember their sequences
                // so we can delete them later.
                env(offer(leg0, btc(10), XRP(100'000), tfPassive));
                env.close();
                std::uint32_t const leg0OfferSeq = env.seq(leg0) - 1;

                env(offer(leg1, XRP(100'000), usd(1'000), tfPassive));
                env.close();
                std::uint32_t const leg1OfferSeq = env.seq(leg1) - 1;

                // This is the offer that matters.
                env(offer(self, usd(1'000), btc(10)));
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
                        std::remove_if(
                            actorOffers.begin(), actorOffers.end(), [](SLE::const_pointer& offer) {
                                return (*offer)[sfTakerGets].signum() == 0;
                            }));
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
        };
        testHelper2TokensMix(test);
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

        env.fund(XRP(400'000), gw, alice, bob);
        env.close();

        // GW requires authorization for holders of its IOUs
        auto gwMUSD =
            MPTTester({.env = env, .issuer = gw, .flags = kMptDexFlags | tfMPTRequireAuth});
        MPT const gwUSD = gwMUSD;

        // Have gw authorize bob and alice
        gwMUSD.authorize({.account = alice});
        gwMUSD.authorize({.account = gw, .holder = alice});
        gwMUSD.authorize({.account = bob});
        gwMUSD.authorize({.account = gw, .holder = bob});
        // Alice is able to place the offer since the GW has authorized her
        env(offer(alice, gwUSD(40), XRP(4'000)));
        env.close();

        env.require(offers(alice, 1));
        env.require(Balance(alice, gwUSD(0)));

        env(pay(gw, bob, gwUSD(50)));
        env.close();

        env.require(Balance(bob, gwUSD(50)));

        // Bob's offer should cross Alice's
        env(offer(bob, XRP(4'000), gwUSD(40)));
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
        // 1. gw creates MPTokenIssuance, which requires authorization.
        //    alice creates an offer to acquire USD/gw, an asset for which
        //    she does not own MPToken. This offer fails since alice
        //    doesn't own MPToken and authorization is required.
        //
        // 2. Next, alice creates MPT, but it's not authorized.
        //    alice attempts to create an offer and again fails.
        //
        // 3. Finally, gw authorizes alice to own USD/gw.
        //    At this point alice successfully
        //    creates and crosses an offer for USD/gw.

        using namespace jtx;

        Env env{*this, features};

        auto const gw = Account("gw");
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(400'000), gw, alice, bob);
        env.close();

        auto gwMUSD =
            MPTTester({.env = env, .issuer = gw, .flags = kMptDexFlags | tfMPTRequireAuth});
        MPT const gwUSD = gwMUSD;

        // alice can't create an offer because alice doesn't own
        // MPToken and MPTokenIssuance requires authorization
        env(offer(alice, gwUSD(40), XRP(4'000)), Ter(tecNO_AUTH));
        env.close();

        env.require(offers(alice, 0));
        env.require(Balance(alice, gwUSD(kNone)));

        gwMUSD.authorize({.account = bob});
        gwMUSD.authorize({.account = gw, .holder = bob});

        env(pay(gw, bob, gwUSD(50)));
        env.close();
        env.require(Balance(bob, gwUSD(50)));

        // bob can create an offer since bob owns MPToken
        // and it is authorized.
        env(offer(bob, XRP(4'000), gwUSD(40)));
        env.close();
        std::uint32_t const bobOfferSeq = env.seq(bob) - 1;

        env.require(offers(alice, 0));

        // alice creates MPToken, which is still not authorized.  alice
        // should still not be able to create an offer for USD/gw.
        gwMUSD.authorize({.account = alice});

        env(offer(alice, gwUSD(40), XRP(4'000)), Ter(tecNO_AUTH));
        env.close();

        env.require(offers(alice, 0));
        env.require(Balance(alice, gwUSD(0)));

        env.require(offers(bob, 1));
        env.require(Balance(bob, gwUSD(50)));

        // Delete bob's offer so alice can create an offer without crossing.
        env(offerCancel(bob, bobOfferSeq));
        env.close();
        env.require(offers(bob, 0));

        // Finally, gw authorizes alice.  Now alice's
        // offer should succeed.
        gwMUSD.authorize({.account = gw, .holder = alice});

        env(offer(alice, gwUSD(40), XRP(4'000)));
        env.close();

        env.require(offers(alice, 1));

        // Now bob creates his offer again.  alice's offer should cross.
        env(offer(bob, XRP(4'000), gwUSD(40)));
        env.close();

        env.require(offers(alice, 0));
        env.require(Balance(alice, gwUSD(40)));

        env.require(offers(bob, 0));
        env.require(Balance(bob, gwUSD(10)));
    }

    void
    testSelfAuth(FeatureBitset features)
    {
        testcase("Self Auth");

        using namespace jtx;

        Env env{*this, features};

        auto const gw = Account("gw");
        auto const alice = Account("alice");

        env.fund(XRP(400'000), gw, alice);
        env.close();

        auto gwMUSD =
            MPTTester({.env = env, .issuer = gw, .flags = kMptDexFlags | tfMPTRequireAuth});
        MPT const gwUSD = gwMUSD;

        // Test that gw can create an offer to buy gw's currency.
        env(offer(gw, gwUSD(40), XRP(4'000)));
        env.close();
        std::uint32_t const gwOfferSeq = env.seq(gw) - 1;
        env.require(offers(gw, 1));

        // Cancel gw's offer
        env(offerCancel(gw, gwOfferSeq));
        env.close();
        env.require(offers(gw, 0));

        // Before DepositPreauth an account with lsfRequireAuth set could not
        // create an offer to buy their own currency.  After DepositPreauth
        // they can.
        env(offer(gw, gwUSD(40), XRP(4'000)));
        env.close();

        env.require(offers(gw, 1));

        // The rest of the test verifies DepositPreauth behavior.

        // Create/authorize alice's MPToken
        gwMUSD.authorize({.account = alice});
        gwMUSD.authorize({.account = gw, .holder = alice});

        env(pay(gw, alice, gwUSD(50)));
        env.close();

        env.require(Balance(alice, gwUSD(50)));

        // alice's offer should cross gw's
        env(offer(alice, XRP(4'000), gwUSD(40)));
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

        auto mpTokenExists =
            [](jtx::Env const& env, AccountID const& account, MPTID const& issuanceID) -> bool {
            return bool(env.le(keylet::mptoken(issuanceID, account)));
        };

        Account const alice("alice");
        Account const becky("becky");
        Account const carol("carol");
        Account const gw("gateway");

        Env env{*this, features};

        env.fund(XRP(10'000), alice, becky, carol, noripple(gw));

        auto musd = MPTTester({.env = env, .issuer = gw});
        MPT const usd = musd;

        musd.authorize({.account = becky});
        BEAST_EXPECT(mpTokenExists(env, becky, usd.issuanceID));
        env(pay(gw, becky, usd(5)));
        env.close();

        auto mbux = MPTTester({.env = env, .issuer = alice});
        MPT const bux = mbux;

        // Make offers that produce USD and can be crossed two ways:
        // direct XRP -> USD
        // direct BUX -> USD
        env(offer(becky, XRP(2), usd(2)), Txflags(tfPassive));
        std::uint32_t const beckyBuxUsdSeq{env.seq(becky)};
        env(offer(becky, bux(3), usd(3)), Txflags(tfPassive));
        env.close();

        // becky keeps the offers, but removes MPT.
        env(pay(becky, gw, usd(5)));
        musd.authorize({.account = becky, .flags = tfMPTUnauthorize});

        BEAST_EXPECT(!mpTokenExists(env, becky, usd.issuanceID));
        BEAST_EXPECT(isOffer(env, becky, XRP(2), usd(2)));
        BEAST_EXPECT(isOffer(env, becky, bux(3), usd(3)));

        // Have to delete MPTokenIssuance in order to delete
        // the issuer account.
        musd.destroy({});

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
        mbux.authorize({.account = carol});
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

    // Helper function that returns offers on an account sorted by sequence.
    static std::vector<SLE::const_pointer>
    sortedOffersOnAccount(jtx::Env& env, jtx::Account const& acct)
    {
        std::vector<SLE::const_pointer> offers{offersOnAccount(env, acct)};
        std::ranges::sort(offers, [](SLE::const_ref rhs, SLE::const_ref lhs) {
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

        env.fund(XRP(10'000), gw, alice, bob);
        env.close();

        MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}});

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

        env.fund(XRP(10'000), gw, alice);
        env.close();

        MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice}});

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
    testFillOrKill(FeatureBitset features)
    {
        testcase("fixFillOrKill");
        using namespace jtx;
        Account const issuer("issuer");
        Account const maker("maker");
        Account const taker("taker");

        auto test = [&](auto&& issue1, auto&& issue2) {
            Env env(*this, features);

            env.fund(XRP(1'000), issuer);
            env.fund(XRP(1'000), maker, taker);
            env.close();

            auto const usd =
                issue1({.env = env, .token = "USD", .issuer = issuer, .holders = {maker, taker}});
            auto const eur =
                issue2({.env = env, .token = "EUR", .issuer = issuer, .holders = {maker, taker}});

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
                if (err == tesSUCCESS)
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
                if (err == tesSUCCESS)
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
                if (err == tesSUCCESS)
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
        };
        testHelper2TokensMix(test);
    }

    void
    testTickSize(FeatureBitset features)
    {
        testcase("Tick Size");

        using namespace jtx;

        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};

        auto getIOU = [&](Env& env) -> PrettyAsset {
            static int kI = 0;
            std::string const name = "IO" + std::to_string(kI++);
            auto const iou = gw[name];
            env(trust(alice, iou(1'000)));
            env(pay(gw, alice, iou(100)));
            env.close();
            return iou;
        };
        auto getMPT = [&](Env& env) -> PrettyAsset {
            MPT const mpt =
                MPTTester({.env = env, .issuer = gw, .holders = {alice}, .pay = 1'000'000'000});
            return mpt;
        };
        auto getXRP = [&](Env& env) -> PrettyAsset { return XRP; };

        using ToAsset = std::function<PrettyAsset(Env&)>;
        struct TestInfo
        {
            ToAsset toAsset1;
            ToAsset toAsset2;
            int val1;
            int val2;
        };
        // XRP/MPT, MPT/XRP, MPT/MPT offers are not adjusted for TickSize
        // IOU/IOU, XRP/IOU, IOU/XRP offers have TickSize logic unchanged
        // IOU/MPT, MPT/IOU have TickSize logic applied to adjust IOU only
        std::vector<TestInfo> const tests = {
            {.toAsset1 = getIOU, .toAsset2 = getIOU, .val1 = 10, .val2 = 30},
            {.toAsset1 = getIOU, .toAsset2 = getXRP, .val1 = 10, .val2 = 30'000'000},
            {.toAsset1 = getXRP, .toAsset2 = getIOU, .val1 = 10'000'000, .val2 = 30},
            {.toAsset1 = getMPT, .toAsset2 = getXRP, .val1 = 10'000'000, .val2 = 30'000'000},
            {.toAsset1 = getXRP, .toAsset2 = getMPT, .val1 = 10'000'000, .val2 = 30'000'000},
            {.toAsset1 = getIOU, .toAsset2 = getMPT, .val1 = 10, .val2 = 30'000'000},
            {.toAsset1 = getMPT, .toAsset2 = getIOU, .val1 = 10'000'000, .val2 = 30},
            {.toAsset1 = getMPT, .toAsset2 = getMPT, .val1 = 10'000'000, .val2 = 30'000'000}};
        for (TestInfo const& t : tests)
        {
            Env env{*this, features};
            env.fund(XRP(10'000), gw, alice);
            env.close();

            auto const xts = t.toAsset1(env);
            auto const xxx = t.toAsset2(env);

            auto tokenType = [](PrettyAsset const& asset) -> std::string {
                return asset.raw().visit(
                    [&](Issue const& issue) { return issue.native() ? "XRPIssue" : "Issue"; },
                    [&](MPTIssue const&) { return "MPTIssue"; });
            };

            testcase << "offer: " << tokenType(xts) << "/" << tokenType(xxx);

            {
                // Gateway sets its tick size to 5
                auto txn = noop(gw);
                txn[sfTickSize.fieldName] = 5;
                env(txn);
                BEAST_EXPECT((*env.le(gw))[sfTickSize] == 5);
            }

            env(offer(alice, xts(t.val1), xxx(t.val2)));
            env(offer(alice, xts(t.val2), xxx(t.val1)));
            env(offer(alice, xts(t.val1), xxx(t.val2)), Json(jss::Flags, tfSell));
            env(offer(alice, xts(t.val2), xxx(t.val1)), Json(jss::Flags, tfSell));

            std::map<std::uint32_t, std::pair<STAmount, STAmount>> offers;
            forEachItem(*env.current(), alice, [&](SLE::const_ref sle) {
                if (sle->getType() == ltOFFER)
                {
                    offers.emplace(
                        (*sle)[sfSequence],
                        std::make_pair((*sle)[sfTakerPays], (*sle)[sfTakerGets]));
                }
            });

            // first offer
            auto it = offers.begin();
            BEAST_EXPECT(it != offers.end());
            if (xxx.native() && !xts.holds<MPTIssue>())
            {
                BEAST_EXPECT(
                    it->second.first == xts(t.val1) && it->second.second == XRPAmount(29'999'400));
            }
            else if (!xxx.integral())
            {
                BEAST_EXPECT(
                    it->second.first == xts(t.val1) && it->second.second < xxx(t.val2) &&
                    it->second.second > STAmount(xxx, 29'9994, -4));
            }
            else
            {
                BEAST_EXPECT(it->second.first == xts(t.val1) && it->second.second == xxx(t.val2));
            }

            // second offer
            ++it;
            BEAST_EXPECT(it != offers.end());
            BEAST_EXPECT(it->second.first == xts(t.val2) && it->second.second == xxx(t.val1));

            // third offer
            ++it;
            BEAST_EXPECT(it != offers.end());
            if (xts.native() && !xxx.holds<MPTIssue>())
            {
                BEAST_EXPECT(
                    it->second.first == XRPAmount(10'000'200) && it->second.second == xxx(t.val2));
            }
            else if (!xts.integral())
            {
                BEAST_EXPECT(
                    it->second.first == STAmount(xts, 10'0002, -4) &&
                    it->second.second == xxx(t.val2));
            }
            else
            {
                BEAST_EXPECT(it->second.first == xts(t.val1) && it->second.second == xxx(t.val2));
            }

            // fourth offer
            // exact TakerPays is XTS(1/.033333)
            ++it;
            BEAST_EXPECT(it != offers.end());
            BEAST_EXPECT(it->second.first == xts(t.val2) && it->second.second == xxx(t.val1));

            BEAST_EXPECT(++it == offers.end());
        }
    }

    void
    testAutoCreateReserve(FeatureBitset features)
    {
        // When an offer on the book is partially crossed, the payment engine
        // auto-creates a new ledger object (MPToken or IOU trustline) for the
        // offer owner to hold the incoming asset.  This happens inside
        // BookStep::forEachOffer (MPT: checkCreateMPT) and BookStep::consumeOffer
        // (IOU: directSendNoFeeIOU -> trustCreate) without a reserve sufficiency
        // check.  The offer owner can therefore end up with more objects than
        // their XRP balance can reserve for, consistent with IOU behavior.

        testcase("Auto-Create Object Without Reserve Check During Partial Crossing");

        using namespace jtx;

        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const carol = Account{"carol"};
        auto const bob = Account{"bob"};

        auto test = [&](auto&& getToken, auto&& execTx) {
            // MPT/IOU: carol's existing offer buys MPT/IOU by selling XRP.
            // carol has no MPToken/Trustline for this issuance.  When alice partially crosses
            // carol's offer, an MPToken/Trustline is auto-created for carol without checking
            // that she can afford the extra reserve slot.
            Env env{*this, features};

            auto const f = env.current()->fees().base;
            auto const r = reserve(env, 0);
            auto const inc = reserve(env, 1) - r;

            env.fund(XRP(10'000), gw, alice, bob);

            // getToken:
            //  - Create MPT with CanTransfer + CanTrade; authorize alice as holder.
            //  - Create IOU trustline
            auto const token = getToken(env);

            // carol: reserve(0) + 1 increment + fee covers placing one offer.
            // After the offer tx she has exactly reserve(1) + XRP(30).
            // XRP(30) < inc (50 XRP), so receiving a second object will put her
            // below reserve(2).
            if (BEAST_EXPECT(inc > XRP(30)))
                env.fund(r + inc + f + XRP(30), carol);

            // carol's offer goes on the book (no counterpart yet).
            // TakerPays=Token(30): carol will receive Token when crossed.
            // TakerGets=XRP(30):  carol will give XRP when crossed.
            env(offer(carol, token(30), XRP(30)));
            env.require(Owners(carol, 1));

            // Execute offer create or cross-currency payment
            // alice partially crosses carol's offer.
            // alice sends Token(15) to carol and receives XRP(15).
            // Token:
            // - MPT: checkCreateMPT auto-creates an MPToken for carol (no reserve check).
            // - IOU: directSendNoFeeIOU auto-creates an Trustline for carol (no reserve check).
            execTx(env, token);

            // Carol now owns 2 objects (remaining offer + new MPToken) even
            // though her XRP balance is only reserve(1) + XRP(15), which is
            // below reserve(2) = reserve(1) + inc.
            auto const carolBalance = r + inc + XRP(15);
            env.require(Owners(carol, 2), Balance(carol, token(15)), Balance(carol, carolBalance));
            BEAST_EXPECT(carolBalance < r + 2 * inc);  // below reserve(2)
        };
        std::function<PrettyAsset(Env&)> const getIOU = [&](Env& env) -> PrettyAsset {
            env.trust(gw["USD"](1'000), alice);
            env(pay(gw, alice, gw["USD"](100)));
            return gw["USD"];
        };
        std::function<PrettyAsset(Env&)> const getMPT = [&](Env& env) -> PrettyAsset {
            MPT const mpT1 = MPTTester({.env = env, .issuer = gw, .holders = {alice}, .pay = 100});
            return mpT1;
        };
        for (auto&& getToken : {getIOU, getMPT})
        {
            test(getToken, [&](Env& env, PrettyAsset const& token) {
                // alice partially crosses carol's offer.
                // alice sends Token(15) to carol and receives XRP(15).
                // Token is MPT: checkCreateMPT auto-creates an MPToken for carol (no reserve
                // check). Token is IOU: directSendNoFeeIOU auto-creates a trustline for carol (no
                // reserve check).
                env(offer(alice, XRP(15), token(15)));
            });
            test(getToken, [&](Env& env, PrettyAsset const& token) {
                // Similar to above but with cross-currency payment.
                env(pay(alice, bob, XRP(15)),
                    Sendmax(token(15)),
                    Path(~XRP),
                    Txflags(tfNoRippleDirect | tfPartialPayment));
            });
        }
    }

    void
    testAll(FeatureBitset features)
    {
        testCanceledOffer(features);
        testRmFundedOffer(features);
        testTinyPayment(features);
        testXRPTinyPayment(features);
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
        testDirectToDirectPath(features);
        testSelfCrossLowQualityOffer(features);
        testOfferInScaling(features);
        testOfferInScalingWithXferRate(features);
        testSelfPayXferFeeOffer(features);
        testSelfPayUnlimitedFunds(features);
        testRequireAuth(features);
        testMissingAuth(features);
        testSelfAuth(features);
        testDeletedOfferIssuer(features);
        testTicketOffer(features);
        testTicketCancelOffer(features);
        testRmSmallIncreasedQOffersXRP(features);
        testRmSmallIncreasedQOffersMPT(features);
        testFillOrKill(features);
        testTickSize(features);
        testAutoCreateReserve(features);
    }

    void
    run() override
    {
        using namespace jtx;
        static FeatureBitset const kAll{testableAmendments()};
        testAll(kAll);
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(OfferMPT, tx, xrpl, 2);

}  // namespace xrpl::test
