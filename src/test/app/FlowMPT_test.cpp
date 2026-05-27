#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/PathSet.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/mpt.h>
#include <test/jtx/owners.h>
#include <test/jtx/paths.h>
#include <test/jtx/pay.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/PaymentSandbox.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/OfferHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/paths/Flow.h>
#include <xrpl/tx/paths/detail/Steps.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace xrpl::test {

struct FlowMPT_test : public beast::unit_test::Suite
{
    using Accounts = std::vector<jtx::Account>;

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
            MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}});
            env(pay(gw, alice, usd(100)));
            env(pay(alice, bob, usd(10)), Paths(usd));
            env.require(Balance(bob, usd(10)));
        }
        {
            // Partial payments
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, gw);
            MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}});
            env(pay(gw, alice, usd(100)));
            env(pay(alice, bob, usd(110)), Paths(usd), Ter(tecPATH_PARTIAL));
            env.require(Balance(bob, usd(0)));
            env(pay(alice, bob, usd(110)), Paths(usd), Txflags(tfPartialPayment));
            env.require(Balance(bob, usd(100)));
        }

        {
            // Limit quality
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this, features);

                env.fund(XRP(10'000), gw, alice, bob, carol);
                env.close();

                auto const usd =
                    issue1({.env = env, .token = "USD", .issuer = gw, .holders = {alice, carol}});
                auto const eur =
                    issue2({.env = env, .token = "EUR", .issuer = gw, .holders = {bob}});

                env(pay(gw, alice, usd(100)));
                env(pay(gw, bob, eur(100)));

                env(offer(alice, eur(4), usd(4)));
                env.close();

                env(pay(bob, carol, usd(5)),
                    Sendmax(eur(4)),
                    Txflags(tfLimitQuality | tfPartialPayment),
                    Ter(tecPATH_DRY));
                env.require(Balance(carol, usd(0)));

                env(pay(bob, carol, usd(5)), Sendmax(eur(4)), Txflags(tfPartialPayment));
                env.require(Balance(carol, usd(4)));
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
                env.close();

                auto const usd = issue1(
                    {.env = env, .token = "USD", .issuer = gw, .holders = {alice, bob, carol}});
                auto const btc = issue2(
                    {.env = env, .token = "BTC", .issuer = gw, .holders = {alice, bob, carol}});

                env(pay(gw, alice, btc(50)));
                env(pay(gw, bob, usd(50)));

                env(offer(bob, btc(50), usd(50)));

                env(pay(alice, carol, usd(50)), Path(~usd), Sendmax(btc(50)));

                env.require(Balance(alice, btc(0)));
                env.require(Balance(bob, btc(50)));
                env.require(Balance(bob, usd(0)));
                env.require(Balance(carol, usd(50)));
                BEAST_EXPECT(!isOffer(env, bob, btc(50), usd(50)));
            };
            testHelper2TokensMix(test);
        }
        {
            // simple [MPT|IOU]/XRP XRP/[IOU|MPT] offer
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this, features);

                env.fund(XRP(10'000), alice, bob, carol, gw);
                env.close();

                auto const usd = issue1(
                    {.env = env, .token = "USD", .issuer = gw, .holders = {alice, bob, carol}});
                auto const btc = issue2(
                    {.env = env, .token = "BTC", .issuer = gw, .holders = {alice, bob, carol}});

                env(pay(gw, alice, btc(50)));
                env(pay(gw, bob, usd(50)));

                env(offer(bob, btc(50), XRP(50)));
                env(offer(bob, XRP(50), usd(50)));

                env(pay(alice, carol, usd(50)), Path(~XRP, ~usd), Sendmax(btc(50)));

                env.require(Balance(alice, btc(0)));
                env.require(Balance(bob, btc(50)));
                env.require(Balance(bob, usd(0)));
                env.require(Balance(carol, usd(50)));
                BEAST_EXPECT(!isOffer(env, bob, XRP(50), usd(50)));
                BEAST_EXPECT(!isOffer(env, bob, btc(50), XRP(50)));
            };
            testHelper2TokensMix(test);
        }
        {
            // simple XRP -> USD through offer and sendmax
            Env env(*this, features);

            env.fund(XRP(10'000), alice, bob, carol, gw);
            env.close();

            MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice, bob, carol}});
            MPT const btc = MPTTester({.env = env, .issuer = gw, .holders = {alice, bob, carol}});

            env(pay(gw, bob, usd(50)));

            env(offer(bob, XRP(50), usd(50)));

            env(pay(alice, carol, usd(50)), Path(~usd), Sendmax(XRP(50)));

            // fee: MPTokenAuthorize * 2(EUR, USD) + pay
            env.require(Balance(alice, XRP(10'000 - 50) - txFee(env, 3)));
            // fee: MPTokenAuthorize * 2(EUR, USD) + offer
            env.require(Balance(bob, XRP(10'000 + 50) - txFee(env, 3)));
            env.require(Balance(bob, usd(0)));
            env.require(Balance(carol, usd(50)));
            BEAST_EXPECT(!isOffer(env, bob, XRP(50), usd(50)));
        }
        {
            // simple USD -> XRP through offer and sendmax
            Env env(*this, features);

            env.fund(XRP(10'000), alice, bob, carol, gw);
            env.close();

            MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice, bob, carol}});
            MPT const btc = MPTTester({.env = env, .issuer = gw, .holders = {alice, bob, carol}});

            env(pay(gw, alice, usd(50)));

            env(offer(bob, usd(50), XRP(50)));

            env(pay(alice, carol, XRP(50)), Path(~XRP), Sendmax(usd(50)));

            env.require(Balance(alice, usd(0)));
            env.require(Balance(bob, XRP(10'000 - 50) - txFee(env, 3)));
            env.require(Balance(bob, usd(50)));
            env.require(Balance(carol, XRP(10'000 + 50) - txFee(env, 2)));
            BEAST_EXPECT(!isOffer(env, bob, usd(50), XRP(50)));
        }
        {
            // test unfunded offers are removed when payment succeeds
            auto test = [&](auto&& issue1, auto&& issue2, auto&& issue3) {
                Env env(*this, features);

                env.fund(XRP(10'000), alice, bob, carol, gw);
                env.close();

                auto const usd = issue1(
                    {.env = env, .token = "USD", .issuer = gw, .holders = {alice, bob, carol}});
                auto const btc = issue2(
                    {.env = env, .token = "BTC", .issuer = gw, .holders = {alice, bob, carol}});
                auto const eur = issue3(
                    {.env = env, .token = "EUR", .issuer = gw, .holders = {alice, bob, carol}});

                env(pay(gw, alice, btc(60)));
                env(pay(gw, bob, usd(50)));
                env(pay(gw, bob, eur(50)));

                env(offer(bob, btc(50), usd(50)));
                env(offer(bob, btc(40), eur(50)));
                env(offer(bob, eur(50), usd(50)));

                // unfund offer
                env(pay(bob, gw, eur(50)));
                env.require(Balance(bob, eur(0)));
                BEAST_EXPECT(isOffer(env, bob, btc(50), usd(50)));
                BEAST_EXPECT(isOffer(env, bob, btc(40), eur(50)));
                BEAST_EXPECT(isOffer(env, bob, eur(50), usd(50)));

                env(pay(alice, carol, usd(50)), Path(~usd), Path(~eur, ~usd), Sendmax(btc(60)));

                env.require(Balance(alice, btc(10)));
                env.require(Balance(bob, btc(50)));
                env.require(Balance(bob, usd(0)));
                env.require(Balance(bob, eur(0)));
                env.require(Balance(carol, usd(50)));
                // used in the payment
                BEAST_EXPECT(!isOffer(env, bob, btc(50), usd(50)));
                // found unfunded
                BEAST_EXPECT(!isOffer(env, bob, btc(40), eur(50)));
                // unfunded, but should not yet be found unfunded
                BEAST_EXPECT(isOffer(env, bob, eur(50), usd(50)));
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
                env.close();

                auto const usd = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 100'000});
                auto const btc = issue2(
                    {.env = env,
                     .token = "BTC",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 100'000});
                auto const eur = issue3(
                    {.env = env,
                     .token = "EUR",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 100'000});

                env(pay(gw, alice, btc(60)));
                env(pay(gw, bob, usd(6'000)));
                env(pay(gw, bob, eur(5'000)));
                env(pay(gw, carol, eur(100)));

                env(offer(bob, btc(50), usd(5'000)));
                env(offer(bob, btc(60), eur(5'000)));
                env(offer(carol, btc(1'000), eur(100)));
                env(offer(bob, eur(5'000), usd(5'000)));

                // unfund offer
                env(pay(bob, gw, eur(5'000)));
                BEAST_EXPECT(isOffer(env, bob, btc(50), usd(5'000)));
                BEAST_EXPECT(isOffer(env, bob, btc(60), eur(5'000)));
                BEAST_EXPECT(isOffer(env, carol, btc(1'000), eur(100)));

                auto flowJournal = env.app().getLogs().journal("Flow");
                auto const flowResult = [&] {
                    STAmount const deliver(usd(5'100));
                    STAmount smax(btc(61));
                    PaymentSandbox sb(env.current().get(), TapNone);
                    STPathSet paths;
                    auto ipe = [](Asset const& asset) {
                        return STPathElement(
                            STPathElement::TypeAsset | STPathElement::TypeIssuer,
                            xrpAccount(),
                            asset,
                            asset.getIssuer());
                    };
                    {
                        // BTC -> USD
                        STPath const p1({ipe(usd)});
                        paths.pushBack(p1);
                        // BTC -> EUR -> USD
                        STPath const p2({ipe(eur), ipe(usd)});
                        paths.pushBack(p2);
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
                        OfferCrossing::No,
                        std::nullopt,
                        smax,
                        std::nullopt,
                        flowJournal);
                }();

                BEAST_EXPECT(flowResult.removableOffers.size() == 1);
                env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal j) {
                    if (flowResult.removableOffers.empty())
                        return false;
                    Sandbox sb(&view, TapNone);
                    for (auto const& o : flowResult.removableOffers)
                    {
                        if (auto ok = sb.peek(keylet::offer(o)))
                            offerDelete(sb, ok, flowJournal);
                    }
                    sb.apply(view);
                    return true;
                });

                // used in payment, but since payment failed should
                // be untouched
                BEAST_EXPECT(isOffer(env, bob, btc(50), usd(5'000)));
                BEAST_EXPECT(isOffer(env, carol, btc(1'000), eur(100)));
                // found unfunded
                BEAST_EXPECT(!isOffer(env, bob, btc(60), eur(5'000)));
            };
            testHelper3TokensMix(test);
        }
        {
            // Do not produce more in the forward pass than the
            // reverse pass. This test uses a path whose reverse
            // pass will compute a 0.5 USD input required for a 1
            // EUR output. It sets a sendmax of 0.4 USD, so the
            // payment engine will need to do a forward pass.
            // Without limits, the 0.4 USD would produce 1000 EUR in
            // the forward pass. This test checks that the payment
            // produces 1 EUR, as expected.
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this, features);
                env.fund(XRP(10'000), alice, bob, carol, gw);

                auto const usd = issue1(
                    {.env = env, .token = "USD", .issuer = gw, .holders = {alice, bob, carol}});
                auto const eur = issue1(
                    {.env = env, .token = "EUR", .issuer = gw, .holders = {alice, bob, carol}});

                env(pay(gw, alice, usd(1'000)));
                env(pay(gw, bob, eur(1'000)));

                Keylet const bobUsdOffer = keylet::offer(bob, env.seq(bob));
                env(offer(bob, usd(10), drops(2)), Txflags(tfPassive));
                env(offer(bob, drops(1), eur(1'000)), Txflags(tfPassive));

                bool const reducedOffersV2 = features[fixReducedOffersV2];

                // With reducedOffersV2, it is not allowed to accept
                // less than USD(0.5) of bob's USD offer.  If we
                // provide 1 drop for less than USD(0.5), then the
                // remaining fractional offer would block the order
                // book.
                TER const expectedTER = reducedOffersV2 ? TER(tecPATH_DRY) : TER(tesSUCCESS);
                env(pay(alice, carol, eur(1)),
                    Path(~XRP, ~eur),
                    Sendmax(usd(4)),
                    Txflags(tfNoRippleDirect | tfPartialPayment),
                    Ter(expectedTER));

                if (!reducedOffersV2)
                {
                    env.require(Balance(carol, eur(1)));
                    env.require(Balance(bob, usd(4)));
                    env.require(Balance(bob, eur(999)));

                    // Show that bob's USD offer is now a blocker.
                    std::shared_ptr<SLE const> const usdOffer = env.le(bobUsdOffer);
                    if (BEAST_EXPECT(usdOffer))
                    {
                        std::uint64_t const bookRate = [&usdOffer]() {
                            // Extract the least significant 64
                            // bits from the book page.  That's
                            // where the quality is stored.
                            std::string bookDirStr = to_string(usdOffer->at(sfBookDirectory));
                            bookDirStr.erase(0, 48);
                            return std::stoull(bookDirStr, nullptr, 16);
                        }();
                        std::uint64_t const actualRate =
                            getRate(usdOffer->at(sfTakerGets), usdOffer->at(sfTakerPays));

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

            MPT const usd = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .transferFee = 25'000,
                 .maxAmt = 1'000});

            env(pay(gw, alice, usd(50)));
            env.require(Balance(alice, usd(50)));
            env(pay(alice, bob, usd(40)), Sendmax(usd(50)));
            env.require(Balance(bob, usd(40)), Balance(alice, usd(0)));
        }
        {
            // transfer rate is not charged when issuer is src or
            // dst
            Env env(*this, features);

            env.fund(XRP(10'000), alice, bob, carol, gw);

            MPT const usd = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .transferFee = 25'000,
                 .maxAmt = 1'000});

            env(pay(gw, alice, usd(50)));
            env.require(Balance(alice, usd(50)));
            env(pay(alice, gw, usd(40)), Sendmax(usd(40)));
            env.require(Balance(alice, usd(10)));
        }
        {
            // transfer fee on an offer
            Env env(*this, features);

            env.fund(XRP(10'000), alice, bob, carol, gw);

            MPT const usd = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob, carol},
                 .transferFee = 25'000,
                 .maxAmt = 10'000});

            // scale by 1
            env(pay(gw, bob, usd(650)));

            env(offer(bob, XRP(50), usd(500)));

            env(pay(alice, carol, usd(500)),
                Path(~usd),
                Sendmax(XRP(50)),
                Txflags(tfPartialPayment));

            // bob pays 25% on 500USD -> 100USD; 400USD goes to carol
            env.require(
                Balance(alice, XRP(10'000 - 50) - txFee(env, 2)),
                Balance(bob, usd(150)),
                Balance(carol, usd(400)));
        }
        {
            // Transfer fee two consecutive offers
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this, features);

                env.fund(XRP(10'000), alice, bob, carol, gw);
                env.close();

                auto const usd = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 1'000,
                     .transferFee = 25'000});
                auto const eur = issue2(
                    {.env = env,
                     .token = "EUR",
                     .issuer = gw,
                     .holders = {alice, bob, carol},
                     .limit = 1'000,
                     .transferFee = 25'000});

                env(pay(gw, bob, usd(50)));
                env(pay(gw, bob, eur(50)));

                env(offer(bob, XRP(50), usd(50)));
                env(offer(bob, usd(50), eur(50)));

                env(pay(alice, carol, eur(40)),
                    Path(~usd, ~eur),
                    Sendmax(XRP(40)),
                    Txflags(tfPartialPayment));
                // +1 for fset in helperIssueIOU
                using tEUR = std::decay_t<decltype(eur)>;
                auto const fee = txFee(env, 3);
                // bob pays 25% on 40USD (40 since sendmax is 40XRP)
                // 8USD goes to gw and 32USD goes back to bob ->
                // bob's USD balance is 42USD. USD/EUR offer is 32USD/32EUR.
                // bob pays 25% on 32EUR -> 7EUR if MPT, 6.4EUR if IOU,
                // therefore carl gets 25EUR if MPT, 25.6EUR if IOU.
                auto const carolEUR = [&]() {
                    if constexpr (std::is_same_v<tEUR, IOU>)
                    {
                        return eur(25.6);
                    }
                    else
                    {
                        return eur(25);
                    }
                }();
                env.require(
                    Balance(alice, XRP(10'000 - 40) - fee),
                    Balance(bob, usd(42)),
                    Balance(bob, eur(18)),
                    Balance(carol, carolEUR));
            };
            testHelper2TokensMix(test);
        }
        {
            // Offer where the owner is also the issuer, sender pays
            // fee
            Env env(*this, features);

            env.fund(XRP(10'000), alice, bob, gw);

            MPT const usd = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .transferFee = 25'000,
                 .maxAmt = 1'000});

            env(offer(gw, XRP(100), usd(100)));
            env(pay(alice, bob, usd(100)), Sendmax(XRP(100)), Txflags(tfPartialPayment));
            env.require(Balance(alice, XRP(10'000 - 100) - txFee(env, 2)), Balance(bob, usd(80)));
        }
        {
            // Offer where the owner is also the issuer, sender pays
            // fee
            Env env(*this, features);

            env.fund(XRP(10'000), alice, bob, gw);

            MPT const usd = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .transferFee = 25'000,
                 .maxAmt = 1'000});

            env(offer(gw, XRP(125), usd(125)));
            env(pay(alice, bob, usd(100)), Sendmax(XRP(200)));
            env.require(Balance(alice, XRP(10'000 - 125) - txFee(env, 2)), Balance(bob, usd(100)));
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
            env.close();

            auto const usd =
                issue1({.env = env, .token = "USD", .issuer = gw, .holders = {alice, carol, bob}});
            auto const eur =
                issue2({.env = env, .token = "EUR", .issuer = gw, .holders = {alice, carol, bob}});

            env(pay(gw, alice, eur(50)));
            env(pay(gw, bob, usd(50)));

            // Bob has _just_ slightly less than 50 xrp available
            // If his owner count changes, he will have more liquidity.
            // This is one error case to test (when Flow is used).
            // Computing the incoming xrp to the XRP/USD offer will
            // require two recursive calls to the EUR/XRP offer. The
            // second call will return tecPATH_DRY, but the entire path
            // should not be marked as dry. This is the second error
            // case to test (when flowV1 is used).
            env(offer(bob, eur(50), XRP(50)));
            env(offer(bob, XRP(50), usd(50)));

            env(pay(alice, carol, usd(1'000'000)),
                Path(~XRP, ~usd),
                Sendmax(eur(500)),
                Txflags(tfNoRippleDirect | tfPartialPayment));

            auto const carolUSD = env.balance(carol, usd).value();
            BEAST_EXPECT(carolUSD > usd(0) && carolUSD < usd(50));
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

            MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice, bob, carol}});

            env(pay(gw, bob, usd(100)));
            env(offer(bob, XRP(50), usd(50)));
            env(offer(bob, XRP(100), usd(50)));

            env(pay(alice, carol, usd(100)),
                Path(~usd),
                Sendmax(XRP(100)),
                Txflags(tfNoRippleDirect | tfPartialPayment | tfLimitQuality));

            env.require(Balance(carol, usd(50)));
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
        forEachItem(*env.current(), account, [&result](std::shared_ptr<SLE const> const& sle) {
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

        auto test = [&](auto&& issue1, auto&& issue2) {
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

            auto const usd = issue1(
                {.env = env, .token = "USD", .issuer = gw1, .holders = {alice}, .limit = 20'000});
            auto const eur = issue2(
                {.env = env, .token = "EUR", .issuer = gw2, .holders = {alice}, .limit = 20'000});

            env(pay(gw1, alice, usd(10)));
            env(pay(gw2, alice, eur(10'000)));
            env.close();

            env(offer(alice, usd(5'000), eur(6'000)));
            env.close();

            env.require(Owners(alice, 3));
            env.require(Balance(alice, usd(10)));
            env.require(Balance(alice, eur(10'000)));

            auto aliceOffers = offersOnAccount(env, alice);
            BEAST_EXPECT(aliceOffers.size() == 1);
            for (auto const& offerPtr : aliceOffers)
            {
                auto const offer = *offerPtr;
                BEAST_EXPECT(offer[sfLedgerEntryType] == ltOFFER);
                BEAST_EXPECT(offer[sfTakerGets] == eur(6'000));
                BEAST_EXPECT(offer[sfTakerPays] == usd(5'000));
            }

            env(pay(alice, alice, eur(6'000)), Sendmax(usd(5'000)), Txflags(tfPartialPayment));
            env.close();

            env.require(Owners(alice, 3));
            env.require(Balance(alice, usd(10)));
            env.require(Balance(alice, eur(10'000)));
            aliceOffers = offersOnAccount(env, alice);
            BEAST_EXPECT(aliceOffers.size() == 1);
            for (auto const& offerPtr : aliceOffers)
            {
                auto const offer = *offerPtr;
                BEAST_EXPECT(offer[sfLedgerEntryType] == ltOFFER);
                if constexpr (std::is_same_v<std::decay_t<decltype(eur)>, IOU>)
                {
                    BEAST_EXPECT(offer[sfTakerGets] == eur(5'988));
                }
                else
                {
                    BEAST_EXPECT(offer[sfTakerGets] == eur(5'989));
                }
                BEAST_EXPECT(offer[sfTakerPays] == usd(4'990));
            }
        };
        testHelper2TokensMix(test);
    }

    template <typename TGets, typename TPays>
    struct TokenData
    {
        TGets gets;
        TPays pays;
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
            MPT const usd =
                MPTTester({.env = env, .issuer = gw1, .holders = {alice}, .maxAmt = 506});
            MPT const eur =
                MPTTester({.env = env, .issuer = gw2, .holders = {alice}, .maxAmt = 606});
            // Payment's engine last step overflows
            // OutstandingAmount since it doesn't know if the
            // BookStep redeems or not. The BookStep then has 600EUR
            // available. Consequently, the entire offer is crossed.
            // Note remaining takerGets is 541 rather than 540 due to integral
            // rounding. XRP has a similar result.
            return TokenData<MPT, MPT>{
                .gets = eur, .pays = usd, .remTakerGets = eur(541), .remTakerPays = usd(450)};
        };

        auto initXRP = [&](Env& env) {
            MPT const usd =
                MPTTester({.env = env, .issuer = gw1, .holders = {alice}, .maxAmt = 1'000});
            // Payment's engine last step overflows
            // OutstandingAmount since it doesn't know if the
            // BookStep redeems or not. The BookStep then has 600EUR
            // available. Consequently, the entire offer is crossed.
            // Note remaining takerGets is 540.000001 rather than 540 due to
            // integral rounding.
            return TokenData<XrpT, MPT>{
                .gets = XRP,
                .pays = usd,
                .remTakerGets = XRP(540.000001),
                .remTakerPays = usd(450)};
        };

        auto initIOU = [&](Env& env) {
            auto const usd = gw1["USD"];
            auto const eur = gw2["EUR"];
            env(trust(alice, usd(506)));
            env(trust(alice, eur(606)));
            env.close();
            // Payment's engine last step is limited by alice's
            // trustline - 606. Therefore, only 6EUR is delivered
            // and the offer is partially crossed.
            return TokenData<IOU, IOU>{
                .gets = eur, .pays = usd, .remTakerGets = eur(594), .remTakerPays = usd(495)};
        };

        auto initIOU1 = [&](Env& env) {
            auto const usd = gw1["USD"];
            auto const eur = gw2["EUR"];
            env(trust(alice, usd(1'000)));
            env(trust(alice, eur(1'000)));
            env.close();
            // Payment's engine last step is not limited by alice's
            // trustline. Therefore, the entire offer is crossed.
            // This the same result as with MPT.
            return TokenData<IOU, IOU>{
                .gets = eur, .pays = usd, .remTakerGets = eur(540), .remTakerPays = usd(450)};
        };

        auto test = [&](auto&& initToken) {
            Env env(*this, features);

            env.fund(XRP(2'000), gw1, gw2, alice);
            env.close();

            auto const f = env.current()->fees().base;

            auto const tok = initToken(env);

            auto const& toK1 = tok.pays;
            auto const& toK2 = tok.gets;
            bool const isTakerGetsXRP = isXRP(Asset{toK2});
            std::uint32_t const ownerCnt = isTakerGetsXRP ? 2 : 3;

            env(pay(gw1, alice, toK1(500)));
            if (!isTakerGetsXRP)
                env(pay(gw2, alice, toK2(600)));
            env.close();

            env(offer(alice, toK1(500), toK2(600)));
            env.close();

            env.require(Owners(alice, ownerCnt));
            env.require(Balance(alice, toK1(500)));
            if (isTakerGetsXRP)
            {
                env.require(Balance(alice, toK2(2'000) - 2 * f));
            }
            else
            {
                env.require(Balance(alice, toK2(600)));
            }

            auto aliceOffers = offersOnAccount(env, alice);
            BEAST_EXPECT(aliceOffers.size() == 1);
            for (auto const& offerPtr : aliceOffers)
            {
                auto const offer = *offerPtr;
                BEAST_EXPECT(offer[sfLedgerEntryType] == ltOFFER);
                BEAST_EXPECT(offer[sfTakerGets] == toK2(600));
                BEAST_EXPECT(offer[sfTakerPays] == toK1(500));
            }

            env(pay(alice, alice, toK2(60)), Sendmax(toK1(50)), Txflags(tfPartialPayment));
            env.close();

            env.require(Owners(alice, ownerCnt));
            env.require(Balance(alice, toK1(500)));
            if (isTakerGetsXRP)
            {
                env.require(Balance(alice, toK2(2'000) - 3 * f));
            }
            else
            {
                env.require(Balance(alice, toK2(600)));
            }
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

        test(initXRP);
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

        MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice}, .maxAmt = 20});

        env(pay(gw, alice, usd(10)));
        env(offer(alice, XRP(50'000), usd(10)));

        // Consuming the offer changes the owner count, which could
        // also cause liquidity to decrease in the forward pass
        auto const toSend = consumeOffer ? usd(10) : usd(9);
        env(pay(alice, alice, toSend),
            Path(~usd),
            Sendmax(XRP(20'000)),
            Txflags(tfPartialPayment | tfNoRippleDirect));
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

            MPT const usd =
                MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}, .maxAmt = 20E+17});

            // scale by 17
            STAmount const tinyAmt1{usd, 9'000'000'000'000'000ll, 0, false, STAmount::Unchecked{}};
            STAmount const tinyAmt3{usd, 9'000'000'000'000'003ll, 0, false, STAmount::Unchecked{}};

            env(offer(gw, drops(9'000'000'000), tinyAmt3));

            env(pay(alice, bob, tinyAmt1),
                Path(~usd),
                Sendmax(drops(9'000'000'000)),
                Txflags(tfNoRippleDirect));

            BEAST_EXPECT(!isOffer(env, gw, XRP(0), usd(0)));
        }
        {
            // Test forward
            Env env(*this, features);

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            env.fund(XRP(100'000), alice, bob, gw);

            MPT const usd =
                MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}, .maxAmt = 20E+17});

            // scale by 17
            STAmount const tinyAmt1{usd, 9'000'000'000'000'000ll, 0, false, STAmount::Unchecked{}};
            STAmount const tinyAmt3{usd, 9'000'000'000'000'003ll, 0, false, STAmount::Unchecked{}};

            env(pay(gw, alice, tinyAmt1));

            env(offer(gw, tinyAmt3, drops(9'000'000'000)));
            env(pay(alice, bob, drops(9'000'000'000)),
                Path(~XRP),
                Sendmax(usd(static_cast<std::uint64_t>(1E+17))),
                Txflags(tfNoRippleDirect));

            BEAST_EXPECT(!isOffer(env, gw, usd(0), XRP(0)));
        }
    }

    void
    testReExecuteDirectStep(FeatureBitset features)
    {
        testcase("ReexecuteDirectStep");

        using namespace jtx;
        Env env(*this, features);

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");

        env.fund(XRP(10'000), alice, bob, gw);

        // scale by 16
        MPT const usd =
            MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}, .maxAmt = 100E+16});

        env(
            pay(gw,
                alice,
                // 12.55....
                STAmount{usd, std::uint64_t(1255555555555555ull), 2, false}));

        env(offer(
            gw,
            // 5.0...
            STAmount{usd, std::uint64_t(5000000000000000ull), 1, false},
            XRP(1000)));

        env(offer(
            gw,
            // .555...
            STAmount{usd, std::uint64_t(5555555555555555ull), 0, false},
            XRP(10)));

        env(offer(
            gw,
            // 4.44....
            STAmount{usd, std::uint64_t(4444444444444444ull), 1, false},
            XRP(.1)));

        env(offer(
            alice,
            // 17
            STAmount{usd, std::uint64_t(1700000000000000ull), 0, false},
            XRP(.001)));

        env(pay(alice, bob, XRP(10'000)),
            Path(~XRP),
            Sendmax(usd(static_cast<std::uint64_t>(100E+16))),
            Txflags(tfPartialPayment | tfNoRippleDirect));
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
        MPT const ctb = MPTTester(
            {.env = env,
             .issuer = gw,
             .holders = {ann},
             .transferFee = 2'000,  // 2%
             .maxAmt = 1'000'000});

        env(pay(gw, ann, ctb(285'600)));
        env.close();

        env(offer(ann, drops(365'611'702'030), ctb(571'300)));
        env.close();

        // This payment caused assert.
        env(pay(ann, ann, ctb(68'700)), Sendmax(drops(20'000'000'000)), Txflags(tfPartialPayment));
    }

    void
    testEmptyStrand(FeatureBitset features)
    {
        testcase("Empty Strand");
        using namespace jtx;

        auto const alice = Account("alice");

        Env env(*this, features);

        env.fund(XRP(10000), alice);

        MPT const usd;

        env(pay(alice, alice, usd(100)), Path(~usd), Ter(temBAD_PATH));
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

                auto const usd =
                    issue1({.env = env, .token = "USD", .issuer = gw, .holders = {alice, bob}});
                auto const eur =
                    issue2({.env = env, .token = "EUR", .issuer = gw, .holders = {alice, bob}});
                env(pay(gw, alice, usd(100)));
                env(pay(gw, alice, eur(100)));
                env.close();

                env(offer(alice, XRP(100), usd(100)), Txflags(tfPassive));
                env(offer(alice, usd(100), XRP(100)), Txflags(tfPassive));
                env(offer(alice, XRP(100), eur(100)), Txflags(tfPassive));
                env.close();

                TER const expectedTer = TER{temBAD_PATH_LOOP};
                env(pay(alice, bob, eur(1)),
                    Path(~usd, ~XRP, ~eur),
                    Sendmax(XRP(1)),
                    Txflags(tfNoRippleDirect),
                    Ter(expectedTer));
            };
            testHelper2TokensMix(test);
        }
        {
            // Payment path ending with XRP
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);
                env.fund(XRP(10'000), alice, bob, gw);
                auto const usd =
                    issue1({.env = env, .token = "USD", .issuer = gw, .holders = {alice, bob}});
                auto const eur =
                    issue2({.env = env, .token = "EUR", .issuer = gw, .holders = {alice, bob}});
                env(pay(gw, alice, usd(100)));
                env(pay(gw, alice, eur(100)));
                env.close();

                env(offer(alice, XRP(100), usd(100)), Txflags(tfPassive));
                env(offer(alice, eur(100), XRP(100)), Txflags(tfPassive));
                env.close();
                // EUR -> //XRP -> //USD ->XRP
                env(pay(alice, bob, XRP(1)),
                    Path(~XRP, ~usd, ~XRP),
                    Sendmax(eur(1)),
                    Txflags(tfNoRippleDirect),
                    Ter(temBAD_PATH_LOOP));
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
                auto const usd =
                    issue1({.env = env, .token = "USD", .issuer = gw, .holders = {alice, bob}});
                auto const eur =
                    issue2({.env = env, .token = "EUR", .issuer = gw, .holders = {alice, bob}});
                auto const jpy =
                    issue3({.env = env, .token = "JPY", .issuer = gw, .holders = {alice, bob}});
                env(pay(gw, alice, usd(100)));
                env(pay(gw, alice, eur(100)));
                env(pay(gw, alice, jpy(100)));
                env.close();

                env(offer(alice, usd(100), XRP(100)), Txflags(tfPassive));
                env(offer(alice, XRP(100), eur(100)), Txflags(tfPassive));
                env(offer(alice, eur(100), XRP(100)), Txflags(tfPassive));
                env(offer(alice, XRP(100), jpy(100)), Txflags(tfPassive));
                env.close();

                env(pay(alice, bob, jpy(1)),
                    Path(~XRP, ~eur, ~XRP, ~jpy),
                    Sendmax(usd(1)),
                    Txflags(tfNoRippleDirect),
                    Ter(temBAD_PATH_LOOP));
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

            MPT const usd =
                MPTTester({.env = env, .issuer = gw, .holders = {alice, carol}, .maxAmt = 100});

            env(pay(gw, alice, usd(100)));

            env(pay(alice, carol, usd(100)));

            BEAST_EXPECT(env.balance(gw, usd) == usd(-100));
            BEAST_EXPECT(env.balance(carol, usd) == usd(100));
            BEAST_EXPECT(env.balance(alice, usd) == usd(0));
        }

        // Direct payment between holders. Partial payment limited
        // by holder funds.
        {
            Env env(*this);

            env.fund(XRP(1'000), gw, alice, carol);

            MPT const usd =
                MPTTester({.env = env, .issuer = gw, .holders = {alice, carol}, .maxAmt = 100});

            env(pay(gw, alice, usd(80)));

            env(pay(alice, carol, usd(100)), Txflags(tfPartialPayment));

            BEAST_EXPECT(env.balance(gw, usd) == usd(-80));
            BEAST_EXPECT(env.balance(alice, usd) == usd(0));
            BEAST_EXPECT(env.balance(carol, usd) == usd(80));
        }

        // Direct payment between holders. Partial payment limited
        // by holder funds. OutstandingAmount is already at max
        // before the payment.
        {
            Env env(*this);

            env.fund(XRP(1'000), gw, alice, carol, bob);

            MPT const usd = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice, carol, bob}, .maxAmt = 100});

            env(pay(gw, bob, usd(20)));
            env(pay(gw, alice, usd(80)));

            env(pay(alice, carol, usd(100)), Txflags(tfPartialPayment));

            BEAST_EXPECT(env.balance(gw, usd) == usd(-100));
            BEAST_EXPECT(env.balance(alice, usd) == usd(0));
            BEAST_EXPECT(env.balance(carol, usd) == usd(80));
        }

        // Cross-currency payment holder to holder. Holder owns an
        // offer. OutstandingAmount is already at max before the
        // payment.
        {
            Env env(*this);

            env.fund(XRP(1'000), gw, alice, carol, bob);

            MPT const usd =
                MPTTester({.env = env, .issuer = gw, .holders = {alice, carol}, .maxAmt = 100});

            env(pay(gw, alice, usd(100)));

            env(offer(alice, XRP(100), usd(100)));

            env(pay(bob, carol, usd(100)), Sendmax(XRP(100)), Path(~usd));

            BEAST_EXPECT(env.balance(gw, usd) == usd(-100));
            BEAST_EXPECT(env.balance(alice, usd) == usd(0));
            BEAST_EXPECT(env.balance(carol, usd) == usd(100));
        }

        // Cross-currency payment holder to holder. Issuer owns an
        // offer. OutstandingAmount is already at max before the
        // payment. Since an issuer owns the offer, it issues more
        // tokens to another holder, and the payment fails.
        {
            Env env(*this);

            env.fund(XRP(1'000), gw, alice, carol);

            MPT const usd =
                MPTTester({.env = env, .issuer = gw, .holders = {carol}, .maxAmt = 100});

            env(pay(gw, carol, usd(100)));

            env(offer(gw, XRP(100), usd(100)));

            env(pay(alice, carol, usd(100)),
                Sendmax(XRP(100)),
                Path(~usd),
                Txflags(tfPartialPayment),
                Ter(tecPATH_DRY));

            BEAST_EXPECT(env.balance(gw, usd) == usd(-100));
            BEAST_EXPECT(env.balance(carol, usd) == usd(100));
        }

        // Cross-currency payment holder to holder. Issuer owns an
        // offer. OutstandingAmount is at 80USD before the payment.
        // Consequently, the issuer can issue 20USD more.
        {
            Env env(*this);

            env.fund(XRP(1'000), gw, alice, carol);

            MPT const usd =
                MPTTester({.env = env, .issuer = gw, .holders = {carol}, .maxAmt = 100});

            env(pay(gw, carol, usd(80)));

            env(offer(gw, XRP(100), usd(100)));

            env(pay(alice, carol, usd(100)),
                Sendmax(XRP(100)),
                Path(~usd),
                Txflags(tfPartialPayment));

            BEAST_EXPECT(env.balance(gw, usd) == usd(-100));
            BEAST_EXPECT(env.balance(carol, usd) == usd(100));
        }

        // Cross-currency payment holder to holder. Holder owns an
        // offer. The offer buys more MPT's. The payment fails since
        // OutstandingAmount is already at max.
        {
            Env env(*this);

            env.fund(XRP(1'000), gw, alice);

            MPT const usd =
                MPTTester({.env = env, .issuer = gw, .holders = {alice}, .maxAmt = 100});

            env(pay(gw, alice, usd(100)));

            env(offer(alice, usd(100), XRP(100)));

            env(pay(gw, alice, XRP(100)), Sendmax(usd(100)), Path(~XRP), Ter(tecPATH_PARTIAL));

            BEAST_EXPECT(env.balance(gw, usd) == usd(-100));
            BEAST_EXPECT(env.balance(alice, usd) == usd(100));
        }

        // Cross-currency payment issuer to holder. Holder owns an
        // offer. The offer buys EUR, OutstandingAmount goes to max,
        // no overflow. The offer redeems USD to the issuer. While
        // OutstandingAmount is already at max, the payment succeeds
        // since USD is redeemed.
        {
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);

                env.fund(XRP(1'000), gw, alice, carol);
                env.close();

                auto const usd = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, carol},
                     .limit = 100});
                using tUSD = std::decay_t<decltype(usd)>;
                auto const eur = issue2(
                    {.env = env,
                     .token = "EUR",
                     .issuer = gw,
                     .holders = {alice, carol},
                     .limit = 100});

                env(pay(gw, alice, usd(100)));

                env(offer(alice, eur(100), usd(100)));

                env(pay(gw, carol, usd(100)), Sendmax(eur(100)), Path(~usd));

                if constexpr (std::is_same_v<tUSD, MPT>)
                    BEAST_EXPECT(env.balance(gw, usd) == usd(-100));
                BEAST_EXPECT(env.balance(alice, usd) == usd(0));
                BEAST_EXPECT(env.balance(alice, eur) == eur(100));
                BEAST_EXPECT(env.balance(carol, usd) == usd(100));
            };
            testHelper2TokensMix(test);
        }

        // Cross-currency payment holder to holder. Offer is owned
        // by destination account. OutstandingAmount is not at max.
        {
            Env env(*this);

            env.fund(XRP(1'000), gw, alice, carol);

            MPT const usd =
                MPTTester({.env = env, .issuer = gw, .holders = {carol}, .maxAmt = 120});

            env(pay(gw, carol, usd(100)));

            env(offer(carol, XRP(100), usd(100)));

            env(pay(alice, carol, usd(100)),
                Path(~usd),
                Sendmax(XRP(100)),
                Txflags(tfPartialPayment));

            BEAST_EXPECT(env.balance(carol, usd) == usd(100));
        }

        // Cross-currency payment holder to holder. Offer is owned
        // by destination account. OutstandingAmount is already at
        // max.
        {
            Env env(*this);

            env.fund(XRP(1'000), gw, alice, carol);

            MPT const usd =
                MPTTester({.env = env, .issuer = gw, .holders = {carol}, .maxAmt = 100});

            env(pay(gw, carol, usd(100)));

            env(offer(carol, XRP(100), usd(100)));

            env(pay(alice, carol, usd(100)),
                Path(~usd),
                Sendmax(XRP(100)),
                Txflags(tfPartialPayment));

            BEAST_EXPECT(env.balance(carol, usd) == usd(100));
        }

        // Cross-currency payment holder to holder. Multiple offers
        // with different owners - some holders, some issuer.
        {
            auto test = [&](auto&& issue1, auto&& issue2) {
                Env env(*this);

                env.fund(XRP(1'000), gw, alice, carol, bob);
                env.close();

                auto const usd = issue1(
                    {.env = env,
                     .token = "USD",
                     .issuer = gw,
                     .holders = {alice, carol, bob},
                     .limit = 1'000});
                using tUSD = std::decay_t<decltype(usd)>;
                auto const eur = issue2(
                    {.env = env,
                     .token = "EUR",
                     .issuer = gw,
                     .holders = {alice, carol, bob},
                     .limit = 1'000});
                using tEUR = std::decay_t<decltype(eur)>;

                env(pay(gw, alice, usd(600)));
                env(pay(gw, carol, eur(700)));

                env(offer(alice, eur(100), usd(105)));
                env(offer(gw, eur(100), usd(104)));
                env(offer(gw, eur(100), usd(103)));
                env(offer(gw, eur(100), usd(102)));
                env(offer(gw, eur(100), usd(101)));
                env(offer(gw, eur(100), usd(100)));

                env(pay(carol, bob, usd(2'000)),
                    Sendmax(eur(2'000)),
                    Path(~usd),
                    Txflags(tfPartialPayment));

                if constexpr (std::is_same_v<tUSD, MPT>)
                {
                    BEAST_EXPECT(env.balance(gw, usd) == usd(-1'000));
                    BEAST_EXPECT(env.balance(alice, usd) == usd(495));
                    BEAST_EXPECT(env.balance(bob, usd) == usd(505));
                }
                else
                {
                    BEAST_EXPECT(env.balance(gw, usd) == usd(0));
                    BEAST_EXPECT(env.balance(alice, usd) == usd(495));
                    // all offers are consumed since the limit is different
                    // for the holders
                    BEAST_EXPECT(env.balance(bob, usd) == usd(615));
                }
                if constexpr (std::is_same_v<tEUR, MPT>)
                {
                    if constexpr (std::is_same_v<tUSD, MPT>)
                    {
                        BEAST_EXPECT(env.balance(carol, eur) == eur(210));
                    }
                    else
                    {
                        // carol sells 600USD since all offers are consumed
                        BEAST_EXPECT(env.balance(carol, eur) == eur(100));
                    }
                }
                else
                {
                    BEAST_EXPECT(
                        env.balance(carol, eur) == STAmount(eur, UINT64_C(209'9009900990099), -13));
                }
                // 100/101 is partially crossed (90/91) and 100/100 is
                // unfunded when MPT. All offers are consumed if IOU.
                env.require(offers(gw, 0));
                // alice's offer is consumed.
                env.require(offers(alice, 0));
            };
            testHelper2TokensMix(test);
        }

        // Cross-currency payment holder to holder. Multiple offers
        // with different owners - some holders, some issuer. Source
        // and destination account is the same.
        {
            Env env(*this);

            env.fund(XRP(1'000), gw, alice, carol);

            MPT const usd =
                MPTTester({.env = env, .issuer = gw, .holders = {alice, carol}, .maxAmt = 2'000});

            env(pay(gw, carol, usd(1'000)));
            env(pay(gw, alice, usd(600)));

            env(offer(gw, XRP(5), usd(11)));
            env(offer(gw, XRP(6), usd(13)));
            env(offer(carol, XRP(7), usd(15)));
            env(offer(carol, XRP(17), usd(35)));
            env(offer(carol, XRP(23), usd(47)));
            env(offer(alice, XRP(10), usd(19)));
            env(offer(alice, XRP(15), usd(28)));
            env(offer(alice, XRP(25), usd(46)));

            env(pay(carol, carol, usd(200)), Sendmax(XRP(100)), Txflags(tfPartialPayment));

            BEAST_EXPECT(env.balance(gw, usd) == usd(-1'624));
            BEAST_EXPECT(env.balance(carol, usd) == usd(1'102));
            env.require(offers(carol, 0));
            env.require(offers(gw, 0));
            // 100 XRP's = 5+6+7+17+23+10+15+17(25-8)
            BEAST_EXPECT(isOffer(env, alice, XRP(8), usd(15)));
        }

        // Cross-currency payment holder to holder. Multiple offers
        // with different owners - some holders, some issuer.
        {
            Env env(*this);
            env.fund(XRP(1'000), gw, alice, carol, bob);

            MPT const usd =
                MPTTester({.env = env, .issuer = gw, .holders = {alice, carol, bob}, .maxAmt = 30});

            env(pay(gw, alice, usd(12)));  // 12, 15, 20
            env(pay(gw, bob, usd(5)));     // 5, 5, 10

            env(offer(alice, XRP(10), usd(12)));
            env(offer(gw, XRP(10), usd(11)));
            env(offer(bob, XRP(10), usd(10)));

            env(pay(carol, bob, usd(30)), Sendmax(XRP(30)), Txflags(tfPartialPayment), Path(~usd));
            BEAST_EXPECT(env.balance(gw, usd) == usd(-28));
            BEAST_EXPECT(env.balance(alice, usd) == usd(0));
            // 12+11+5
            BEAST_EXPECT(env.balance(bob, usd) == usd(28));
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
                int outstandingUSD;
                int expEdBuyUSD;
                int expDanBuyUSD;
                int expBobSellUSD;
                int expGwXRP;  // whole XRP excluding the fees
                std::uint8_t expOffersGw;
                bool lastGwBuyUSD;
                [[nodiscard]] std::uint8_t
                expOffersBob() const
                {
                    return expBobSellUSD == 0 ? 1 : 0;
                }
                [[nodiscard]] std::uint8_t
                expOffersEd() const
                {
                    // partially crossed if < 100
                    return expEdBuyUSD < 100 ? 1 : 0;
                }
                [[nodiscard]] std::uint8_t
                expOffersDan() const
                {
                    return expDanBuyUSD == 0 ? 1 : 0;
                }
            };

            auto test = [&](TestData const& d) {
                Env env(*this);
                env.fund(XRP(1'000), gw, alice, carol, bob, dan, john, ed, sam, bill);
                env.close();

                MPT const usd = MPTTester(
                    {.env = env, .issuer = gw, .holders = {alice, carol, bob}, .maxAmt = d.maxAmt});
                auto const eur = gw["EUR"];

                env(pay(gw, alice, usd(100)));
                env(pay(gw, carol, usd(100)));
                env(pay(gw, bob, usd(100)));

                BEAST_EXPECT(env.balance(gw, usd) == usd(-300));

                env(trust(john, eur(100)));
                env(trust(dan, eur(100)));
                env(trust(ed, eur(100)));
                env(trust(bill, eur(d.dstTrustLimit)));

                env(pay(gw, john, eur(100)));
                env(pay(gw, dan, eur(100)));
                env(pay(gw, ed, eur(100)));
                env.close();

                // Sell USD
                env(offer(alice, XRP(100), usd(100)));
                env.close();  // close after each create to ensure
                              // the order
                env(offer(carol, XRP(100), usd(100)));
                env.close();
                if (!d.lastGwBuyUSD)
                {
                    env(offer(gw, XRP(100), usd(100)));
                    env.close();
                }
                env(offer(bob, XRP(100), usd(100)));
                env.close();
                if (d.lastGwBuyUSD)
                {
                    env(offer(gw, XRP(100), usd(100)));
                    env.close();
                }
                BEAST_EXPECT(expectOffers(env, alice, 1));
                BEAST_EXPECT(expectOffers(env, carol, 1));
                BEAST_EXPECT(expectOffers(env, gw, 1));
                BEAST_EXPECT(expectOffers(env, bob, 1));

                // Buy USD
                env(offer(john, usd(100), eur(100)));
                env.close();
                env(offer(gw, usd(100), eur(100)));
                env.close();
                env(offer(dan, usd(100), eur(100)));
                env.close();
                env(offer(ed, usd(100), eur(100)));
                env.close();
                BEAST_EXPECT(expectOffers(env, john, 1));
                BEAST_EXPECT(expectOffers(env, gw, 2));
                BEAST_EXPECT(expectOffers(env, dan, 1));
                BEAST_EXPECT(expectOffers(env, ed, 1));

                env(pay(sam, bill, eur(400)),
                    Sendmax(XRP(d.sendMax)),
                    Path(~usd, ~eur),
                    Txflags(tfPartialPayment | tfNoRippleDirect));
                env.close();

                auto const baseFee = env.current()->fees().base.drops();
                BEAST_EXPECT(env.balance(bill, eur) == eur(d.dstExpectEUR));
                BEAST_EXPECT(env.balance(john, usd) == usd(100));
                BEAST_EXPECT(env.balance(dan, usd) == usd(d.expDanBuyUSD));
                BEAST_EXPECT(env.balance(ed, usd) == usd(d.expEdBuyUSD));
                BEAST_EXPECT(env.balance(gw, usd) == usd(-d.outstandingUSD));
                BEAST_EXPECT(env.balance(alice, usd) == usd(0));
                BEAST_EXPECT(env.balance(carol, usd) == usd(0));
                BEAST_EXPECT(env.balance(bob, usd) == usd(100 - d.expBobSellUSD));
                BEAST_EXPECT(env.balance(gw) == XRPAmount{d.expGwXRP * kDropsPerXrp - baseFee * 9});
                BEAST_EXPECT(expectOffers(env, john, 0));
                BEAST_EXPECT(expectOffers(env, gw, d.expOffersGw));
                BEAST_EXPECT(expectOffers(env, dan, d.expOffersDan()));
                BEAST_EXPECT(expectOffers(env, ed, d.expOffersEd()));
                BEAST_EXPECT(expectOffers(env, alice, 0));
                BEAST_EXPECT(expectOffers(env, carol, 0));
                BEAST_EXPECT(expectOffers(env, bob, d.expOffersBob()));
            };

            // clang-format off
            std::vector<TestData> const tests = {
                // Sell USD: alice, carol, bob, gw are consumed.
                // Buy USD: john, gw, dan, ed are consumed.
                // gw's sell USD is consumed because there is sufficient available balance (100USD).
                // but OutstandingAmount is 300USD because gw's sell offer is balanced out by
                // gw's buy offer.
                //*maxAmt sendMax limitEUR expectEUR outstandingUSD edBuy danBuy bobSell gwXRP offersGw lastGw
                {  400,   400,    400,     400,      300,           100,  100,   100,    1100, 0,       false},
                // Sell USD: alice, carol, bob, gw are consumed.
                // Buy USD: john, gw, dan, ed (partially) are consumed.
                // gw's sell USD is partially consumed because there is available balance (50USD).
                // OutstandingAmount is 250USD because gw's sell offer is partially balanced by
                // gw's buy offer. ed's offer is on the books because it's partially crossed.
                // gw's offer is removed from the order book because it's partially consumed and
                // the remaining offer is unfunded.
                //*maxAmt sendMax limitEUR expectEUR outstandingUSD edBuy danBuy bobSell gwXRP offersGw lastGw
                {  350,   400,    400,     350,      250,           50,   100,   100,    1050, 0,       false},
                // Sell USD: alice, carol, bob are consumed; gw's is unfunded
                //   since OutstandingAmount is initially at MaximumAmount.
                // Buy USD: john, gw, dan are consumed; ed's remains on the order
                //   book since 300USD is the sell limit.
                //*maxAmt sendMax limitEUR expectEUR outstandingUSD edBuy danBuy bobSell gwXRP offersGw lastGw
                {  300,   400,    400,     300,      200,           0,    100,   100,    1000, 0,       false},
                // Same as above. bill's trustline limit sets the output to 300USD.
                //*maxAmt sendMax limitEUR expectEUR outstandingUSD edBuy danBuy bobSell gwXRP offersGw lastGw
                {  300,   400,    300,     300,      200,           0,    100,   100,    1000, 0,       false},
                // Sell USD: alice, carol, bob are consumed; gw's removed from
                //   the order book since it's unfunded.
                // Buy USD: john, gw, dan are consumed; ed's  remains on the order
                //   book since 300USD is the limit.
                //*maxAmt sendMax limitEUR expectEUR outstandingUSD edBuy danBuy bobSell gwXRP offersGw lastGw
                {  300,   400,    300,     300,      200,           0,    100,   100,    1000, 0,       true},
                // Sell USD: alice, carol are consumed; gw's removed from
                //   the order book in rev pass since it's unfunded; bob's
                //   remains on the order book.
                // Buy USD: john, gw; ed's, dan's  remains on the order
                //   book since 300USD is the limit.
                //*maxAmt sendMax limitEUR expectEUR outstandingUSD edBuy danBuy bobSell gwXRP offersGw lastGw
                {  300,   200,    300,     200,      200,           0,    0,     0,      1000, 0,       false},
                // Same as three tests above since limited by buy 300USD (gw offer is unfunded)
                //*maxAmt sendMax limitEUR expectEUR outstandingUSD edBuy danBuy bobSell gwXRP offersGw lastGw
                {  300,   380,    400,     300,      200,           0,    100,   100,    1000, 0,       false},
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
                int outstandingUSD;
                int expBobBuyUSD;
                int expGwXRP;  // whole XRP excluding the fees
                std::uint8_t expOffersGw;
                bool lastGwBuyUSD;
                [[nodiscard]] std::uint8_t
                expOffersBob() const
                {
                    // partially crossed if < 100
                    return expBobBuyUSD < 100 ? 1 : 0;
                }
            };

            auto test = [&](TestData const& d) {
                Env env(*this);
                env.fund(XRP(1'000), gw, alice, carol, bob, ed);
                env.close();

                MPT const usd =
                    MPTTester({.env = env, .issuer = gw, .holders = {alice}, .maxAmt = d.maxAmt});

                env(pay(gw, alice, usd(300)));
                env.close();

                env(offer(carol, usd(100), XRP(100)));
                env.close();
                if (!d.lastGwBuyUSD)
                {
                    env(offer(gw, usd(d.gwOffer), XRP(d.gwOffer)));
                    env.close();
                }
                env(offer(bob, usd(100), XRP(100)));
                env.close();
                if (d.lastGwBuyUSD)
                {
                    env(offer(gw, usd(d.gwOffer), XRP(d.gwOffer)));
                    env.close();
                }

                BEAST_EXPECT(expectOffers(env, carol, 1));
                BEAST_EXPECT(expectOffers(env, bob, 1));
                BEAST_EXPECT(expectOffers(env, gw, 1));
                BEAST_EXPECT(env.balance(gw, usd) == usd(-300));

                env(pay(alice, ed, XRP(300)),
                    Sendmax(usd(d.sendMax)),
                    Path(~XRP),
                    Txflags(tfPartialPayment | tfNoRippleDirect));
                env.close();

                auto const baseFee = env.current()->fees().base.drops();
                BEAST_EXPECT(env.balance(alice, usd) == usd(300 - d.sendMax));
                BEAST_EXPECT(env.balance(carol, usd) == usd(100));
                BEAST_EXPECT(env.balance(bob, usd) == usd(d.expBobBuyUSD));
                BEAST_EXPECT(env.balance(ed) == XRP(d.dstExpectXRP));
                BEAST_EXPECT(env.balance(gw, usd) == usd(-d.outstandingUSD));
                BEAST_EXPECT(env.balance(gw) == XRPAmount{d.expGwXRP * kDropsPerXrp - baseFee * 3});
                BEAST_EXPECT(expectOffers(env, carol, 0));
                BEAST_EXPECT(expectOffers(env, bob, d.expOffersBob()));
                BEAST_EXPECT(expectOffers(env, gw, d.expOffersGw));
            };

            // clang-format off
            std::vector<TestData> const tests = {
                // Buy USD: carol, gw, bob are consumed.
                // Gw gets 300USD from alice; carol and bob buy 200USD,
                // therefore OutstandingAmount is 200.
                //*maxAmt sendMax gwOffer dstXRP outstandingUSD bobBuy gwXRP offersGw lastGw
                { 300,    300,    100,    1300,  200,           100,   900,  0,       false},
                // Same as above. Gw offer location in the order book doesn't matter
                //*maxAmt sendMax gwOffer dstXRP outstandingUSD bobBuy gwXRP offersGw lastGw
                { 300,    300,    100,    1300,  200,           100,   900,  0,       true},
                // Buy USD: carol, gw are consumed. bob's offer remains on the order book.
                // Gw gets 300USD from alice; carol buys 100USD,
                // therefore OutstandingAmount is 100.
                //*maxAmt sendMax gwOffer dstXRP outstandingUSD bobBuy gwXRP offersGw lastGw
                { 300,    300,    200,    1300,  100,           0,     800,  0,       false},
                // Buy USD: carol, bob are consumed; gw's is partially consumed (100/100) since it's last.
                // Gw gets 300USD from alice; carol and bob buy 200USD,
                // therefore OutstandingAmount is 200.
                //*maxAmt sendMax gwOffer dstXRP outstandingUSD bobBuy gwXRP offersGw lastGw
                { 300,    300,    200,    1300,  200,           100,   900,  1,       true},
                // Buy USD: carol, bob are consumed; gw's is partially consumed (50/50) since it's last
                // and sendMax limits the output.
                // Gw gets 250USD from alice; carol and bob buy 200USD, alice has 50USD left,
                // therefore OutstandingAmount is 200.
                //*maxAmt sendMax gwOffer dstXRP outstandingUSD bobBuy gwXRP offersGw lastGw
                { 300,    250,    200,    1250,  250,           100,   950,  1,       true},
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
                int outstandingUSD;
                int expAliceXRP;  // whole XRP excluding the fees
                int expBobSellUSD;
                int expGwXRP;
                std::uint8_t expOffersGw;
                bool lastGwBuyUSD;
                [[nodiscard]] std::uint8_t
                expOffersBob() const
                {
                    return expBobSellUSD > 0 && expBobSellUSD < 100 ? 1 : 0;
                }
            };

            auto test = [&](TestData const& d) {
                Env env(*this);
                env.fund(XRP(1'000), gw, alice, carol, bob, ed);
                env.close();

                MPT const usd = MPTTester(
                    {.env = env, .issuer = gw, .holders = {carol, bob, ed}, .maxAmt = d.maxAmt});

                if (d.initDst != 0)
                    env(pay(gw, ed, usd(d.initDst)));
                env(pay(gw, carol, usd(100)));
                env(pay(gw, bob, usd(100)));
                env.close();

                env(offer(carol, XRP(100), usd(100)));
                env.close();
                if (!d.lastGwBuyUSD)
                {
                    env(offer(gw, XRP(d.gwOffer), usd(d.gwOffer)));
                    env.close();
                }
                env(offer(bob, XRP(100), usd(100)));
                env.close();
                if (d.lastGwBuyUSD)
                {
                    env(offer(gw, XRP(d.gwOffer), usd(d.gwOffer)));
                    env.close();
                }

                BEAST_EXPECT(expectOffers(env, carol, 1));
                BEAST_EXPECT(expectOffers(env, bob, 1));
                BEAST_EXPECT(expectOffers(env, gw, 1));
                BEAST_EXPECT(env.balance(gw, usd) == usd(-200 - d.initDst));

                env(pay(alice, ed, usd(300)),
                    Sendmax(XRP(d.sendMax)),
                    Path(~usd),
                    Txflags(tfPartialPayment | tfNoRippleDirect));
                env.close();

                auto const baseFee = env.current()->fees().base.drops();
                BEAST_EXPECT(
                    env.balance(alice) == XRPAmount{d.expAliceXRP * kDropsPerXrp - baseFee});
                BEAST_EXPECT(env.balance(carol, usd) == usd(0));
                BEAST_EXPECT(env.balance(bob, usd) == usd(100 - d.expBobSellUSD));
                BEAST_EXPECT(env.balance(ed, usd) == usd(d.dstExpectUSD));
                BEAST_EXPECT(env.balance(gw, usd) == usd(-d.outstandingUSD));
                BEAST_EXPECT(
                    env.balance(gw) ==
                    XRPAmount{
                        d.expGwXRP * kDropsPerXrp - baseFee * (4 + (d.initDst != 0 ? 1 : 0))});
                BEAST_EXPECT(expectOffers(env, carol, 0));
                BEAST_EXPECT(expectOffers(env, bob, d.expOffersBob()));
                BEAST_EXPECT(expectOffers(env, gw, d.expOffersGw));
            };

            // clang-format off
            std::vector<TestData> const tests = {
                // Sell USD: carol, gw, bob are consumed.
                // ed buys 300USD from carol, gw, bob therefore OutstandingAmount is 300.
                //*maxAmt sendMax initDst gwOffer dstUSD outstandingUSD aliceXRP bobSell gwXRP offersGw lastGw
                { 300,    300,    0,      100,    300,   300,           700,     100,    1100, 0,       false},
                // Same as above. Gw offer location in the order book doesn't matter
                //*maxAmt sendMax initDst gwOffer dstUSD outstandingUSD aliceXRP bobSell gwXRP offersGw lastGw
                { 300,    300,    0,      100,    300,   300,           700,     100,    1100, 0,       true},
                // Sell USD: carol, bob are consumed, gw is partially consumed.
                // ed buys 200 from carol and bob and 50 from gw because gw can only issue 50
                // (300(max) - 200(carol+bob) - 50(ed)). ed buys 250 from carol, gw, bob and has 50 initially,
                // therefore OutstandingAmount is 300.
                // gw's offer is removed from the order book because it's partially consumed and the remaining
                // offer is unfunded.
                //*maxAmt sendMax initDst gwOffer dstUSD outstandingUSD aliceXRP bobSell gwXRP offersGw lastGw
                { 300,    300,    50,     100,    300,   300,           750,     100,    1050, 0,       false},
                // Same as above. Gw offer location in the order book doesn't matter.
                //*maxAmt sendMax initDst gwOffer dstUSD outstandingUSD aliceXRP bobSell gwXRP offersGw lastGw
                { 300,    300,    50,     100,    300,   300,           750,     100,    1050, 0,       true},
                // Same as above. Gw offer size doesn't matter.
                //*maxAmt sendMax initDst gwOffer dstUSD outstandingUSD aliceXRP bobSell gwXRP offersGw lastGw
                { 300,    300,    50,     200,    300,   300,           750,     100,    1050, 0,       true},
                // Sell USD: carol, gw are consumed, bob is partially consumed.
                // ed buys 200 from carol and gw and 50 form bob because of sendMax limit. bob keeps 50,
                // therefore OutstandingAmount is 300.
                //*maxAmt sendMax initDst gwOffer dstUSD outstandingUSD aliceXRP bobSell gwXRP offersGw lastGw
                { 300,    250,    0,      100,    250,   300,           750,     50,     1100, 0,       false},
                // Sell USD: carol, bob are consumed, gw is partially consumed because of sendMax limit.
                // ed buys 200 from carol and bob and 50 from gw. Therefore, OutstandingAmount is 250.
                // gw's offer remains on the order book because it's partially consumed and has more funds.
                //*maxAmt sendMax initDst gwOffer dstUSD outstandingUSD aliceXRP bobSell gwXRP offersGw lastGw
                { 300,    250,    0,      100,    250,   250,           750,     100,    1050, 1,       true},
                // Sell USD: carol, bob are consumed, gw is partially consumed because of sendMax limit, also
                // there is only 50 available to issue. ed buys 200 from carol and bob and 50 from gw, plus
                // he has initially 50, therefore OutstandingAmount is 300.
                //*maxAmt sendMax initDst gwOffer dstUSD outstandingUSD aliceXRP bobSell gwXRP offersGw lastGw
                { 300,    250,    50,     100,    300,   300,           750,     100,    1050, 0,       true},
                // Sell USD: carol, bob are consumed, gw is not consumed because there is not available funds
                // to issue. ed buys 200 from carol and bob and, plus he has initially 100,
                // therefore OutstandingAmount is 300. gw offer is removed because it's unfunded.
                //*maxAmt sendMax initDst gwOffer dstUSD outstandingUSD aliceXRP bobSell gwXRP offersGw lastGw
                { 300,    250,    100,    100,    300,   300,           800,     100,    1000, 0,       true},
            };
            // clang-format on
            for (auto const& t : tests)
                test(t);
        }

        // Cross-currency payment with BookStep as the first step.
        // BookStep limits the buy amount.
        {
            auto test = [&](int sendMax, std::uint16_t dstXRP, std::uint8_t expGwOffers) {
                Env env(*this);
                env.fund(XRP(1'000), gw, alice, carol);

                MPT const usd = MPTTester({.env = env, .issuer = gw, .maxAmt = 300});

                env(offer(carol, usd(400), XRP(400)));
                env(offer(gw, usd(100), XRP(100)));
                BEAST_EXPECT(expectOffers(env, carol, 1));
                BEAST_EXPECT(expectOffers(env, gw, 1));

                env(pay(gw, alice, XRP(500)),
                    Sendmax(usd(sendMax)),
                    Path(~XRP),
                    Txflags(tfPartialPayment | tfNoRippleDirect));

                BEAST_EXPECT(env.balance(alice) == XRP(dstXRP));
                BEAST_EXPECT(env.balance(gw, usd) == usd(-300));
                BEAST_EXPECT(env.balance(carol, usd) == usd(300));
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
        testReExecuteDirectStep(features);
        testSelfPayLowQualityOffer(features);
    }

    void
    run() override
    {
        using namespace jtx;
        auto const sa = testableAmendments();
        testLimitQuality();
        testXRPPathLoop();
        testWithFeats(sa);
        testEmptyStrand(sa);
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(FlowMPT, app, xrpl, 2);

}  // namespace xrpl::test
