//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <test/jtx.h>
#include <test/jtx/PathSet.h>

#include <xrpld/app/paths/Flow.h>
#include <xrpld/app/paths/detail/Steps.h>
#include <xrpld/core/Config.h>
#include <xrpld/ledger/ApplyViewImpl.h>
#include <xrpld/ledger/PaymentSandbox.h>
#include <xrpld/ledger/Sandbox.h>

#include <xrpl/basics/contract.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {

#if 0
bool
getNoRippleFlag(
    jtx::Env const& env,
    jtx::Account const& src,
    jtx::Account const& dst,
    Currency const& cur)
{
    if (auto sle = env.le(keylet::line(src, dst, cur)))
    {
        auto const flag =
            (src.id() > dst.id()) ? lsfHighNoRipple : lsfLowNoRipple;
        return sle->isFlag(flag);
    }
    Throw<std::runtime_error>("No line in getTrustFlag");
    return false;  // silence warning
}
#endif

struct FlowMPT_test : public beast::unit_test::suite
{
    void
    testDirectStep(FeatureBitset features)
    {
        testcase("Direct Step");

        using namespace jtx;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        // auto const dan = Account("dan");
        // auto const erin = Account("erin");
        auto const gw = Account("gw");
        {
            // Pay USD, trivial path
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, gw);
            MPT const USD =
                MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}});
            env(pay(gw, alice, USD(100)));
            env(pay(alice, bob, USD(10)), paths(USD));
            env.require(balance(bob, USD(10)));
        }
        {
            // Partial payments
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, gw);
            MPT const USD =
                MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}});
            env(pay(gw, alice, USD(100)));
            env(pay(alice, bob, USD(110)), paths(USD), ter(tecPATH_PARTIAL));
            env.require(balance(bob, USD(0)));
            env(pay(alice, bob, USD(110)),
                paths(USD),
                txflags(tfPartialPayment));
            env.require(balance(bob, USD(100)));
        }

        {
            // Limit quality
            Env env(*this, features);

            env.fund(XRP(10'000), gw, alice, bob, carol);
            MPT const USD = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice, carol}});
            MPT const EUR =
                MPTTester({.env = env, .issuer = gw, .holders = {bob}});
            env(pay(gw, alice, USD(100)));
            env(pay(gw, bob, EUR(100)));

            env(offer(alice, EUR(4), USD(4)));
            env.close();

            env(pay(bob, carol, USD(5)),
                sendmax(EUR(4)),
                txflags(tfLimitQuality | tfPartialPayment),
                ter(tecPATH_DRY));
            env.require(balance(carol, USD(0)));

            env(pay(bob, carol, USD(5)),
                sendmax(EUR(4)),
                txflags(tfPartialPayment));
            env.require(balance(carol, USD(4)));
        }
    }

    void
    testBookStep(FeatureBitset features)
    {
        testcase("Book Step");

        using namespace jtx;

        auto const gw = Account("gateway");
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");

        {
            // simple MPT/MPT offer
            Env env(*this, features);

            env.fund(XRP(10'000), alice, bob, carol, gw);

            MPT const USD = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice, bob, carol}});
            MPT const BTC = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice, bob, carol}});

            env(pay(gw, alice, BTC(50)));
            env(pay(gw, bob, USD(50)));

            env(offer(bob, BTC(50), USD(50)));

            env(pay(alice, carol, USD(50)), path(~USD), sendmax(BTC(50)));

            env.require(balance(alice, BTC(0)));
            env.require(balance(bob, BTC(50)));
            env.require(balance(bob, USD(0)));
            env.require(balance(carol, USD(50)));
            BEAST_EXPECT(!isOffer(env, bob, BTC(50), USD(50)));
        }
        {
            // simple MPT/XRP XRP/MPT offer
            Env env(*this, features);

            env.fund(XRP(10'000), alice, bob, carol, gw);

            MPT const USD = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice, bob, carol}});
            MPT const BTC = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice, bob, carol}});

            env(pay(gw, alice, BTC(50)));
            env(pay(gw, bob, USD(50)));

            env(offer(bob, BTC(50), XRP(50)));
            env(offer(bob, XRP(50), USD(50)));

            env(pay(alice, carol, USD(50)), path(~XRP, ~USD), sendmax(BTC(50)));

            env.require(balance(alice, BTC(0)));
            env.require(balance(bob, BTC(50)));
            env.require(balance(bob, USD(0)));
            env.require(balance(carol, USD(50)));
            BEAST_EXPECT(!isOffer(env, bob, XRP(50), USD(50)));
            BEAST_EXPECT(!isOffer(env, bob, BTC(50), XRP(50)));
        }
        {
            // simple XRP -> USD through offer and sendmax
            Env env(*this, features);

            env.fund(XRP(10'000), alice, bob, carol, gw);

            MPT const USD = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice, bob, carol}});
            MPT const BTC = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice, bob, carol}});

            env(pay(gw, bob, USD(50)));

            env(offer(bob, XRP(50), USD(50)));

            env(pay(alice, carol, USD(50)), path(~USD), sendmax(XRP(50)));

            // fee: MPTokenAuthorize * 2(EUR, USD) + pay
            env.require(balance(alice, XRP(10'000 - 50) - txfee(env, 3)));
            // fee: MPTokenAuthorize * 2(EUR, USD) + offer
            env.require(balance(bob, XRP(10'000 + 50) - txfee(env, 3)));
            env.require(balance(bob, USD(0)));
            env.require(balance(carol, USD(50)));
            BEAST_EXPECT(!isOffer(env, bob, XRP(50), USD(50)));
        }
        {
            // simple USD -> XRP through offer and sendmax
            Env env(*this, features);

            env.fund(XRP(10'000), alice, bob, carol, gw);

            MPT const USD = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice, bob, carol}});
            MPT const BTC = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice, bob, carol}});

            env(pay(gw, alice, USD(50)));

            env(offer(bob, USD(50), XRP(50)));

            env(pay(alice, carol, XRP(50)), path(~XRP), sendmax(USD(50)));

            env.require(balance(alice, USD(0)));
            env.require(balance(bob, XRP(10'000 - 50) - txfee(env, 3)));
            env.require(balance(bob, USD(50)));
            env.require(balance(carol, XRP(10'000 + 50) - txfee(env, 2)));
            BEAST_EXPECT(!isOffer(env, bob, USD(50), XRP(50)));
        }
        {
            // test unfunded offers are removed when payment succeeds
            Env env(*this, features);

            env.fund(XRP(10'000), alice, bob, carol, gw);

            MPT const USD = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice, bob, carol}});
            MPT const BTC = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice, bob, carol}});
            MPT const EUR = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice, bob, carol}});

            env(pay(gw, alice, BTC(60)));
            env(pay(gw, bob, USD(50)));
            env(pay(gw, bob, EUR(50)));

            env(offer(bob, BTC(50), USD(50)));
            env(offer(bob, BTC(40), EUR(50)));
            env(offer(bob, EUR(50), USD(50)));

            // unfund offer
            env(pay(bob, gw, EUR(50)));
            BEAST_EXPECT(isOffer(env, bob, BTC(50), USD(50)));
            BEAST_EXPECT(isOffer(env, bob, BTC(40), EUR(50)));
            BEAST_EXPECT(isOffer(env, bob, EUR(50), USD(50)));

            env(pay(alice, carol, USD(50)),
                path(~USD),
                path(~EUR, ~USD),
                sendmax(BTC(60)));

            env.require(balance(alice, BTC(10)));
            env.require(balance(bob, BTC(50)));
            env.require(balance(bob, USD(0)));
            env.require(balance(bob, EUR(0)));
            env.require(balance(carol, USD(50)));
            // used in the payment
            BEAST_EXPECT(!isOffer(env, bob, BTC(50), USD(50)));
            // found unfunded
            BEAST_EXPECT(!isOffer(env, bob, BTC(40), EUR(50)));
            // unfunded, but should not yet be found unfunded
            BEAST_EXPECT(isOffer(env, bob, EUR(50), USD(50)));
        }
        {
            // test unfunded offers are returned when the payment fails.
            // bob makes two offers: a funded 5000 USD for 50 BTC and an
            // unfunded 5000 EUR for 60 BTC. alice pays carol 6100 USD with 61
            // BTC. alice only has 60 BTC, so the payment will fail. The payment
            // uses two paths: one through bob's funded offer and one through
            // his unfunded offer. When the payment fails `flow` should return
            // the unfunded offer. This test is intentionally similar to the one
            // that removes unfunded offers when the payment succeeds.
            Env env(*this, features);

            env.fund(XRP(10'000), alice, bob, carol, gw);

            MPT const USD = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice, bob, carol}});
            MPT const BTC = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice, bob, carol}});
            MPT const EUR = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice, bob, carol}});

            env(pay(gw, alice, BTC(60)));
            env(pay(gw, bob, USD(6'000)));
            env(pay(gw, bob, EUR(5'000)));
            env(pay(gw, carol, EUR(100)));

            env(offer(bob, BTC(50), USD(5'000)));
            env(offer(bob, BTC(60), EUR(5'000)));
            env(offer(carol, BTC(1'000), EUR(100)));
            env(offer(bob, EUR(5'000), USD(5'000)));

            // unfund offer
            env(pay(bob, gw, EUR(5'000)));
            BEAST_EXPECT(isOffer(env, bob, BTC(50), USD(5'000)));
            BEAST_EXPECT(isOffer(env, bob, BTC(60), EUR(5'000)));
            BEAST_EXPECT(isOffer(env, carol, BTC(1'000), EUR(100)));

            auto flowJournal = env.app().logs().journal("Flow");
            auto const flowResult = [&] {
                STAmount deliver(USD(5'100));
                STAmount smax(BTC(61));
                PaymentSandbox sb(env.current().get(), tapNONE);
                STPathSet paths;
                auto IPE = [](Asset const& asset) {
                    return STPathElement(
                        STPathElement::typeAsset | STPathElement::typeIssuer,
                        xrpAccount(),
                        asset,
                        asset.getIssuer());
                };
                {
                    // BTC -> USD
                    STPath p1({IPE(USD)});
                    paths.push_back(p1);
                    // BTC -> EUR -> USD
                    STPath p2({IPE(EUR), IPE(USD)});
                    paths.push_back(p2);
                }

                return flow(
                    sb,
                    deliver,
                    alice,
                    carol,
                    paths,
                    false,
                    false,
                    true,
                    OfferCrossing::no,
                    std::nullopt,
                    smax,
                    flowJournal);
            }();

            BEAST_EXPECT(flowResult.removableOffers.size() == 1);
            env.app().openLedger().modify(
                [&](OpenView& view, beast::Journal j) {
                    if (flowResult.removableOffers.empty())
                        return false;
                    Sandbox sb(&view, tapNONE);
                    for (auto const& o : flowResult.removableOffers)
                        if (auto ok = sb.peek(keylet::offer(o)))
                            offerDelete(sb, ok, flowJournal);
                    sb.apply(view);
                    return true;
                });

            // used in payment, but since payment failed should be untouched
            BEAST_EXPECT(isOffer(env, bob, BTC(50), USD(5'000)));
            BEAST_EXPECT(isOffer(env, carol, BTC(1'000), EUR(100)));
            // found unfunded
            BEAST_EXPECT(!isOffer(env, bob, BTC(60), EUR(5'000)));
        }
        {
            // Do not produce more in the forward pass than the reverse pass
            // This test uses a path that whose reverse pass will compute a
            // 0.5 USD input required for a 1 EUR output. It sets a sendmax of
            // 0.4 USD, so the payment engine will need to do a forward pass.
            // Without limits, the 0.4 USD would produce 1000 EUR in the forward
            // pass. This test checks that the payment produces 1 EUR, as
            // expected.
            Env env(*this, features);
            env.fund(XRP(10'000), alice, bob, carol, gw);

            MPT const USD = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice, bob, carol}});
            MPT const EUR = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice, bob, carol}});

            env(pay(gw, alice, USD(1'000)));
            env(pay(gw, bob, EUR(1'000)));

            Keylet const bobUsdOffer = keylet::offer(bob, env.seq(bob));
            env(offer(bob, USD(10), drops(2)), txflags(tfPassive));
            env(offer(bob, drops(1), EUR(1'000)), txflags(tfPassive));

            bool const reducedOffersV2 = features[fixReducedOffersV2];

            // With reducedOffersV2, it is not allowed to accept less than
            // USD(0.5) of bob's USD offer.  If we provide 1 drop for less
            // than USD(0.5), then the remaining fractional offer would
            // block the order book.
            TER const expectedTER =
                reducedOffersV2 ? TER(tecPATH_DRY) : TER(tesSUCCESS);
            env(pay(alice, carol, EUR(1)),
                path(~XRP, ~EUR),
                sendmax(USD(4)),
                txflags(tfNoRippleDirect | tfPartialPayment),
                ter(expectedTER));

            if (!reducedOffersV2)
            {
                env.require(balance(carol, EUR(1)));
                env.require(balance(bob, USD(4)));
                env.require(balance(bob, EUR(999)));

                // Show that bob's USD offer is now a blocker.
                std::shared_ptr<SLE const> const usdOffer = env.le(bobUsdOffer);
                if (BEAST_EXPECT(usdOffer))
                {
                    std::uint64_t const bookRate = [&usdOffer]() {
                        // Extract the least significant 64 bits from the
                        // book page.  That's where the quality is stored.
                        std::string bookDirStr =
                            to_string(usdOffer->at(sfBookDirectory));
                        bookDirStr.erase(0, 48);
                        return std::stoull(bookDirStr, nullptr, 16);
                    }();
                    std::uint64_t const actualRate = getRate(
                        usdOffer->at(sfTakerGets), usdOffer->at(sfTakerPays));

                    // We expect the actual rate of the offer to be worse
                    // (larger) than the rate of the book page holding the
                    // offer.  This is a defect which is corrected by
                    // fixReducedOffersV2.
                    BEAST_EXPECT(actualRate > bookRate);
                }
            }
        }
    }

    void
    testTransferRate(FeatureBitset features)
    {
        testcase("Transfer Rate");

        using namespace jtx;

        auto const gw = Account("gateway");
        auto const USD = gw["USD"];
        auto const BTC = gw["BTC"];
        auto const EUR = gw["EUR"];
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");

        {
            // Simple payment through a gateway with a
            // transfer rate
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, gw);
            env(rate(gw, 1.25));
            env.trust(USD(1000), alice, bob, carol);
            env(pay(gw, alice, USD(50)));
            env.require(balance(alice, USD(50)));
            env(pay(alice, bob, USD(40)), sendmax(USD(50)));
            env.require(balance(bob, USD(40)), balance(alice, USD(0)));
        }
        {
            // transfer rate is not charged when issuer is src or dst
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, gw);
            env(rate(gw, 1.25));
            env.trust(USD(1000), alice, bob, carol);
            env(pay(gw, alice, USD(50)));
            env.require(balance(alice, USD(50)));
            env(pay(alice, gw, USD(40)), sendmax(USD(40)));
            env.require(balance(alice, USD(10)));
        }
        {
            // transfer fee on an offer
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, gw);
            env(rate(gw, 1.25));
            env.trust(USD(1000), alice, bob, carol);
            env(pay(gw, bob, USD(65)));

            env(offer(bob, XRP(50), USD(50)));

            env(pay(alice, carol, USD(50)), path(~USD), sendmax(XRP(50)));
            env.require(
                balance(alice, xrpMinusFee(env, 10000 - 50)),
                balance(bob, USD(2.5)),  // owner pays transfer fee
                balance(carol, USD(50)));
        }

        {
            // Transfer fee two consecutive offers
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, gw);
            env(rate(gw, 1.25));
            env.trust(USD(1000), alice, bob, carol);
            env.trust(EUR(1000), alice, bob, carol);
            env(pay(gw, bob, USD(50)));
            env(pay(gw, bob, EUR(50)));

            env(offer(bob, XRP(50), USD(50)));
            env(offer(bob, USD(50), EUR(50)));

            env(pay(alice, carol, EUR(40)), path(~USD, ~EUR), sendmax(XRP(40)));
            env.require(
                balance(alice, xrpMinusFee(env, 10000 - 40)),
                balance(bob, USD(40)),
                balance(bob, EUR(0)),
                balance(carol, EUR(40)));
        }

        {
            // First pass through a strand redeems, second pass issues, no
            // offers limiting step is not an endpoint
            Env env(*this, features);
            auto const USDA = alice["USD"];
            auto const USDB = bob["USD"];

            env.fund(XRP(10000), alice, bob, carol, gw);
            env(rate(gw, 1.25));
            env.trust(USD(1000), alice, bob, carol);
            env.trust(USDA(1000), bob);
            env.trust(USDB(1000), gw);
            env(pay(gw, bob, USD(50)));
            // alice -> bob -> gw -> carol. $50 should have transfer fee; $10,
            // no fee
            env(pay(alice, carol, USD(50)), path(bob), sendmax(USDA(60)));
            env.require(
                balance(bob, USD(-10)),
                balance(bob, USDA(60)),
                balance(carol, USD(50)));
        }
        {
            // First pass through a strand redeems, second pass issues, through
            // an offer limiting step is not an endpoint
            Env env(*this, features);
            auto const USDA = alice["USD"];
            auto const USDB = bob["USD"];
            Account const dan("dan");

            env.fund(XRP(10000), alice, bob, carol, dan, gw);
            env(rate(gw, 1.25));
            env.trust(USD(1000), alice, bob, carol, dan);
            env.trust(EUR(1000), carol, dan);
            env.trust(USDA(1000), bob);
            env.trust(USDB(1000), gw);
            env(pay(gw, bob, USD(50)));
            env(pay(gw, dan, EUR(100)));
            env(offer(dan, USD(100), EUR(100)));
            // alice -> bob -> gw -> carol. $50 should have transfer fee; $10,
            // no fee
            env(pay(alice, carol, EUR(50)),
                path(bob, gw, ~EUR),
                sendmax(USDA(60)),
                txflags(tfNoRippleDirect));
            env.require(
                balance(bob, USD(-10)),
                balance(bob, USDA(60)),
                balance(dan, USD(50)),
                balance(dan, EUR(37.5)),
                balance(carol, EUR(50)));
        }

        {
            // Offer where the owner is also the issuer, owner pays fee
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, gw);
            env(rate(gw, 1.25));
            env.trust(USD(1000), alice, bob);
            env(offer(gw, XRP(100), USD(100)));
            env(pay(alice, bob, USD(100)), sendmax(XRP(100)));
            env.require(
                balance(alice, xrpMinusFee(env, 10000 - 100)),
                balance(bob, USD(100)));
        }
        if (!features[featureOwnerPaysFee])
        {
            // Offer where the owner is also the issuer, sender pays fee
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, gw);
            env(rate(gw, 1.25));
            env.trust(USD(1000), alice, bob);
            env(offer(gw, XRP(125), USD(125)));
            env(pay(alice, bob, USD(100)), sendmax(XRP(200)));
            env.require(
                balance(alice, xrpMinusFee(env, 10000 - 125)),
                balance(bob, USD(100)));
        }
    }

    void
    testFalseDry(FeatureBitset features)
    {
        testcase("falseDryChanges");

        using namespace jtx;

        auto const gw = Account("gateway");
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");

        Env env(*this, features);

        env.fund(XRP(10'000), alice, carol, gw);
        env.fund(reserve(env, 5), bob);

        MPT const USD = MPTTester(
            {.env = env, .issuer = gw, .holders = {alice, carol, bob}});
        MPT const EUR = MPTTester(
            {.env = env, .issuer = gw, .holders = {alice, carol, bob}});

        env(pay(gw, alice, EUR(50)));
        env(pay(gw, bob, USD(50)));

        // Bob has _just_ slightly less than 50 xrp available
        // If his owner count changes, he will have more liquidity.
        // This is one error case to test (when Flow is used).
        // Computing the incoming xrp to the XRP/USD offer will require two
        // recursive calls to the EUR/XRP offer. The second call will return
        // tecPATH_DRY, but the entire path should not be marked as dry. This
        // is the second error case to test (when flowV1 is used).
        env(offer(bob, EUR(50), XRP(50)));
        env(offer(bob, XRP(50), USD(50)));

        env(pay(alice, carol, USD(1'000'000)),
            path(~XRP, ~USD),
            sendmax(EUR(500)),
            txflags(tfNoRippleDirect | tfPartialPayment));

        auto const carolUSD = env.balance(carol, USD).value();
        BEAST_EXPECT(carolUSD > USD(0) && carolUSD < USD(50));
    }

    void
    testLimitQuality()
    {
        // Single path with two offers and limit quality. The quality limit is
        // such that the first offer should be taken but the second should not.
        // The total amount delivered should be the sum of the two offers and
        // sendMax should be more than the first offer.
        testcase("limitQuality");
        using namespace jtx;

        auto const gw = Account("gateway");
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");

        {
            Env env(*this);

            env.fund(XRP(10'000), alice, bob, carol, gw);

            MPT const USD = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice, bob, carol}});

            env(pay(gw, bob, USD(100)));
            env(offer(bob, XRP(50), USD(50)));
            env(offer(bob, XRP(100), USD(50)));

            env(pay(alice, carol, USD(100)),
                path(~USD),
                sendmax(XRP(100)),
                txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));

            env.require(balance(carol, USD(50)));
        }
    }

    // Helper function that returns the reserve on an account based on
    // the passed in number of owners.
    static XRPAmount
    reserve(jtx::Env& env, std::uint32_t count)
    {
        return env.current()->fees().accountReserve(count);
    }

    // Helper function that returns the Offers on an account.
    static std::vector<std::shared_ptr<SLE const>>
    offersOnAccount(jtx::Env& env, jtx::Account account)
    {
        std::vector<std::shared_ptr<SLE const>> result;
        forEachItem(
            *env.current(),
            account,
            [&result](std::shared_ptr<SLE const> const& sle) {
                if (sle->getType() == ltOFFER)
                    result.push_back(sle);
            });
        return result;
    }

    void
    testSelfPayment1(FeatureBitset features)
    {
        testcase("Self-payment 1");

        // In this test case the new flow code mis-computes the amount
        // of money to move.  Fortunately the new code's re-execute
        // check catches the problem and throws out the transaction.
        //
        // The old payment code handles the payment correctly.
        using namespace jtx;

        auto const gw1 = Account("gw1");
        auto const gw2 = Account("gw2");
        auto const alice = Account("alice");

        Env env(*this, features);

        env.fund(XRP(1'000'000), gw1, gw2);
        env.close();

        // The fee that's charged for transactions.
        auto const f = env.current()->fees().base;

        env.fund(reserve(env, 3) + f * 4, alice);
        env.close();

        MPT const USD =
            MPTTester({.env = env, .issuer = gw1, .holders = {alice}});
        MPT const EUR =
            MPTTester({.env = env, .issuer = gw2, .holders = {alice}});

        env(pay(gw1, alice, USD(10)));
        env(pay(gw2, alice, EUR(10'000)));
        env.close();

        env(offer(alice, USD(5'000), EUR(6'000)));
        env.close();

        env.require(owners(alice, 3));
        env.require(balance(alice, USD(10)));
        env.require(balance(alice, EUR(10'000)));

        auto aliceOffers = offersOnAccount(env, alice);
        BEAST_EXPECT(aliceOffers.size() == 1);
        for (auto const& offerPtr : aliceOffers)
        {
            auto const offer = *offerPtr;
            BEAST_EXPECT(offer[sfLedgerEntryType] == ltOFFER);
            BEAST_EXPECT(offer[sfTakerGets] == EUR(6'000));
            BEAST_EXPECT(offer[sfTakerPays] == USD(5'000));
        }

        env(pay(alice, alice, EUR(6'000)),
            sendmax(USD(5'000)),
            txflags(tfPartialPayment));
        env.close();

        env.require(owners(alice, 3));
        env.require(balance(alice, USD(10)));
        env.require(balance(alice, EUR(10'000)));
        aliceOffers = offersOnAccount(env, alice);
        BEAST_EXPECT(aliceOffers.size() == 1);
        for (auto const& offerPtr : aliceOffers)
        {
            auto const offer = *offerPtr;
            BEAST_EXPECT(offer[sfLedgerEntryType] == ltOFFER);
            BEAST_EXPECT(offer[sfTakerGets] == EUR(5'989));
            BEAST_EXPECT(offer[sfTakerPays] == USD(4'990));
        }
    }

    void
    testSelfPayment2(FeatureBitset features)
    {
        testcase("Self-payment 2");

        // In this case the difference between the old payment code and
        // the new is the values left behind in the offer.  Not saying either
        // ios ring, they are just different.
        using namespace jtx;

        auto const gw1 = Account("gw1");
        auto const gw2 = Account("gw2");
        auto const alice = Account("alice");

        Env env(*this, features);

        env.fund(XRP(1'000'000), gw1, gw2);
        env.close();

        // The fee that's charged for transactions.
        auto const f = env.current()->fees().base;

        env.fund(reserve(env, 3) + f * 4, alice);
        env.close();

        MPT const USD = MPTTester(
            {.env = env, .issuer = gw1, .holders = {alice}, .maxAmt = 506});
        MPT const EUR = MPTTester(
            {.env = env, .issuer = gw2, .holders = {alice}, .maxAmt = 606});

        env(pay(gw1, alice, USD(500)));
        env(pay(gw2, alice, EUR(600)));
        env.close();

        env(offer(alice, USD(500), EUR(600)));
        env.close();

        env.require(owners(alice, 3));
        env.require(balance(alice, USD(500)));
        env.require(balance(alice, EUR(600)));

        auto aliceOffers = offersOnAccount(env, alice);
        BEAST_EXPECT(aliceOffers.size() == 1);
        for (auto const& offerPtr : aliceOffers)
        {
            auto const offer = *offerPtr;
            BEAST_EXPECT(offer[sfLedgerEntryType] == ltOFFER);
            BEAST_EXPECT(offer[sfTakerGets] == EUR(600));
            BEAST_EXPECT(offer[sfTakerPays] == USD(500));
        }

        env(pay(alice, alice, EUR(60)),
            sendmax(USD(50)),
            txflags(tfPartialPayment));
        env.close();

        env.require(owners(alice, 3));
        env.require(balance(alice, USD(500)));
        env.require(balance(alice, EUR(600)));
        aliceOffers = offersOnAccount(env, alice);
        BEAST_EXPECT(aliceOffers.size() == 1);
        for (auto const& offerPtr : aliceOffers)
        {
            auto const offer = *offerPtr;
            BEAST_EXPECT(offer[sfLedgerEntryType] == ltOFFER);
            BEAST_EXPECT(offer[sfTakerGets] == EUR(594));
            BEAST_EXPECT(offer[sfTakerPays] == USD(495));
        }
    }

    void
    testSelfFundedXRPEndpoint(bool consumeOffer, FeatureBitset features)
    {
        // Test that the deferred credit table is not bypassed for
        // XRPEndpointSteps. If the account in the first step is sending XRP and
        // that account also owns an offer that receives XRP, it should not be
        // possible for that step to use the XRP received in the offer as part
        // of the payment.
        testcase("Self funded XRPEndpoint");

        using namespace jtx;

        Env env(*this, features);

        auto const alice = Account("alice");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];

        env.fund(XRP(10000), alice, gw);
        env(trust(alice, USD(20)));
        env(pay(gw, alice, USD(10)));
        env(offer(alice, XRP(50000), USD(10)));

        // Consuming the offer changes the owner count, which could also cause
        // liquidity to decrease in the forward pass
        auto const toSend = consumeOffer ? USD(10) : USD(9);
        env(pay(alice, alice, toSend),
            path(~USD),
            sendmax(XRP(20000)),
            txflags(tfPartialPayment | tfNoRippleDirect));
    }

    void
    testUnfundedOffer(FeatureBitset features)
    {
        testcase("Unfunded Offer");

        using namespace jtx;
        {
            // Test reverse
            Env env(*this, features);

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            auto const USD = gw["USD"];

            env.fund(XRP(100000), alice, bob, gw);
            env(trust(bob, USD(20)));

            STAmount tinyAmt1{
                USD, 9000000000000000ll, -17, false, STAmount::unchecked{}};
            STAmount tinyAmt3{
                USD, 9000000000000003ll, -17, false, STAmount::unchecked{}};

            env(offer(gw, drops(9000000000), tinyAmt3));
            env(pay(alice, bob, tinyAmt1),
                path(~USD),
                sendmax(drops(9000000000)),
                txflags(tfNoRippleDirect));

            BEAST_EXPECT(!isOffer(env, gw, XRP(0), USD(0)));
        }
        {
            // Test forward
            Env env(*this, features);

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            auto const USD = gw["USD"];

            env.fund(XRP(100000), alice, bob, gw);
            env(trust(alice, USD(20)));

            STAmount tinyAmt1{
                USD, 9000000000000000ll, -17, false, STAmount::unchecked{}};
            STAmount tinyAmt3{
                USD, 9000000000000003ll, -17, false, STAmount::unchecked{}};

            env(pay(gw, alice, tinyAmt1));

            env(offer(gw, tinyAmt3, drops(9000000000)));
            env(pay(alice, bob, drops(9000000000)),
                path(~XRP),
                sendmax(USD(1)),
                txflags(tfNoRippleDirect));

            BEAST_EXPECT(!isOffer(env, gw, USD(0), XRP(0)));
        }
    }

    void
    testReexecuteDirectStep(FeatureBitset features)
    {
        testcase("ReexecuteDirectStep");

        using namespace jtx;
        Env env(*this, features);

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];
        // auto const usdC = USD.currency;

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env(trust(alice, USD(100)));
        env.close();

        // BEAST_EXPECT(!getNoRippleFlag(env, gw, alice, usdC));

        env(
            pay(gw,
                alice,
                // 12.55....
                STAmount{USD, std::uint64_t(1255555555555555ull), -14, false}));

        env(offer(
            gw,
            // 5.0...
            STAmount{USD, std::uint64_t(5000000000000000ull), -15, false},
            XRP(1000)));

        env(offer(
            gw,
            // .555...
            STAmount{USD, std::uint64_t(5555555555555555ull), -16, false},
            XRP(10)));

        env(offer(
            gw,
            // 4.44....
            STAmount{USD, std::uint64_t(4444444444444444ull), -15, false},
            XRP(.1)));

        env(offer(
            alice,
            // 17
            STAmount{USD, std::uint64_t(1700000000000000ull), -14, false},
            XRP(.001)));

        env(pay(alice, bob, XRP(10000)),
            path(~XRP),
            sendmax(USD(100)),
            txflags(tfPartialPayment | tfNoRippleDirect));
    }

    void
    testSelfPayLowQualityOffer(FeatureBitset features)
    {
        // The new payment code used to assert if an offer was made for more
        // XRP than the offering account held.  This unit test reproduces
        // that failing case.
        testcase("Self crossing low quality offer");

        using namespace jtx;

        Env env(*this, features);

        auto const ann = Account("ann");
        auto const gw = Account("gateway");
        auto const CTB = gw["CTB"];

        auto const fee = env.current()->fees().base;
        env.fund(reserve(env, 2) + drops(9999640) + fee, ann);
        env.fund(reserve(env, 2) + fee * 4, gw);
        env.close();

        env(rate(gw, 1.002));
        env(trust(ann, CTB(10)));
        env.close();

        env(pay(gw, ann, CTB(2.856)));
        env.close();

        env(offer(ann, drops(365611702030), CTB(5.713)));
        env.close();

        // This payment caused the assert.
        env(pay(ann, ann, CTB(0.687)),
            sendmax(drops(20000000000)),
            txflags(tfPartialPayment));
    }

    void
    testEmptyStrand(FeatureBitset features)
    {
        testcase("Empty Strand");
        using namespace jtx;

        auto const alice = Account("alice");

        Env env(*this, features);

        env.fund(XRP(10000), alice);

        env(pay(alice, alice, alice["USD"](100)),
            path(~alice["USD"]),
            ter(temBAD_PATH));
    }

    void
    testXRPPathLoop()
    {
        testcase("Circular XRP");

        using namespace jtx;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];

        {
            // Payment path starting with XRP
            Env env(*this);
            env.fund(XRP(10'000), alice, bob, gw);

            MPT const USD =
                MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}});
            MPT const EUR =
                MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}});
            env(pay(gw, alice, USD(100)));
            env(pay(gw, alice, EUR(100)));
            env.close();

            env(offer(alice, XRP(100), USD(100)), txflags(tfPassive));
            env(offer(alice, USD(100), XRP(100)), txflags(tfPassive));
            env(offer(alice, XRP(100), EUR(100)), txflags(tfPassive));
            env.close();

            TER const expectedTer = TER{temBAD_PATH_LOOP};
            env(pay(alice, bob, EUR(1)),
                path(~USD, ~XRP, ~EUR),
                sendmax(XRP(1)),
                txflags(tfNoRippleDirect),
                ter(expectedTer));
        }
        {
            // Payment path ending with XRP
            Env env(*this);
            env.fund(XRP(10'000), alice, bob, gw);
            MPT const USD =
                MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}});
            MPT const EUR =
                MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}});
            env(pay(gw, alice, USD(100)));
            env(pay(gw, alice, EUR(100)));
            env.close();

            env(offer(alice, XRP(100), USD(100)), txflags(tfPassive));
            env(offer(alice, EUR(100), XRP(100)), txflags(tfPassive));
            env.close();
            // EUR -> //XRP -> //USD ->XRP
            env(pay(alice, bob, XRP(1)),
                path(~XRP, ~USD, ~XRP),
                sendmax(EUR(1)),
                txflags(tfNoRippleDirect),
                ter(temBAD_PATH_LOOP));
        }
        {
            // Payment where loop is formed in the middle of the path, not on an
            // endpoint
            Env env(*this);
            env.fund(XRP(10'000), alice, bob, gw);
            env.close();
            MPT const USD =
                MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}});
            MPT const EUR =
                MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}});
            MPT const JPY =
                MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}});
            env(pay(gw, alice, USD(100)));
            env(pay(gw, alice, EUR(100)));
            env(pay(gw, alice, JPY(100)));
            env.close();

            env(offer(alice, USD(100), XRP(100)), txflags(tfPassive));
            env(offer(alice, XRP(100), EUR(100)), txflags(tfPassive));
            env(offer(alice, EUR(100), XRP(100)), txflags(tfPassive));
            env(offer(alice, XRP(100), JPY(100)), txflags(tfPassive));
            env.close();

            env(pay(alice, bob, JPY(1)),
                path(~XRP, ~EUR, ~XRP, ~JPY),
                sendmax(USD(1)),
                txflags(tfNoRippleDirect),
                ter(temBAD_PATH_LOOP));
        }
    }

    void
    testTicketPay(FeatureBitset features)
    {
        testcase("Payment with ticket");
        using namespace jtx;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env(*this, features);

        env.fund(XRP(10000), alice);

        // alice creates a ticket for the payment.
        std::uint32_t const ticketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, 1));

        // Make a payment using the ticket.
        env(pay(alice, bob, XRP(1000)), ticket::use(ticketSeq));
        env.close();
        env.require(balance(bob, XRP(1000)));
        env.require(balance(alice, XRP(9000) - drops(20)));
    }

    void
    testWithFeats(FeatureBitset features)
    {
        using namespace jtx;

        testFalseDry(features);
        testDirectStep(features);
        testBookStep(features);
        testSelfPayment1(features);
        return;
        testSelfPayment2(features);
        testSelfFundedXRPEndpoint(false, features);
        testSelfFundedXRPEndpoint(true, features);
        testUnfundedOffer(features);
        testReexecuteDirectStep(features);
        testSelfPayLowQualityOffer(features);
        testTicketPay(features);
    }

    void
    run() override
    {
        testLimitQuality();
        testXRPPathLoop();
        using namespace jtx;
        auto const sa = supported_amendments();
        testWithFeats(sa);
        return;
        testEmptyStrand(sa);
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(FlowMPT, app, ripple, 2);

}  // namespace test
}  // namespace ripple
