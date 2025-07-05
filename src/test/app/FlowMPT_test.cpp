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

struct FlowMPT_test : public beast::unit_test::suite
{
    using Accounts = std::vector<jtx::Account>;

    struct IssuerArgs
    {
        jtx::Env& env;
        // 3-letter currency if Issue, ignored if MPT
        std::string token = "";
        jtx::Account issuer;
        Accounts holders;
        // trust-limit if Issue, maxAmount if MPT
        std::optional<std::uint64_t> limit = std::nullopt;
        // 0-50'000 (0-50%)
        std::uint16_t transferFee = 0;
    };

    static auto
    issueHelperIOU(IssuerArgs const& args)
    {
        auto const iou = args.issuer[args.token];
        if (args.transferFee != 0)
        {
            auto const tfee =
                1. + static_cast<double>(args.transferFee) / 100'000;
            args.env(rate(args.issuer, tfee));
        }
        for (auto const& account : args.holders)
        {
            args.env(fset(account, asfDefaultRipple));
            args.env(trust(account, iou(args.limit.value_or(1'000))));
        }
        return iou;
    }

    static auto
    issueHelperMPT(IssuerArgs const& args)
    {
        using namespace jtx;
        if (args.limit)
        {
            MPT const mpt = MPTTester(
                {.env = args.env,
                 .issuer = args.issuer,
                 .holders = args.holders,
                 .transferFee = args.transferFee,
                 .maxAmt = *args.limit});
            return mpt;
        }
        else
        {
            MPT const mpt = MPTTester(
                {.env = args.env,
                 .issuer = args.issuer,
                 .holders = args.holders,
                 .transferFee = args.transferFee});
            return mpt;
        }
    }

    static auto
    testHelper2TokensMix(auto&& tester)
    {
        tester(issueHelperMPT, issueHelperMPT);
        tester(issueHelperIOU, issueHelperMPT);
        tester(issueHelperMPT, issueHelperIOU);
    }

    static auto
    testHelper3TokensMix(auto&& tester)
    {
        tester(issueHelperMPT, issueHelperMPT, issueHelperMPT);
        tester(issueHelperMPT, issueHelperMPT, issueHelperIOU);
        tester(issueHelperMPT, issueHelperIOU, issueHelperMPT);
        tester(issueHelperMPT, issueHelperIOU, issueHelperIOU);
        tester(issueHelperIOU, issueHelperMPT, issueHelperMPT);
        tester(issueHelperIOU, issueHelperMPT, issueHelperIOU);
        tester(issueHelperIOU, issueHelperIOU, issueHelperMPT);
    }

    void
    testDirectStep(FeatureBitset features)
    {
        testcase("Direct Step");

        using namespace jtx;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
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
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this, features);

                env.fund(XRP(10'000), gw, alice, bob, carol);

                auto const USD = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, carol}});
                auto const EUR = issue2(
                    {.env = env, .token = "EUR", .issuer = gw, .holders = {bob}});

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
            };
            testHelper2TokensMix(test);
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
            // simple [MPT|IOU]/[IOU|MPT] offer
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this, features);

                env.fund(XRP(10'000), alice, bob, carol, gw);

                auto const USD = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, bob, carol}});
                auto const BTC = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw,
                     .holders = {alice, bob, carol}});

                env(pay(gw, alice, BTC(50)));
                env(pay(gw, bob, USD(50)));

                env(offer(bob, BTC(50), USD(50)));

                env(pay(alice, carol, USD(50)), path(~USD), sendmax(BTC(50)));

                env.require(balance(alice, BTC(0)));
                env.require(balance(bob, BTC(50)));
                env.require(balance(bob, USD(0)));
                env.require(balance(carol, USD(50)));
                BEAST_EXPECT(!isOffer(env, bob, BTC(50), USD(50)));
            };
            testHelper2TokensMix(test);
        }
        {
            // simple [MPT|IOU]/XRP XRP/[IOU|MPT] offer
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this, features);

                env.fund(XRP(10'000), alice, bob, carol, gw);

                auto const USD = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, bob, carol}});
                auto const BTC = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw,
                     .holders = {alice, bob, carol}});

                env(pay(gw, alice, BTC(50)));
                env(pay(gw, bob, USD(50)));

                env(offer(bob, BTC(50), XRP(50)));
                env(offer(bob, XRP(50), USD(50)));

                env(pay(alice, carol, USD(50)),
                    path(~XRP, ~USD),
                    sendmax(BTC(50)));

                env.require(balance(alice, BTC(0)));
                env.require(balance(bob, BTC(50)));
                env.require(balance(bob, USD(0)));
                env.require(balance(carol, USD(50)));
                BEAST_EXPECT(!isOffer(env, bob, XRP(50), USD(50)));
                BEAST_EXPECT(!isOffer(env, bob, BTC(50), XRP(50)));
            };
            testHelper2TokensMix(test);
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
            auto test = [&](auto&& issue1, auto&& issue2, auto&& issue3) {
                Env env(*this, features);

                env.fund(XRP(10'000), alice, bob, carol, gw);

                auto const USD = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, bob, carol}});
                auto const BTC = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw,
                     .holders = {alice, bob, carol}});
                auto const EUR = issue3(
                    {.env = env,
                     .token = "EUR",
                     .issuer = gw,
                     .holders = {alice, bob, carol}});

                env(pay(gw, alice, BTC(60)));
                env(pay(gw, bob, USD(50)));
                env(pay(gw, bob, EUR(50)));

                env(offer(bob, BTC(50), USD(50)));
                env(offer(bob, BTC(40), EUR(50)));
                env(offer(bob, EUR(50), USD(50)));

                // unfund offer
                env(pay(bob, gw, EUR(50)));
                env.require(balance(bob, EUR(0)));
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
            };
            testHelper3TokensMix(test);
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
            auto test = [&](auto&& issue1, auto&& issue2, auto&& issue3) {
                Env env(*this, features);

                env.fund(XRP(10'000), alice, bob, carol, gw);

                auto const USD = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 100'000});
                auto const BTC = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 100'000});
                auto const EUR = issue3(
                    {.env = env,
                     .token = "EUR",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 100'000});

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
                            STPathElement::typeAsset |
                                STPathElement::typeIssuer,
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
                        std::nullopt,
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

                // used in payment, but since payment failed should
                // be untouched
                BEAST_EXPECT(isOffer(env, bob, BTC(50), USD(5'000)));
                BEAST_EXPECT(isOffer(env, carol, BTC(1'000), EUR(100)));
                // found unfunded
                BEAST_EXPECT(!isOffer(env, bob, BTC(60), EUR(5'000)));
            };
            testHelper3TokensMix(test);
        }
        {
            // Do not produce more in the forward pass than the
            // reverse pass This test uses a path that whose reverse
            // pass will compute a 0.5 USD input required for a 1
            // EUR output. It sets a sendmax of 0.4 USD, so the
            // payment engine will need to do a forward pass.
            // Without limits, the 0.4 USD would produce 1000 EUR in
            // the forward pass. This test checks that the payment
            // produces 1 EUR, as expected.
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this, features);
                env.fund(XRP(10'000), alice, bob, carol, gw);

                auto const USD = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, bob, carol}});
                auto const EUR = issue1(
                    {.env = env,
                     .token = "EUR",
                     .issuer = gw,
                     .holders = {alice, bob, carol}});

                env(pay(gw, alice, USD(1'000)));
                env(pay(gw, bob, EUR(1'000)));

                Keylet const bobUsdOffer = keylet::offer(bob, env.seq(bob));
                env(offer(bob, USD(10), drops(2)), txflags(tfPassive));
                env(offer(bob, drops(1), EUR(1'000)), txflags(tfPassive));

                bool const reducedOffersV2 = features[fixReducedOffersV2];

                // With reducedOffersV2, it is not allowed to accept
                // less than USD(0.5) of bob's USD offer.  If we
                // provide 1 drop for less than USD(0.5), then the
                // remaining fractional offer would block the order
                // book.
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
                    std::shared_ptr<SLE const> const usdOffer =
                        env.le(bobUsdOffer);
                    if (BEAST_EXPECT(usdOffer))
                    {
                        std::uint64_t const bookRate = [&usdOffer]() {
                            // Extract the least significant 64
                            // bits from the book page.  That's
                            // where the quality is stored.
                            std::string bookDirStr =
                                to_string(usdOffer->at(sfBookDirectory));
                            bookDirStr.erase(0, 48);
                            return std::stoull(bookDirStr, nullptr, 16);
                        }();
                        std::uint64_t const actualRate = getRate(
                            usdOffer->at(sfTakerGets),
                            usdOffer->at(sfTakerPays));

                        // We expect the actual rate of the offer to
                        // be worse (larger) than the rate of the
                        // book page holding the offer.  This is a
                        // defect which is corrected by
                        // fixReducedOffersV2.
                        BEAST_EXPECT(actualRate > bookRate);
                    }
                }
            };
            testHelper2TokensMix(test);
        }
    }

    void
    testTransferRate(FeatureBitset features)
    {
        testcase("Transfer Rate");

        using namespace jtx;

        auto const gw = Account("gateway");
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");

        {
            // Simple payment through a gateway with a
            // transfer rate
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, gw);

            MPT const USD = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .transferFee = 25'000,
                 .maxAmt = 1'000});

            env(pay(gw, alice, USD(50)));
            env.require(balance(alice, USD(50)));
            env(pay(alice, bob, USD(40)), sendmax(USD(50)));
            env.require(balance(bob, USD(40)), balance(alice, USD(0)));
        }
        {
            // transfer rate is not charged when issuer is src or
            // dst
            Env env(*this, features);

            env.fund(XRP(10'000), alice, bob, carol, gw);

            MPT const USD = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .transferFee = 25'000,
                 .maxAmt = 1'000});

            env(pay(gw, alice, USD(50)));
            env.require(balance(alice, USD(50)));
            env(pay(alice, gw, USD(40)), sendmax(USD(40)));
            env.require(balance(alice, USD(10)));
        }
        {
            // transfer fee on an offer
            Env env(*this, features);

            env.fund(XRP(10'000), alice, bob, carol, gw);

            MPT const USD = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .transferFee = 25'000,
                 .maxAmt = 10'000});

            // scale by 1
            env(pay(gw, bob, USD(650)));

            env(offer(bob, XRP(50), USD(500)));

            env(pay(alice, carol, USD(500)),
                path(~USD),
                sendmax(XRP(50)),
                txflags(tfPartialPayment));

            // bob pays 25% on 500USD -> 100USD; 400USD goes to carol
            env.require(
                balance(alice, XRP(10'000 - 50) - txfee(env, 2)),
                balance(bob, USD(150)),
                balance(carol, USD(400)));
        }
        {
            // Transfer fee two consecutive offers
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this, features);

                env.fund(XRP(10'000), alice, bob, carol, gw);

                auto const USD = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 1'000,
                     .transferFee = 25'000});
                auto const EUR = issue2(
                    {.env = env,
                     .token = "EUR",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 1'000,
                     .transferFee = 25'000});

                env(pay(gw, bob, USD(50)));
                env(pay(gw, bob, EUR(50)));

                env(offer(bob, XRP(50), USD(50)));
                env(offer(bob, USD(50), EUR(50)));

                env(pay(alice, carol, EUR(40)),
                    path(~USD, ~EUR),
                    sendmax(XRP(40)),
                    txflags(tfPartialPayment));
                // +1 for fset in helperIssueIOU
                using tEUR = std::decay_t<decltype(EUR)>;
                using tUSD = std::decay_t<decltype(USD)>;
                bool constexpr extraFee =
                    std::is_same_v<tEUR, IOU> || std::is_same_v<tUSD, IOU>;
                auto const fee = txfee(env, extraFee ? 4 : 3);
                // bob pays 25% on 40USD (40 since sendmax is 40XRP)
                // 8USD goes to gw and 32USD goes back to bob ->
                // bob's USD balance is 42USD. USD/EUR offer is 32USD/32EUR.
                // bob pays 25% on 32EUR -> 7EUR if MPT, 6.4EUR if IOU,
                // therefore carl gets 25EUR if MPT, 25.6EUR if IOU.
                auto const carolEUR = [&]() {
                    if constexpr (std::is_same_v<tEUR, IOU>)
                        return EUR(25.6);
                    else
                        return EUR(25);
                }();
                env.require(
                    balance(alice, XRP(10'000 - 40) - fee),
                    balance(bob, USD(42)),
                    balance(bob, EUR(18)),
                    balance(carol, carolEUR));
            };
            testHelper2TokensMix(test);
        }
        {
            // Offer where the owner is also the issuer, sender pays
            // fee
            Env env(*this, features);

            env.fund(XRP(10'000), alice, bob, gw);

            MPT const USD = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .transferFee = 25'000,
                 .maxAmt = 1'000});

            env(offer(gw, XRP(100), USD(100)));
            env(pay(alice, bob, USD(100)),
                sendmax(XRP(100)),
                txflags(tfPartialPayment));
            env.require(
                balance(alice, XRP(10'000 - 100) - txfee(env, 2)),
                balance(bob, USD(80)));
        }
        {
            // Offer where the owner is also the issuer, sender pays
            // fee
            Env env(*this, features - featureOwnerPaysFee);

            env.fund(XRP(10'000), alice, bob, gw);

            MPT const USD = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .transferFee = 25'000,
                 .maxAmt = 1'000});

            env(offer(gw, XRP(125), USD(125)));
            env(pay(alice, bob, USD(100)), sendmax(XRP(200)));
            env.require(
                balance(alice, XRP(10'000 - 125) - txfee(env, 2)),
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

        auto test = [&](auto&& issue1, auto&& issue2) {
            Env env(*this, features);

            env.fund(XRP(10'000), alice, carol, gw);
            env.fund(reserve(env, 5), bob);

            auto const USD = issue1(
                {.env = env,
                 .token = "USD",
                 .issuer = gw,
                 .holders = {alice, carol, bob}});
            auto const EUR = issue2(
                {.env = env,
                 .token = "EUR",
                 .issuer = gw,
                 .holders = {alice, carol, bob}});

            env(pay(gw, alice, EUR(50)));
            env(pay(gw, bob, USD(50)));

            // Bob has _just_ slightly less than 50 xrp available
            // If his owner count changes, he will have more liquidity.
            // This is one error case to test (when Flow is used).
            // Computing the incoming xrp to the XRP/USD offer will
            // require two recursive calls to the EUR/XRP offer. The
            // second call will return tecPATH_DRY, but the entire path
            // should not be marked as dry. This is the second error
            // case to test (when flowV1 is used).
            env(offer(bob, EUR(50), XRP(50)));
            env(offer(bob, XRP(50), USD(50)));

            env(pay(alice, carol, USD(1'000'000)),
                path(~XRP, ~USD),
                sendmax(EUR(500)),
                txflags(tfNoRippleDirect | tfPartialPayment));

            auto const carolUSD = env.balance(carol, USD).value();
            BEAST_EXPECT(carolUSD > USD(0) && carolUSD < USD(50));
        };
        testHelper2TokensMix(test);
    }

    void
    testLimitQuality()
    {
        // Single path with two offers and limit quality. The
        // quality limit is such that the first offer should be
        // taken but the second should not. The total amount
        // delivered should be the sum of the two offers and sendMax
        // should be more than the first offer.
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

        // In this test case the new flow code mis-computes the
        // amount of money to move.  Fortunately the new code's
        // re-execute check catches the problem and throws out the
        // transaction.
        //
        // The old payment code handles the payment correctly.
        using namespace jtx;

        auto test = [&](auto& issue1, auto&& issue2) {
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

            auto const USD = issue1(
                {.env = env,
                 .token = "USD",
                 .issuer = gw1,
                 .holders = {alice},
                 .limit = 20'000});
            auto const EUR = issue2(
                {.env = env,
                 .token = "EUR",
                 .issuer = gw2,
                 .holders = {alice},
                 .limit = 20'000});

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
                if constexpr (std::is_same_v<std::decay_t<decltype(EUR)>, IOU>)
                    BEAST_EXPECT(offer[sfTakerGets] == EUR(5'988));
                else
                    BEAST_EXPECT(offer[sfTakerGets] == EUR(5'989));
                BEAST_EXPECT(offer[sfTakerPays] == USD(4'990));
            }
        };
        testHelper2TokensMix(test);
    }

    template <typename TGets, typename TPays>
    struct TokenData
    {
        TGets EUR;
        TPays USD;
        jtx::PrettyAmount remTakerGets;
        jtx::PrettyAmount remTakerPays;
    };

    void
    testSelfPayment2(FeatureBitset features)
    {
        testcase("Self-payment 2");

        using namespace jtx;

        // This test shows a difference between IOU and MPT
        // self-payment result depending on IOU trustline limit.

        auto const gw1 = Account("gw1");
        auto const gw2 = Account("gw2");
        auto const alice = Account("alice");

        auto initMPT = [&](Env& env) {
            MPT const USD = MPTTester(
                {.env = env, .issuer = gw1, .holders = {alice}, .maxAmt = 506});
            MPT const EUR = MPTTester(
                {.env = env, .issuer = gw2, .holders = {alice}, .maxAmt = 606});
            // Payment's engine last step overflows
            // OutstandingAmount since it doesn't know if the
            // BookStep redeems or not. The BookStep then has 600EUR
            // available. Consequently, the entire offer is crossed.
            return TokenData<MPT, MPT>{EUR, USD, EUR(540), USD(450)};
        };

        auto initIOU = [&](Env& env) {
            auto const USD = gw1["USD"];
            auto const EUR = gw2["EUR"];
            env(trust(alice, USD(506)));
            env(trust(alice, EUR(606)));
            env.close();
            // Payment's engine last step is limited by alice's
            // trustline - 606. Therefore, only 6EUR is delivered
            // and the offer is partially crossed.
            return TokenData<IOU, IOU>{EUR, USD, EUR(594), USD(495)};
        };

        auto initIOU1 = [&](Env& env) {
            auto const USD = gw1["USD"];
            auto const EUR = gw2["EUR"];
            env(trust(alice, USD(1'000)));
            env(trust(alice, EUR(1'000)));
            env.close();
            // Payment's engine last step is not limited by alice's
            // trustline. Therefore, the entire offer is crossed.
            // This the same result as with MPT.
            return TokenData<IOU, IOU>{EUR, USD, EUR(540), USD(450)};
        };

        auto test = [&](auto&& initToken) {
            Env env(*this, features);

            env.fund(XRP(1'000'000), gw1, gw2);
            env.close();

            // The fee that's charged for transactions.
            auto const f = env.current()->fees().base;

            env.fund(reserve(env, 3) + f * 4, alice);
            env.close();

            auto const tok = initToken(env);

            auto const& USD = tok.USD;
            auto const& EUR = tok.EUR;

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
                BEAST_EXPECT(offer[sfTakerGets] == tok.remTakerGets);
                BEAST_EXPECT(offer[sfTakerPays] == tok.remTakerPays);
            }
        };

        test(initMPT);
        test(initIOU);
        test(initIOU1);
    }

    void
    testSelfFundedXRPEndpoint(bool consumeOffer, FeatureBitset features)
    {
        // Test that the deferred credit table is not bypassed for
        // XRPEndpointSteps. If the account in the first step is
        // sending XRP and that account also owns an offer that
        // receives XRP, it should not be possible for that step to
        // use the XRP received in the offer as part of the payment.
        testcase("Self funded XRPEndpoint");

        using namespace jtx;

        Env env(*this, features);

        auto const alice = Account("alice");
        auto const gw = Account("gw");

        env.fund(XRP(10'000), alice, gw);

        MPT const USD = MPTTester(
            {.env = env, .issuer = gw, .holders = {alice}, .maxAmt = 20});

        env(pay(gw, alice, USD(10)));
        env(offer(alice, XRP(50'000), USD(10)));

        // Consuming the offer changes the owner count, which could
        // also cause liquidity to decrease in the forward pass
        auto const toSend = consumeOffer ? USD(10) : USD(9);
        env(pay(alice, alice, toSend),
            path(~USD),
            sendmax(XRP(20'000)),
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

            env.fund(XRP(100'000), alice, bob, gw);

            MPT const USD = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .maxAmt = 20E+17});

            // scale by 17
            STAmount tinyAmt1{
                USD, 9'000'000'000'000'000ll, 0, false, STAmount::unchecked{}};
            STAmount tinyAmt3{
                USD, 9'000'000'000'000'003ll, 0, false, STAmount::unchecked{}};

            env(offer(gw, drops(9'000'000'000), tinyAmt3));

            env(pay(alice, bob, tinyAmt1),
                path(~USD),
                sendmax(drops(9'000'000'000)),
                txflags(tfNoRippleDirect));

            BEAST_EXPECT(!isOffer(env, gw, XRP(0), USD(0)));
        }
        {
            // Test forward
            Env env(*this, features);

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            env.fund(XRP(100'000), alice, bob, gw);

            MPT const USD = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .maxAmt = 20E+17});

            // scale by 17
            STAmount tinyAmt1{
                USD, 9'000'000'000'000'000ll, 0, false, STAmount::unchecked{}};
            STAmount tinyAmt3{
                USD, 9'000'000'000'000'003ll, 0, false, STAmount::unchecked{}};

            env(pay(gw, alice, tinyAmt1));

            env(offer(gw, tinyAmt3, drops(9'000'000'000)));
            env(pay(alice, bob, drops(9'000'000'000)),
                path(~XRP),
                sendmax(USD(static_cast<std::uint64_t>(1E+17))),
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

        env.fund(XRP(10'000), alice, bob, gw);

        // scale by 16
        MPT const USD = MPTTester(
            {.env = env,
             .issuer = gw,
             .holders = {alice, bob},
             .maxAmt = 100E+16});

        env(
            pay(gw,
                alice,
                // 12.55....
                STAmount{USD, std::uint64_t(1255555555555555ull), 2, false}));

        env(offer(
            gw,
            // 5.0...
            STAmount{USD, std::uint64_t(5000000000000000ull), 1, false},
            XRP(1000)));

        env(offer(
            gw,
            // .555...
            STAmount{USD, std::uint64_t(5555555555555555ull), 0, false},
            XRP(10)));

        env(offer(
            gw,
            // 4.44....
            STAmount{USD, std::uint64_t(4444444444444444ull), 1, false},
            XRP(.1)));

        env(offer(
            alice,
            // 17
            STAmount{USD, std::uint64_t(1700000000000000ull), 0, false},
            XRP(.001)));

        env(pay(alice, bob, XRP(10'000)),
            path(~XRP),
            sendmax(USD(static_cast<std::uint64_t>(100E+16))),
            txflags(tfPartialPayment | tfNoRippleDirect));
    }

    void
    testSelfPayLowQualityOffer(FeatureBitset features)
    {
        // The new payment code used to assert if an offer was made
        // for more XRP than the offering account held.  This unit
        // test reproduces that failing case.
        testcase("Self crossing low quality offer");

        using namespace jtx;

        Env env(*this, features);

        auto const ann = Account("ann");
        auto const gw = Account("gateway");

        auto const fee = env.current()->fees().base;
        env.fund(reserve(env, 2) + drops(9999640) + fee, ann);
        env.fund(reserve(env, 2) + fee * 4, gw);

        // scale by 5
        MPT const CTB = MPTTester(
            {.env = env,
             .issuer = gw,
             .holders = {ann},
             .transferFee = 2'000,  // 2%
             .maxAmt = 1'000'000});

        env(pay(gw, ann, CTB(285'600)));
        env.close();

        env(offer(ann, drops(365'611'702'030), CTB(571'300)));
        env.close();

        // This payment caused assert.
        env(pay(ann, ann, CTB(68'700)),
            sendmax(drops(20'000'000'000)),
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

        MPT USD;

        env(pay(alice, alice, USD(100)), path(~USD), ter(temBAD_PATH));
    }

    void
    testXRPPathLoop()
    {
        testcase("Circular XRP");

        using namespace jtx;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");

        {
            // Payment path starting with XRP
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);
                env.fund(XRP(10'000), alice, bob, gw);

                auto const USD = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, bob}});
                auto const EUR = issue2(
                    {.env = env,
                     .token = "EUR",
                     .issuer = gw,
                     .holders = {alice, bob}});
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
            };
            testHelper2TokensMix(test);
        }
        {
            // Payment path ending with XRP
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);
                env.fund(XRP(10'000), alice, bob, gw);
                auto const USD = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, bob}});
                auto const EUR = issue2(
                    {.env = env,
                     .token = "EUR",
                     .issuer = gw,
                     .holders = {alice, bob}});
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
            };
            testHelper2TokensMix(test);
        }
        {
            // Payment where loop is formed in the middle of the
            // path, not on an endpoint
            auto test = [&](auto&& issue1, auto&& issue2, auto&& issue3) {
                Env env(*this);
                env.fund(XRP(10'000), alice, bob, gw);
                env.close();
                auto const USD = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, bob}});
                auto const EUR = issue2(
                    {.env = env,
                     .token = "EUR",
                     .issuer = gw,
                     .holders = {alice, bob}});
                auto const JPY = issue3(
                    {.env = env,
                     .token = "JPY",
                     .issuer = gw,
                     .holders = {alice, bob}});
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
            };
            testHelper3TokensMix(test);
        }
    }

    void
    testMaxAndSelfPaymentEdgeCases(FeatureBitset features)
    {
        testcase("Max Flow/Self Payment Edge Cases");
        using namespace jtx;
        Account const gw("gw");
        Account const alice("alice");
        Account const carol("carol");
        Account const bob("bob");

        // Direct payment between holders.
        {
            Env env(*this);

            env.fund(XRP(1'000), gw, alice, carol);

            MPT const USD = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .maxAmt = 100});

            env(pay(gw, alice, USD(100)));

            env(pay(alice, carol, USD(100)));

            BEAST_EXPECT(env.balance(gw, USD) == USD(100));
            BEAST_EXPECT(env.balance(carol, USD) == USD(100));
            BEAST_EXPECT(env.balance(alice, USD) == USD(0));
        }

        // Direct payment between holders. Partial payment limited
        // by holder funds.
        {
            Env env(*this);

            env.fund(XRP(1'000), gw, alice, carol);

            MPT const USD = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .maxAmt = 100});

            env(pay(gw, alice, USD(80)));

            env(pay(alice, carol, USD(100)), txflags(tfPartialPayment));

            BEAST_EXPECT(env.balance(gw, USD) == USD(80));
            BEAST_EXPECT(env.balance(alice, USD) == USD(0));
            BEAST_EXPECT(env.balance(carol, USD) == USD(80));
        }

        // Direct payment between holders. Partial payment limited
        // by holder funds. OutstandingAmount is already at max
        // before the payment.
        {
            Env env(*this);

            env.fund(XRP(1'000), gw, alice, carol, bob);

            MPT const USD = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol, bob},
                 .maxAmt = 100});

            env(pay(gw, bob, USD(20)));
            env(pay(gw, alice, USD(80)));

            env(pay(alice, carol, USD(100)), txflags(tfPartialPayment));

            BEAST_EXPECT(env.balance(gw, USD) == USD(100));
            BEAST_EXPECT(env.balance(alice, USD) == USD(0));
            BEAST_EXPECT(env.balance(carol, USD) == USD(80));
        }

        // Cross-currency payment holder to holder. Holder owns an
        // offer. OutstandingAmount is already at max before the
        // payment.
        {
            Env env(*this);

            env.fund(XRP(1'000), gw, alice, carol, bob);

            MPT const USD = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .maxAmt = 100});

            env(pay(gw, alice, USD(100)));

            env(offer(alice, XRP(100), USD(100)));

            env(pay(bob, carol, USD(100)), sendmax(XRP(100)), path(~USD));

            BEAST_EXPECT(env.balance(gw, USD) == USD(100));
            BEAST_EXPECT(env.balance(alice, USD) == USD(0));
            BEAST_EXPECT(env.balance(carol, USD) == USD(100));
        }

        // Cross-currency payment holder to holder. Issuer owns an
        // offer. OutstandingAmount is already at max before the
        // payment. Since an issuer owns the offer, it issues more
        // tokens to another holder, and the payment fails.
        {
            Env env(*this);

            env.fund(XRP(1'000), gw, alice, carol);

            MPT const USD = MPTTester(
                {.env = env, .issuer = gw, .holders = {carol}, .maxAmt = 100});

            env(pay(gw, carol, USD(100)));

            env(offer(gw, XRP(100), USD(100)));

            env(pay(alice, carol, USD(100)),
                sendmax(XRP(100)),
                path(~USD),
                txflags(tfPartialPayment),
                ter(tecPATH_DRY));

            BEAST_EXPECT(env.balance(gw, USD) == USD(100));
            BEAST_EXPECT(env.balance(carol, USD) == USD(100));
        }

        // Cross-currency payment holder to holder. Issuer owns an
        // offer. OutstandingAmount is at 80USD before the payment.
        // Consequently, the issuer can issue 20USD more.
        {
            Env env(*this);

            env.fund(XRP(1'000), gw, alice, carol);

            MPT const USD = MPTTester(
                {.env = env, .issuer = gw, .holders = {carol}, .maxAmt = 100});

            env(pay(gw, carol, USD(80)));

            env(offer(gw, XRP(100), USD(100)));

            env(pay(alice, carol, USD(100)),
                sendmax(XRP(100)),
                path(~USD),
                txflags(tfPartialPayment));

            BEAST_EXPECT(env.balance(gw, USD) == USD(100));
            BEAST_EXPECT(env.balance(carol, USD) == USD(100));
        }

        // Cross-currency payment holder to holder. Holder owns an
        // offer. The offer buys more MPT's. The payment fails since
        // OutstandingAmount is already at max.
        {
            Env env(*this);

            env.fund(XRP(1'000), gw, alice);

            MPT const USD = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice}, .maxAmt = 100});

            env(pay(gw, alice, USD(100)));

            env(offer(alice, USD(100), XRP(100)));

            env(pay(gw, alice, XRP(100)),
                sendmax(USD(100)),
                path(~XRP),
                ter(tecPATH_PARTIAL));

            BEAST_EXPECT(env.balance(gw, USD) == USD(100));
            BEAST_EXPECT(env.balance(alice, USD) == USD(100));
        }

        // Cross-currency payment issuer to holder. Holder owns an
        // offer. The offer buys EUR, OutstandingAmount goes to max,
        // no overflow. The offer redeems USD to the issuer. While
        // OutstandingAmount is already at max, the payment succeeds
        // since USD is redeemed.
        {
            Env env(*this);

            env.fund(XRP(1'000), gw, alice, carol);

            MPT const USD = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .maxAmt = 100});
            MPT const EUR = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .maxAmt = 100});

            env(pay(gw, alice, USD(100)));

            env(offer(alice, EUR(100), USD(100)));

            env(pay(gw, carol, USD(100)), sendmax(EUR(100)), path(~USD));

            BEAST_EXPECT(env.balance(gw, USD) == USD(100));
            BEAST_EXPECT(env.balance(alice, USD) == USD(0));
            BEAST_EXPECT(env.balance(alice, EUR) == EUR(100));
            BEAST_EXPECT(env.balance(carol, USD) == USD(100));
        }

        // Cross-currency payment holder to holder. Offer is owned
        // by destination account. OutstandingAmount is not at max.
        {
            Env env(*this);

            env.fund(XRP(1'000), gw, alice, carol);

            MPT const USD = MPTTester(
                {.env = env, .issuer = gw, .holders = {carol}, .maxAmt = 120});

            env(pay(gw, carol, USD(100)));

            env(offer(carol, XRP(100), USD(100)));

            env(pay(alice, carol, USD(100)),
                path(~USD),
                sendmax(XRP(100)),
                txflags(tfPartialPayment));

            BEAST_EXPECT(env.balance(carol, USD) == USD(100));
        }

        // Cross-currency payment holder to holder. Offer is owned
        // by destination account. OutstandingAmount is already at
        // max.
        {
            Env env(*this);

            env.fund(XRP(1'000), gw, alice, carol);

            MPT const USD = MPTTester(
                {.env = env, .issuer = gw, .holders = {carol}, .maxAmt = 100});

            env(pay(gw, carol, USD(100)));

            env(offer(carol, XRP(100), USD(100)));

            env(pay(alice, carol, USD(100)),
                path(~USD),
                sendmax(XRP(100)),
                txflags(tfPartialPayment));

            BEAST_EXPECT(env.balance(carol, USD) == USD(100));
        }

        // Cross-currency payment holder to holder. Multiple offers
        // with different owners - some holders, some issuer.
        {
            Env env(*this);

            env.fund(XRP(1'000), gw, alice, carol, bob);

            MPT const USD = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol, bob},
                 .maxAmt = 1'000});
            MPT const EUR = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol, bob},
                 .maxAmt = 1'000});

            env(pay(gw, alice, USD(600)));
            env(pay(gw, carol, EUR(700)));

            env(offer(alice, EUR(100), USD(105)));
            env(offer(gw, EUR(100), USD(104)));
            env(offer(gw, EUR(100), USD(103)));
            env(offer(gw, EUR(100), USD(102)));
            env(offer(gw, EUR(100), USD(101)));
            env(offer(gw, EUR(100), USD(100)));

            env(pay(carol, bob, USD(2000)),
                sendmax(EUR(2000)),
                path(~USD),
                txflags(tfPartialPayment));

            BEAST_EXPECT(env.balance(gw, USD) == USD(1'000));
            BEAST_EXPECT(env.balance(alice, USD) == USD(495));
            BEAST_EXPECT(env.balance(bob, USD) == USD(505));
            BEAST_EXPECT(env.balance(carol, EUR) == EUR(210));
            // 100/101 is partially crossed (90/91) and 100/100 is
            // unfunded
            env.require(offers(gw, 0));
        }

        // Cross-currency payment holder to holder. Multiple offers
        // with different owners - some holders, some issuer. Source
        // and destination account is the same.
        {
            Env env(*this);

            env.fund(XRP(1'000), gw, alice, carol);

            MPT const USD = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol},
                 .maxAmt = 2'000});

            env(pay(gw, carol, USD(1'000)));
            env(pay(gw, alice, USD(600)));

            env(offer(gw, XRP(5), USD(11)));
            env(offer(gw, XRP(6), USD(13)));
            env(offer(carol, XRP(7), USD(15)));
            env(offer(carol, XRP(17), USD(35)));
            env(offer(carol, XRP(23), USD(47)));
            env(offer(alice, XRP(10), USD(19)));
            env(offer(alice, XRP(15), USD(28)));
            env(offer(alice, XRP(25), USD(46)));

            env(pay(carol, carol, USD(200)),
                sendmax(XRP(100)),
                txflags(tfPartialPayment));

            BEAST_EXPECT(env.balance(gw, USD) == USD(1'624));
            BEAST_EXPECT(env.balance(carol, USD) == USD(1'102));
            env.require(offers(carol, 0));
            env.require(offers(gw, 0));
            // 100 XRP's = 5+6+7+17+23+10+15+17(25-8)
            BEAST_EXPECT(isOffer(env, alice, XRP(8), USD(15)));
        }

        // Cross-currency payment holder to holder. Multiple offers
        // with different owners - some holders, some issuer.
        {
            Env env(*this);
            env.fund(XRP(1'000), gw, alice, carol, bob);

            MPT const USD = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, carol, bob},
                 .maxAmt = 30});

            env(pay(gw, alice, USD(12)));  // 12, 15, 20
            env(pay(gw, bob, USD(5)));     // 5, 5, 10

            env(offer(alice, XRP(10), USD(12)));
            env(offer(gw, XRP(10), USD(11)));
            env(offer(bob, XRP(10), USD(10)));

            env(pay(carol, bob, USD(30)),
                sendmax(XRP(30)),
                txflags(tfPartialPayment),
                path(~USD));
            BEAST_EXPECT(env.balance(gw, USD) == USD(28));
            BEAST_EXPECT(env.balance(alice, USD) == USD(0));
            // 12+11+5
            BEAST_EXPECT(env.balance(bob, USD) == USD(28));
        }

        // Cross-currency payment two steps. Second book step
        // issues, first book step redeems.
        {
            Account const dan{"dan"};
            Account const john{"john"};
            Account const ed{"ed"};
            Account const sam{"sam"};
            Account const bill{"bill"};

            struct TestData
            {
                int maxAmt;
                int sendMax;
                int dstTrustLimit;
                int dstExpectEUR;
                int outstUSD;
                int expEdBuyUSD;
                int expDanBuyUSD;
                int expBobSellUSD;
                int expGwXRP;  // whole XRP excluding the fees
                std::uint8_t expOffersGw;
                bool lastGwBuyUSD;
                std::uint8_t
                expOffersBob() const
                {
                    return expBobSellUSD == 0 ? 1 : 0;
                }
                std::uint8_t
                expOffersEd() const
                {
                    // partially crossed if < 100
                    return expEdBuyUSD < 100 ? 1 : 0;
                }
                std::uint8_t
                expOffersDan() const
                {
                    return expDanBuyUSD == 0 ? 1 : 0;
                }
            };

            auto test = [&](TestData const& d) {
                Env env(*this);
                env.fund(
                    XRP(1'000),
                    gw,
                    alice,
                    carol,
                    bob,
                    dan,
                    john,
                    ed,
                    sam,
                    bill);

                MPT const USD = MPTTester(
                    {.env = env,
                     .issuer = gw,
                     .holders = {alice, carol, bob},
                     .maxAmt = d.maxAmt});
                auto const EUR = gw["EUR"];

                env(pay(gw, alice, USD(100)));
                env(pay(gw, carol, USD(100)));
                env(pay(gw, bob, USD(100)));

                BEAST_EXPECT(env.balance(gw, USD) == USD(300));

                env(trust(john, EUR(100)));
                env(trust(dan, EUR(100)));
                env(trust(ed, EUR(100)));
                env(trust(bill, EUR(d.dstTrustLimit)));

                env(pay(gw, john, EUR(100)));
                env(pay(gw, dan, EUR(100)));
                env(pay(gw, ed, EUR(100)));
                env.close();

                // Sell USD
                env(offer(alice, XRP(100), USD(100)));
                env.close();  // close after each create to ensure
                              // the order
                env(offer(carol, XRP(100), USD(100)));
                env.close();
                if (!d.lastGwBuyUSD)
                {
                    env(offer(gw, XRP(100), USD(100)));
                    env.close();
                }
                env(offer(bob, XRP(100), USD(100)));
                env.close();
                if (d.lastGwBuyUSD)
                {
                    env(offer(gw, XRP(100), USD(100)));
                    env.close();
                }
                BEAST_EXPECT(expectOffers(env, alice, 1));
                BEAST_EXPECT(expectOffers(env, carol, 1));
                BEAST_EXPECT(expectOffers(env, gw, 1));
                BEAST_EXPECT(expectOffers(env, bob, 1));

                // Buy USD
                env(offer(john, USD(100), EUR(100)));
                env.close();
                env(offer(gw, USD(100), EUR(100)));
                env.close();
                env(offer(dan, USD(100), EUR(100)));
                env.close();
                env(offer(ed, USD(100), EUR(100)));
                env.close();
                BEAST_EXPECT(expectOffers(env, john, 1));
                BEAST_EXPECT(expectOffers(env, gw, 2));
                BEAST_EXPECT(expectOffers(env, dan, 1));
                BEAST_EXPECT(expectOffers(env, ed, 1));

                env(pay(sam, bill, EUR(400)),
                    sendmax(XRP(d.sendMax)),
                    path(~USD, ~EUR),
                    txflags(tfPartialPayment | tfNoRippleDirect));
                env.close();

                auto const baseFee = env.current()->fees().base.drops();
                BEAST_EXPECT(env.balance(bill, EUR) == EUR(d.dstExpectEUR));
                BEAST_EXPECT(env.balance(john, USD) == USD(100));
                BEAST_EXPECT(env.balance(dan, USD) == USD(d.expDanBuyUSD));
                BEAST_EXPECT(env.balance(ed, USD) == USD(d.expEdBuyUSD));
                BEAST_EXPECT(env.balance(gw, USD) == USD(d.outstUSD));
                BEAST_EXPECT(env.balance(alice, USD) == USD(0));
                BEAST_EXPECT(env.balance(carol, USD) == USD(0));
                BEAST_EXPECT(
                    env.balance(bob, USD) == USD(100 - d.expBobSellUSD));
                BEAST_EXPECT(
                    env.balance(gw) ==
                    XRPAmount{d.expGwXRP * DROPS_PER_XRP - baseFee * 9});
                BEAST_EXPECT(expectOffers(env, john, 0));
                BEAST_EXPECT(expectOffers(env, gw, d.expOffersGw));
                BEAST_EXPECT(expectOffers(env, dan, d.expOffersDan()));
                BEAST_EXPECT(expectOffers(env, ed, d.expOffersEd()));
                BEAST_EXPECT(expectOffers(env, alice, 0));
                BEAST_EXPECT(expectOffers(env, carol, 0));
                BEAST_EXPECT(expectOffers(env, bob, d.expOffersBob()));
            };

            // clang-format off
            std::vector<TestData> tests = {
                // Sell USD: alice, carol, bob, gw are consumed.
                // Buy USD: john, gw, dan, ed are consumed.
                // gw's sell USD is consumed because there is sufficient available balance (100USD).
                // but OutstandingAmount is 300USD because gw's sell offer is balanced out by
                // gw's buy offer.
                //*maxAmt sendMax limitEUR expectEUR outstUSD edBuy danBuy bobSell gwXRP offersGw lastGw
                {  400,   400,    400,     400,      300,     100,  100,   100,    1100, 0,       false},
                // Sell USD: alice, carol, bob, gw are consumed.
                // Buy USD: john, gw, dan, ed (partially) are consumed.
                // gw's sell USD is partially consumed because there is available balance (50USD).
                // OutstandingAmount is 250USD because gw's sell offer is partially balanced by
                // gw's buy offer. ed's offer is on the books because it's partially crossed.
                // gw's offer is removed from the order book because it's partially consumed and
                // the remaining offer is unfunded.
                //*maxAmt sendMax limitEUR expectEUR outstUSD edBuy danBuy bobSell gwXRP offersGw lastGw
                {  350,   400,    400,     350,      250,     50,   100,   100,    1050, 0,       false},
                // Sell USD: alice, carol, bob are consumed; gw's is unfunded
                //   since OutstandingAmount is initially at MaximumAmount.
                // Buy USD: john, gw, dan are consumed; ed's remains on the order
                //   book since 300USD is the sell limit.
                //*maxAmt sendMax limitEUR expectEUR outstUSD edBuy danBuy bobSell gwXRP offersGw lastGw
                {  300,   400,    400,     300,      200,     0,    100,   100,    1000, 0,       false},
                // Same as above. bill's trustline limit sets the output to 300USD.
                //*maxAmt sendMax limitEUR expectEUR outstUSD edBuy danBuy bobSell gwXRP offersGw lastGw
                {  300,   400,    300,     300,      200,     0,    100,   100,    1000, 0,       false},
                // Sell USD: alice, carol, bob are consumed; gw's removed from
                //   the order book since it's unfunded.
                // Buy USD: john, gw, dan are consumed; ed's  remains on the order
                //   book since 300USD is the limit.
                //*maxAmt sendMax limitEUR expectEUR outstUSD edBuy danBuy bobSell gwXRP offersGw lastGw
                {  300,   400,    300,     300,      200,     0,    100,   100,    1000, 0,       true},
                // Sell USD: alice, carol are consumed; gw's removed from
                //   the order book in rev pass since it's unfunded; bob's
                //   remains on the order book.
                // Buy USD: john, gw; ed's, dan's  remains on the order
                //   book since 300USD is the limit.
                //*maxAmt sendMax limitEUR expectEUR outstUSD edBuy danBuy bobSell gwXRP offersGw lastGw
                {  300,   200,    300,     200,      200,     0,    0,     0,      1000, 0,       false},
                // Same as three tests above since limited by buy 300USD (gw offer is unfunded)
                //*maxAmt sendMax limitEUR expectEUR outstUSD edBuy danBuy bobSell gwXRP offersGw lastGw
                {  300,   380,    400,     300,      200,     0,    100,   100,    1000, 0,       false},
            };
            // clang-format on
            for (auto const& t : tests)
                test(t);
        }

        // Cross-currency payment. BookStep issues, the first step
        // redeems.
        {
            Account const ed{"ed"};

            struct TestData
            {
                int maxAmt;
                int sendMax;
                int gwOffer;  // quality == 1
                int dstExpectXRP;
                int outstUSD;
                int expBobBuyUSD;
                int expGwXRP;  // whole XRP excluding the fees
                std::uint8_t expOffersGw;
                bool lastGwBuyUSD;
                std::uint8_t
                expOffersBob() const
                {
                    // partially crossed if < 100
                    return expBobBuyUSD < 100 ? 1 : 0;
                }
            };

            auto test = [&](TestData const& d) {
                Env env(*this);
                env.fund(XRP(1'000), gw, alice, carol, bob, ed);

                MPT const USD = MPTTester(
                    {.env = env,
                     .issuer = gw,
                     .holders = {alice},
                     .maxAmt = d.maxAmt});

                env(pay(gw, alice, USD(300)));
                env.close();

                env(offer(carol, USD(100), XRP(100)));
                env.close();
                if (!d.lastGwBuyUSD)
                {
                    env(offer(gw, USD(d.gwOffer), XRP(d.gwOffer)));
                    env.close();
                }
                env(offer(bob, USD(100), XRP(100)));
                env.close();
                if (d.lastGwBuyUSD)
                {
                    env(offer(gw, USD(d.gwOffer), XRP(d.gwOffer)));
                    env.close();
                }

                BEAST_EXPECT(expectOffers(env, carol, 1));
                BEAST_EXPECT(expectOffers(env, bob, 1));
                BEAST_EXPECT(expectOffers(env, gw, 1));
                BEAST_EXPECT(env.balance(gw, USD) == USD(300));

                env(pay(alice, ed, XRP(300)),
                    sendmax(USD(d.sendMax)),
                    path(~XRP),
                    txflags(tfPartialPayment | tfNoRippleDirect));
                env.close();

                auto const baseFee = env.current()->fees().base.drops();
                BEAST_EXPECT(env.balance(alice, USD) == USD(300 - d.sendMax));
                BEAST_EXPECT(env.balance(carol, USD) == USD(100));
                BEAST_EXPECT(env.balance(bob, USD) == USD(d.expBobBuyUSD));
                BEAST_EXPECT(env.balance(ed) == XRP(d.dstExpectXRP));
                BEAST_EXPECT(env.balance(gw, USD) == USD(d.outstUSD));
                BEAST_EXPECT(
                    env.balance(gw) ==
                    XRPAmount{d.expGwXRP * DROPS_PER_XRP - baseFee * 3});
                BEAST_EXPECT(expectOffers(env, carol, 0));
                BEAST_EXPECT(expectOffers(env, bob, d.expOffersBob()));
                BEAST_EXPECT(expectOffers(env, gw, d.expOffersGw));
            };

            // clang-format off
            std::vector<TestData> tests = {
                // Buy USD: carol, gw, bob are consumed.
                // Gw gets 300USD from alice; carol and bob buy 200USD,
                // therefore OutstandingAmount is 200.
                //*maxAmt sendMax gwOffer dstXRP outstUSD bobBuy gwXRP offersGw lastGw
                { 300,    300,    100,    1300,  200,     100,   900,  0,       false},
                // Same as above. Gw offer location in the order book doesn't matter
                //*maxAmt sendMax gwOffer dstXRP outstUSD bobBuy gwXRP offersGw lastGw
                { 300,    300,    100,    1300,  200,     100,   900,  0,       true},
                // Buy USD: carol, gw are consumed. bob's offer remains on the order book.
                // Gw gets 300USD from alice; carol buys 100USD,
                // therefore OutstandingAmount is 100.
                //*maxAmt sendMax gwOffer dstXRP outstUSD bobBuy gwXRP offersGw lastGw
                { 300,    300,    200,    1300,  100,     0,     800,  0,       false},
                // Buy USD: carol, bob are consumed; gw's is partially consumed (100/100) since it's last.
                // Gw gets 300USD from alice; carol and bob buy 200USD,
                // therefore OutstandingAmount is 200.
                //*maxAmt sendMax gwOffer dstXRP outstUSD bobBuy gwXRP offersGw lastGw
                { 300,    300,    200,    1300,  200,     100,   900,  1,       true},
                // Buy USD: carol, bob are consumed; gw's is partially consumed (50/50) since it's last
                // and sendMax limits the output.
                // Gw gets 250USD from alice; carol and bob buy 200USD, alice has 50USD left,
                // therefore OutstandingAmount is 200.
                //*maxAmt sendMax gwOffer dstXRP outstUSD bobBuy gwXRP offersGw lastGw
                { 300,    250,    200,    1250,  250,     100,   950,  1,       true},
            };
            // clang-format on
            for (auto const& t : tests)
                test(t);
        }

        // Cross-currency payment. BookStep redeems, the last step
        // issues.
        {
            Account const ed{"ed"};

            struct TestData
            {
                int maxAmt;
                int sendMax;
                int initDst;
                int gwOffer;  // quality == 1
                int dstExpectUSD;
                int outstUSD;
                int expAliceXRP;  // whole XRP excluding the fees
                int expBobSellUSD;
                int expGwXRP;
                std::uint8_t expOffersGw;
                bool lastGwBuyUSD;
                std::uint8_t
                expOffersBob() const
                {
                    return expBobSellUSD > 0 && expBobSellUSD < 100 ? 1 : 0;
                }
            };

            auto test = [&](TestData const& d) {
                Env env(*this);
                env.fund(XRP(1'000), gw, alice, carol, bob, ed);

                MPT const USD = MPTTester(
                    {.env = env,
                     .issuer = gw,
                     .holders = {carol, bob, ed},
                     .maxAmt = d.maxAmt});

                if (d.initDst != 0)
                    env(pay(gw, ed, USD(d.initDst)));
                env(pay(gw, carol, USD(100)));
                env(pay(gw, bob, USD(100)));
                env.close();

                env(offer(carol, XRP(100), USD(100)));
                env.close();
                if (!d.lastGwBuyUSD)
                {
                    env(offer(gw, XRP(d.gwOffer), USD(d.gwOffer)));
                    env.close();
                }
                env(offer(bob, XRP(100), USD(100)));
                env.close();
                if (d.lastGwBuyUSD)
                {
                    env(offer(gw, XRP(d.gwOffer), USD(d.gwOffer)));
                    env.close();
                }

                BEAST_EXPECT(expectOffers(env, carol, 1));
                BEAST_EXPECT(expectOffers(env, bob, 1));
                BEAST_EXPECT(expectOffers(env, gw, 1));
                BEAST_EXPECT(env.balance(gw, USD) == USD(200 + d.initDst));

                env(pay(alice, ed, USD(300)),
                    sendmax(XRP(d.sendMax)),
                    path(~USD),
                    txflags(tfPartialPayment | tfNoRippleDirect));
                env.close();

                auto const baseFee = env.current()->fees().base.drops();
                BEAST_EXPECT(
                    env.balance(alice) ==
                    XRPAmount{d.expAliceXRP * DROPS_PER_XRP - baseFee});
                BEAST_EXPECT(env.balance(carol, USD) == USD(0));
                BEAST_EXPECT(
                    env.balance(bob, USD) == USD(100 - d.expBobSellUSD));
                BEAST_EXPECT(env.balance(ed, USD) == USD(d.dstExpectUSD));
                BEAST_EXPECT(env.balance(gw, USD) == USD(d.outstUSD));
                BEAST_EXPECT(
                    env.balance(gw) ==
                    XRPAmount{
                        d.expGwXRP * DROPS_PER_XRP -
                        baseFee * (4 + (d.initDst != 0 ? 1 : 0))});
                BEAST_EXPECT(expectOffers(env, carol, 0));
                BEAST_EXPECT(expectOffers(env, bob, d.expOffersBob()));
                BEAST_EXPECT(expectOffers(env, gw, d.expOffersGw));
            };

            // clang-format off
            std::vector<TestData> tests = {
                // Sell USD: carol, gw, bob are consumed.
                // ed buys 300USD from carol, gw, bob therefore OutstandingAmount is 300.
                //*maxAmt sendMax initDst gwOffer dstUSD outstUSD aliceXRP bobSell gwXRP offersGw lastGw
                { 300,    300,    0,      100,    300,   300,     700,     100,    1100, 0,       false},
                // Same as above. Gw offer location in the order book doesn't matter
                //*maxAmt sendMax initDst gwOffer dstUSD outstUSD aliceXRP bobSell gwXRP offersGw lastGw
                { 300,    300,    0,      100,    300,   300,     700,     100,    1100, 0,       true},
                // Sell USD: carol, bob are consumed, gw is partially consumed.
                // ed buys 200 from carol and bob and 50 from gw because gw can only issue 50
                // (300(max) - 200(carol+bob) - 50(ed)). ed buys 250 from carol, gw, bob and has 50 initially,
                // therefore OutstandingAmount is 300.
                // gw's offer is removed from the order book because it's partially consumed and the remaining
                // offer is unfunded.
                //*maxAmt sendMax initDst gwOffer dstUSD outstUSD aliceXRP bobSell gwXRP offersGw lastGw
                { 300,    300,    50,     100,    300,   300,     750,     100,    1050, 0,       false},
                // Same as above. Gw offer location in the order book doesn't matter.
                //*maxAmt sendMax initDst gwOffer dstUSD outstUSD aliceXRP bobSell gwXRP offersGw lastGw
                { 300,    300,    50,     100,    300,   300,     750,     100,    1050, 0,       true},
                // Same as above. Gw offer size doesn't matter.
                //*maxAmt sendMax initDst gwOffer dstUSD outstUSD aliceXRP bobSell gwXRP offersGw lastGw
                { 300,    300,    50,     200,    300,   300,     750,     100,    1050, 0,       true},
                // Sell USD: carol, gw are consumed, bob is partially consumed.
                // ed buys 200 from carol and gw and 50 form bob because of sendMax limit. bob keeps 50,
                // therefore OutstandingAmount is 300.
                //*maxAmt sendMax initDst gwOffer dstUSD outstUSD aliceXRP bobSell gwXRP offersGw lastGw
                { 300,    250,    0,      100,    250,   300,     750,     50,     1100, 0,       false},
                // Sell USD: carol, bob are consumed, gw is partially consumed because of sendMax limit.
                // ed buys 200 from carol and bob and 50 from gw. Therefore, OutstandingAmount is 250.
                // gw's offer remains on the order book because it's partially consumed and has more funds.
                //*maxAmt sendMax initDst gwOffer dstUSD outstUSD aliceXRP bobSell gwXRP offersGw lastGw
                { 300,    250,    0,      100,    250,   250,     750,     100,    1050, 1,       true},
                // Sell USD: carol, bob are consumed, gw is partially consumed because of sendMax limit, also
                // there is only 50 available to issue. ed buys 200 from carol and bob and 50 from gw, plus
                // he has initially 50, therefore OutstandingAmount is 300.
                //*maxAmt sendMax initDst gwOffer dstUSD outstUSD aliceXRP bobSell gwXRP offersGw lastGw
                { 300,    250,    50,     100,    300,   300,     750,     100,    1050, 0,       true},
                // Sell USD: carol, bob are consumed, gw is not consumed because there is not available funds
                // to issue. ed buys 200 from carol and bob and, plus he has initially 100,
                // therefore OutstandingAmount is 300. gw offer is removed because it's unfunded.
                //*maxAmt sendMax initDst gwOffer dstUSD outstUSD aliceXRP bobSell gwXRP offersGw lastGw
                { 300,    250,    100,    100,    300,   300,     800,     100,    1000, 0,       true},
            };
            // clang-format on
            for (auto const& t : tests)
                test(t);
        }

        // Cross-currency payment with BookStep as the first step.
        // BookStep limits the buy amount.
        {
            auto test = [&](int sendMax,
                            std::uint16_t dstXRP,
                            std::uint8_t expGwOffers) {
                Env env(*this);
                env.fund(XRP(1'000), gw, alice, carol);

                MPT const USD =
                    MPTTester({.env = env, .issuer = gw, .maxAmt = 300});

                env(offer(carol, USD(400), XRP(400)));
                env(offer(gw, USD(100), XRP(100)));
                BEAST_EXPECT(expectOffers(env, carol, 1));
                BEAST_EXPECT(expectOffers(env, gw, 1));

                env(pay(gw, alice, XRP(500)),
                    sendmax(USD(sendMax)),
                    path(~XRP),
                    txflags(tfPartialPayment | tfNoRippleDirect));

                BEAST_EXPECT(env.balance(alice) == XRP(dstXRP));
                BEAST_EXPECT(env.balance(gw, USD) == USD(300));
                BEAST_EXPECT(env.balance(carol, USD) == USD(300));
                BEAST_EXPECT(expectOffers(env, carol, 0));
                BEAST_EXPECT(expectOffers(env, gw, expGwOffers));
            };
            // carol's offer is partially consumed - 300USD/300XRP
            // because available amount to issue is 300USD. gw's
            // offer is fully consumed because it doesn't change
            // OutstandingAmount. Both offers are removed from the
            // order book - carol's offer is unfunded and gw's offer
            // is fully consumed.
            test(500, 1'400, 0);
            // carol's offer is partially consumed - 300USD/300XRP
            // because available amount to issue is 300USD. gw's
            // offer is partially consumed because of sendMax limit.
            // carol's offer is removed from the order book because
            // it's unfunded. gw's offer remains on the order book
            // because it's partially consumed and gw has more
            // funds.
            test(350, 1'350, 1);
        }
    }

    void
    testWithFeats(FeatureBitset features)
    {
        using namespace jtx;

        testMaxAndSelfPaymentEdgeCases(features);
        testFalseDry(features);
        testDirectStep(features);
        testBookStep(features);
        testTransferRate(features);
        testSelfPayment1(features);
        testSelfPayment2(features);
        testSelfFundedXRPEndpoint(false, features);
        testSelfFundedXRPEndpoint(true, features);
        testUnfundedOffer(features);
        testReexecuteDirectStep(features);
        testSelfPayLowQualityOffer(features);
    }

    void
    run() override
    {
        using namespace jtx;
        auto const sa = supported_amendments();
        testLimitQuality();
        testXRPPathLoop();
        testWithFeats(sa);
        testEmptyStrand(sa);
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(FlowMPT, app, ripple, 2);

}  // namespace test
}  // namespace ripple
