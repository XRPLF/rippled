#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/PathSet.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/offer.h>
#include <test/jtx/owners.h>
#include <test/jtx/paths.h>
#include <test/jtx/pay.h>
#include <test/jtx/quality.h>
#include <test/jtx/rate.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/PaymentSandbox.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/OfferHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/paths/Flow.h>
#include <xrpl/tx/paths/detail/Steps.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace xrpl::test {

bool
getNoRippleFlag(
    jtx::Env const& env,
    jtx::Account const& src,
    jtx::Account const& dst,
    Currency const& cur)
{
    if (auto sle = env.le(keylet::line(src, dst, cur)))
    {
        auto const flag = (src.id() > dst.id()) ? lsfHighNoRipple : lsfLowNoRipple;
        return sle->isFlag(flag);
    }
    Throw<std::runtime_error>("No line in getTrustFlag");
    return false;  // silence warning
}

struct Flow_test : public beast::unit_test::Suite
{
    void
    testDirectStep(FeatureBitset features)
    {
        testcase("Direct Step");

        using namespace jtx;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const dan = Account("dan");
        auto const erin = Account("erin");
        auto const usda = alice["USD"];
        auto const usdb = bob["USD"];
        auto const usdc = carol["USD"];
        auto const usdd = dan["USD"];
        auto const gw = Account("gw");
        auto const usd = gw["USD"];
        {
            // Pay USD, trivial path
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(usd(1000), alice, bob);
            env(pay(gw, alice, usd(100)));
            env(pay(alice, bob, usd(10)), Paths(usd));
            env.require(Balance(bob, usd(10)));
        }
        {
            // XRP transfer
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob);
            env.close();
            env(pay(alice, bob, XRP(100)));
            env.require(Balance(bob, XRP(10000 + 100)));
            env.require(Balance(alice, xrpMinusFee(env, 10000 - 100)));
        }
        {
            // Partial payments
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(usd(1000), alice, bob);
            env(pay(gw, alice, usd(100)));
            env(pay(alice, bob, usd(110)), Paths(usd), Ter(tecPATH_PARTIAL));
            env.require(Balance(bob, usd(0)));
            env(pay(alice, bob, usd(110)), Paths(usd), Txflags(tfPartialPayment));
            env.require(Balance(bob, usd(100)));
        }
        {
            // Pay by rippling through accounts, use path finder
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, dan);
            env.close();
            env.trust(usda(10), bob);
            env.trust(usdb(10), carol);
            env.trust(usdc(10), dan);
            env(pay(alice, dan, usdc(10)), Paths(usda));
            env.require(Balance(bob, usda(10)), Balance(carol, usdb(10)), Balance(dan, usdc(10)));
        }
        {
            // Pay by rippling through accounts, specify path
            // and charge a transfer fee
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, dan);
            env.close();
            env.trust(usda(10), bob);
            env.trust(usdb(10), alice, carol);
            env.trust(usdc(10), dan);
            env(rate(bob, 1.1));

            // alice will redeem to bob; a transfer fee will be charged
            env(pay(bob, alice, usdb(6)));
            env(pay(alice, dan, usdc(5)),
                Path(bob, carol),
                Sendmax(usda(6)),
                Txflags(tfNoRippleDirect));
            env.require(Balance(dan, usdc(5)));
            env.require(Balance(alice, usdb(0.5)));
        }
        {
            // Pay by rippling through accounts, specify path and transfer fee
            // Test that the transfer fee is not charged when alice issues
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, dan);
            env.close();
            env.trust(usda(10), bob);
            env.trust(usdb(10), alice, carol);
            env.trust(usdc(10), dan);
            env(rate(bob, 1.1));

            env(pay(alice, dan, usdc(5)),
                Path(bob, carol),
                Sendmax(usda(6)),
                Txflags(tfNoRippleDirect));
            env.require(Balance(dan, usdc(5)));
            env.require(Balance(bob, usda(5)));
        }
        {
            // test best quality path is taken
            // Paths: A->B->D->E ; A->C->D->E
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, dan, erin);
            env.close();
            env.trust(usda(10), bob, carol);
            env.trust(usdb(10), dan);
            env.trust(usdc(10), alice, dan);
            env.trust(usdd(20), erin);
            env(rate(bob, 1));
            env(rate(carol, 1.1));

            // Pay alice so she redeems to carol and a transfer fee is charged
            env(pay(carol, alice, usdc(10)));
            env(pay(alice, erin, usdd(5)),
                Path(carol, dan),
                Path(bob, dan),
                Txflags(tfNoRippleDirect));

            env.require(Balance(erin, usdd(5)));
            env.require(Balance(dan, usdb(5)));
            env.require(Balance(dan, usdc(0)));
        }
        {
            // Limit quality
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol);
            env.close();
            env.trust(usda(10), bob);
            env.trust(usdb(10), carol);

            env(pay(alice, carol, usdb(5)),
                Sendmax(usda(4)),
                Txflags(tfLimitQuality | tfPartialPayment),
                Ter(tecPATH_DRY));
            env.require(Balance(carol, usdb(0)));

            env(pay(alice, carol, usdb(5)), Sendmax(usda(4)), Txflags(tfPartialPayment));
            env.require(Balance(carol, usdb(4)));
        }
    }

    void
    testLineQuality(FeatureBitset features)
    {
        testcase("Line Quality");

        using namespace jtx;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const dan = Account("dan");
        auto const usda = alice["USD"];
        auto const usdb = bob["USD"];
        auto const usdc = carol["USD"];
        auto const usdd = dan["USD"];

        //   Dan -> Bob -> Alice -> Carol; vary bobDanQIn and bobAliceQOut
        for (auto bobDanQIn : {80, 100, 120})
        {
            for (auto bobAliceQOut : {80, 100, 120})
            {
                Env env(*this, features);
                env.fund(XRP(10000), alice, bob, carol, dan);
                env.close();
                env(trust(bob, usdd(100)), QualityInPercent(bobDanQIn));
                env(trust(bob, usda(100)), QualityOutPercent(bobAliceQOut));
                env(trust(carol, usda(100)));

                env(pay(alice, bob, usda(100)));
                env.require(Balance(bob, usda(100)));
                env(pay(dan, carol, usda(10)),
                    Path(bob),
                    Sendmax(usdd(100)),
                    Txflags(tfNoRippleDirect));
                env.require(Balance(bob, usda(90)));
                if (bobAliceQOut > bobDanQIn)
                {
                    env.require(
                        Balance(bob, usdd(10.0 * double(bobAliceQOut) / double(bobDanQIn))));
                }
                else
                {
                    env.require(Balance(bob, usdd(10)));
                }
                env.require(Balance(carol, usda(10)));
            }
        }

        // bob -> alice -> carol; vary carolAliceQIn
        for (auto carolAliceQIn : {80, 100, 120})
        {
            Env env(*this, features);
            env.fund(XRP(10000), alice, bob, carol);
            env.close();

            env(trust(bob, usda(10)));
            env(trust(carol, usda(10)), QualityInPercent(carolAliceQIn));

            env(pay(alice, bob, usda(10)));
            env.require(Balance(bob, usda(10)));
            env(pay(bob, carol, usda(5)), Sendmax(usda(10)));
            auto const effectiveQ = carolAliceQIn > 100 ? 1.0 : carolAliceQIn / 100.0;
            env.require(Balance(bob, usda(10.0 - (5.0 / effectiveQ))));
        }

        // bob -> alice -> carol; bobAliceQOut varies.
        for (auto bobAliceQOut : {80, 100, 120})
        {
            Env env(*this, features);
            env.fund(XRP(10000), alice, bob, carol);
            env.close();
            env(trust(bob, usda(10)), QualityOutPercent(bobAliceQOut));
            env(trust(carol, usda(10)));

            env(pay(alice, bob, usda(10)));
            env.require(Balance(bob, usda(10)));
            env(pay(bob, carol, usda(5)), Sendmax(usda(5)));
            env.require(Balance(carol, usda(5)));
            env.require(Balance(bob, usda(10 - 5)));
        }
    }

    void
    testBookStep(FeatureBitset features)
    {
        testcase("Book Step");

        using namespace jtx;

        auto const gw = Account("gateway");
        auto const usd = gw["USD"];
        auto const btc = gw["BTC"];
        auto const eur = gw["EUR"];
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");

        {
            // simple IOU/IOU offer
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, gw);
            env.close();
            env.trust(usd(1000), alice, bob, carol);
            env.trust(btc(1000), alice, bob, carol);

            env(pay(gw, alice, btc(50)));
            env(pay(gw, bob, usd(50)));

            env(offer(bob, btc(50), usd(50)));

            env(pay(alice, carol, usd(50)), Path(~usd), Sendmax(btc(50)));

            env.require(Balance(alice, btc(0)));
            env.require(Balance(bob, btc(50)));
            env.require(Balance(bob, usd(0)));
            env.require(Balance(carol, usd(50)));
            BEAST_EXPECT(!isOffer(env, bob, btc(50), usd(50)));
        }
        {
            // simple IOU/XRP XRP/IOU offer
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, gw);
            env.close();
            env.trust(usd(1000), alice, bob, carol);
            env.trust(btc(1000), alice, bob, carol);

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
        }
        {
            // simple XRP -> USD through offer and sendmax
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, gw);
            env.close();
            env.trust(usd(1000), alice, bob, carol);
            env.trust(btc(1000), alice, bob, carol);

            env(pay(gw, bob, usd(50)));

            env(offer(bob, XRP(50), usd(50)));

            env(pay(alice, carol, usd(50)), Path(~usd), Sendmax(XRP(50)));

            env.require(Balance(alice, xrpMinusFee(env, 10000 - 50)));
            env.require(Balance(bob, xrpMinusFee(env, 10000 + 50)));
            env.require(Balance(bob, usd(0)));
            env.require(Balance(carol, usd(50)));
            BEAST_EXPECT(!isOffer(env, bob, XRP(50), usd(50)));
        }
        {
            // simple USD -> XRP through offer and sendmax
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, gw);
            env.close();
            env.trust(usd(1000), alice, bob, carol);
            env.trust(btc(1000), alice, bob, carol);

            env(pay(gw, alice, usd(50)));

            env(offer(bob, usd(50), XRP(50)));

            env(pay(alice, carol, XRP(50)), Path(~XRP), Sendmax(usd(50)));

            env.require(Balance(alice, usd(0)));
            env.require(Balance(bob, xrpMinusFee(env, 10000 - 50)));
            env.require(Balance(bob, usd(50)));
            env.require(Balance(carol, XRP(10000 + 50)));
            BEAST_EXPECT(!isOffer(env, bob, usd(50), XRP(50)));
        }
        {
            // test unfunded offers are removed when payment succeeds
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, gw);
            env.close();
            env.trust(usd(1000), alice, bob, carol);
            env.trust(btc(1000), alice, bob, carol);
            env.trust(eur(1000), alice, bob, carol);

            env(pay(gw, alice, btc(60)));
            env(pay(gw, bob, usd(50)));
            env(pay(gw, bob, eur(50)));

            env(offer(bob, btc(50), usd(50)));
            env(offer(bob, btc(40), eur(50)));
            env(offer(bob, eur(50), usd(50)));

            // unfund offer
            env(pay(bob, gw, eur(50)));
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
        }
        {
            // test unfunded offers are returned when the payment fails.
            // bob makes two offers: a funded 50 USD for 50 BTC and an unfunded
            // 50 EUR for 60 BTC. alice pays carol 61 USD with 61 BTC. alice
            // only has 60 BTC, so the payment will fail. The payment uses two
            // paths: one through bob's funded offer and one through his
            // unfunded offer. When the payment fails `flow` should return the
            // unfunded offer. This test is intentionally similar to the one
            // that removes unfunded offers when the payment succeeds.
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, gw);
            env.close();
            env.trust(usd(1000), alice, bob, carol);
            env.trust(btc(1000), alice, bob, carol);
            env.trust(eur(1000), alice, bob, carol);

            env(pay(gw, alice, btc(60)));
            env(pay(gw, bob, usd(60)));
            env(pay(gw, bob, eur(50)));
            env(pay(gw, carol, eur(1)));

            env(offer(bob, btc(50), usd(50)));
            env(offer(bob, btc(60), eur(50)));
            env(offer(carol, btc(1000), eur(1)));
            env(offer(bob, eur(50), usd(50)));

            // unfund offer
            env(pay(bob, gw, eur(50)));
            BEAST_EXPECT(isOffer(env, bob, btc(50), usd(50)));
            BEAST_EXPECT(isOffer(env, bob, btc(60), eur(50)));
            BEAST_EXPECT(isOffer(env, carol, btc(1000), eur(1)));

            auto flowJournal = env.app().getJournal("Flow");
            auto const flowResult = [&] {
                STAmount const deliver(usd(51));
                STAmount smax(btc(61));
                PaymentSandbox sb(env.current().get(), TapNone);
                STPathSet paths;
                auto ipe = [](Issue const& iss) {
                    return STPathElement(
                        STPathElement::TypeCurrency | STPathElement::TypeIssuer,
                        xrpAccount(),
                        iss.currency,
                        iss.account);
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

            // used in payment, but since payment failed should be untouched
            BEAST_EXPECT(isOffer(env, bob, btc(50), usd(50)));
            BEAST_EXPECT(isOffer(env, carol, btc(1000), eur(1)));
            // found unfunded
            BEAST_EXPECT(!isOffer(env, bob, btc(60), eur(50)));
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
            env.fund(XRP(10000), alice, bob, carol, gw);
            env.close();

            env.trust(usd(1000), alice, bob, carol);
            env.trust(eur(1000), alice, bob, carol);

            env(pay(gw, alice, usd(1000)));
            env(pay(gw, bob, eur(1000)));

            Keylet const bobUsdOffer = keylet::offer(bob, env.seq(bob));
            env(offer(bob, usd(1), drops(2)), Txflags(tfPassive));
            env(offer(bob, drops(1), eur(1000)), Txflags(tfPassive));

            bool const reducedOffersV2 = features[fixReducedOffersV2];

            // With reducedOffersV2, it is not allowed to accept less than
            // USD(0.5) of bob's USD offer.  If we provide 1 drop for less
            // than USD(0.5), then the remaining fractional offer would
            // block the order book.
            TER const expectedTER = reducedOffersV2 ? TER(tecPATH_DRY) : TER(tesSUCCESS);
            env(pay(alice, carol, eur(1)),
                Path(~XRP, ~eur),
                Sendmax(usd(0.4)),
                Txflags(tfNoRippleDirect | tfPartialPayment),
                Ter(expectedTER));

            if (!reducedOffersV2)
            {
                env.require(Balance(carol, eur(1)));
                env.require(Balance(bob, usd(0.4)));
                env.require(Balance(bob, eur(999)));

                // Show that bob's USD offer is now a blocker.
                SLE::const_pointer const usdOffer = env.le(bobUsdOffer);
                if (BEAST_EXPECT(usdOffer))
                {
                    std::uint64_t const bookRate = [&usdOffer]() {
                        // Extract the least significant 64 bits from the
                        // book page.  That's where the quality is stored.
                        std::string bookDirStr = to_string(usdOffer->at(sfBookDirectory));
                        bookDirStr.erase(0, 48);
                        return std::stoull(bookDirStr, nullptr, 16);
                    }();
                    std::uint64_t const actualRate =
                        getRate(usdOffer->at(sfTakerGets), usdOffer->at(sfTakerPays));

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
        auto const usd = gw["USD"];
        auto const btc = gw["BTC"];
        auto const eur = gw["EUR"];
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");

        // Offer where the owner is also the issuer, sender pays fee
        Env env(*this, features);

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env(rate(gw, 1.25));
        env.trust(usd(1000), alice, bob);
        env(offer(gw, XRP(125), usd(125)));
        env(pay(alice, bob, usd(100)), Sendmax(XRP(200)));
        env.require(Balance(alice, xrpMinusFee(env, 10000 - 125)), Balance(bob, usd(100)));
    }

    void
    testFalseDry(FeatureBitset features)
    {
        testcase("falseDryChanges");

        using namespace jtx;

        auto const gw = Account("gateway");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");

        Env env(*this, features);

        env.fund(XRP(10000), alice, carol, gw);
        env.fund(reserve(env, 5), bob);
        env.close();
        env.trust(usd(1000), alice, bob, carol);
        env.trust(eur(1000), alice, bob, carol);

        env(pay(gw, alice, eur(50)));
        env(pay(gw, bob, usd(50)));

        // Bob has _just_ slightly less than 50 xrp available
        // If his owner count changes, he will have more liquidity.
        // This is one error case to test (when Flow is used).
        // Computing the incoming xrp to the XRP/USD offer will require two
        // recursive calls to the EUR/XRP offer. The second call will return
        // tecPATH_DRY, but the entire path should not be marked as dry. This
        // is the second error case to test (when flowV1 is used).
        env(offer(bob, eur(50), XRP(50)));
        env(offer(bob, XRP(50), usd(50)));

        env(pay(alice, carol, usd(1000000)),
            Path(~XRP, ~usd),
            Sendmax(eur(500)),
            Txflags(tfNoRippleDirect | tfPartialPayment));

        auto const carolUSD = env.balance(carol, usd).value();
        BEAST_EXPECT(carolUSD > usd(0) && carolUSD < usd(50));
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
        auto const usd = gw["USD"];
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");

        {
            Env env(*this);

            env.fund(XRP(10000), alice, bob, carol, gw);
            env.close();

            env.trust(usd(100), alice, bob, carol);
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
        auto const usd = gw1["USD"];
        auto const eur = gw2["EUR"];

        Env env(*this, features);

        env.fund(XRP(1000000), gw1, gw2);
        env.close();

        // The fee that's charged for transactions.
        auto const f = env.current()->fees().base;

        env.fund(reserve(env, 3) + f * 4, alice);
        env.close();

        env(trust(alice, usd(2000)));
        env(trust(alice, eur(2000)));
        env.close();

        env(pay(gw1, alice, usd(1)));
        env(pay(gw2, alice, eur(1000)));
        env.close();

        env(offer(alice, usd(500), eur(600)));
        env.close();

        env.require(Owners(alice, 3));
        env.require(Balance(alice, usd(1)));
        env.require(Balance(alice, eur(1000)));

        auto aliceOffers = offersOnAccount(env, alice);
        BEAST_EXPECT(aliceOffers.size() == 1);
        for (auto const& offerPtr : aliceOffers)
        {
            auto const offer = *offerPtr;
            BEAST_EXPECT(offer[sfLedgerEntryType] == ltOFFER);
            BEAST_EXPECT(offer[sfTakerGets] == eur(600));
            BEAST_EXPECT(offer[sfTakerPays] == usd(500));
        }

        env(pay(alice, alice, eur(600)), Sendmax(usd(500)), Txflags(tfPartialPayment));
        env.close();

        env.require(Owners(alice, 3));
        env.require(Balance(alice, usd(1)));
        env.require(Balance(alice, eur(1000)));
        aliceOffers = offersOnAccount(env, alice);
        BEAST_EXPECT(aliceOffers.size() == 1);
        for (auto const& offerPtr : aliceOffers)
        {
            auto const offer = *offerPtr;
            BEAST_EXPECT(offer[sfLedgerEntryType] == ltOFFER);
            BEAST_EXPECT(offer[sfTakerGets] == eur(598.8));
            BEAST_EXPECT(offer[sfTakerPays] == usd(499));
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
        auto const usd = gw1["USD"];
        auto const eur = gw2["EUR"];

        Env env(*this, features);

        env.fund(XRP(1000000), gw1, gw2);
        env.close();

        // The fee that's charged for transactions.
        auto const f = env.current()->fees().base;

        env.fund(reserve(env, 3) + f * 4, alice);
        env.close();

        env(trust(alice, usd(506)));
        env(trust(alice, eur(606)));
        env.close();

        env(pay(gw1, alice, usd(500)));
        env(pay(gw2, alice, eur(600)));
        env.close();

        env(offer(alice, usd(500), eur(600)));
        env.close();

        env.require(Owners(alice, 3));
        env.require(Balance(alice, usd(500)));
        env.require(Balance(alice, eur(600)));

        auto aliceOffers = offersOnAccount(env, alice);
        BEAST_EXPECT(aliceOffers.size() == 1);
        for (auto const& offerPtr : aliceOffers)
        {
            auto const offer = *offerPtr;
            BEAST_EXPECT(offer[sfLedgerEntryType] == ltOFFER);
            BEAST_EXPECT(offer[sfTakerGets] == eur(600));
            BEAST_EXPECT(offer[sfTakerPays] == usd(500));
        }

        env(pay(alice, alice, eur(60)), Sendmax(usd(50)), Txflags(tfPartialPayment));
        env.close();

        env.require(Owners(alice, 3));
        env.require(Balance(alice, usd(500)));
        env.require(Balance(alice, eur(600)));
        aliceOffers = offersOnAccount(env, alice);
        BEAST_EXPECT(aliceOffers.size() == 1);
        for (auto const& offerPtr : aliceOffers)
        {
            auto const offer = *offerPtr;
            BEAST_EXPECT(offer[sfLedgerEntryType] == ltOFFER);
            BEAST_EXPECT(offer[sfTakerGets] == eur(594));
            BEAST_EXPECT(offer[sfTakerPays] == usd(495));
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
        auto const usd = gw["USD"];

        env.fund(XRP(10000), alice, gw);
        env.close();
        env(trust(alice, usd(20)));
        env(pay(gw, alice, usd(10)));
        env(offer(alice, XRP(50000), usd(10)));

        // Consuming the offer changes the owner count, which could also cause
        // liquidity to decrease in the forward pass
        auto const toSend = consumeOffer ? usd(10) : usd(9);
        env(pay(alice, alice, toSend),
            Path(~usd),
            Sendmax(XRP(20000)),
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
            auto const usd = gw["USD"];

            env.fund(XRP(100000), alice, bob, gw);
            env.close();
            env(trust(bob, usd(20)));

            STAmount const tinyAmt1{usd, 9000000000000000ll, -17, false, STAmount::Unchecked{}};
            STAmount const tinyAmt3{usd, 9000000000000003ll, -17, false, STAmount::Unchecked{}};

            env(offer(gw, drops(9000000000), tinyAmt3));
            env(pay(alice, bob, tinyAmt1),
                Path(~usd),
                Sendmax(drops(9000000000)),
                Txflags(tfNoRippleDirect));

            BEAST_EXPECT(!isOffer(env, gw, XRP(0), usd(0)));
        }
        {
            // Test forward
            Env env(*this, features);

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            auto const usd = gw["USD"];

            env.fund(XRP(100000), alice, bob, gw);
            env.close();
            env(trust(alice, usd(20)));

            STAmount const tinyAmt1{usd, 9000000000000000ll, -17, false, STAmount::Unchecked{}};
            STAmount const tinyAmt3{usd, 9000000000000003ll, -17, false, STAmount::Unchecked{}};

            env(pay(gw, alice, tinyAmt1));

            env(offer(gw, tinyAmt3, drops(9000000000)));
            env(pay(alice, bob, drops(9000000000)),
                Path(~XRP),
                Sendmax(usd(1)),
                Txflags(tfNoRippleDirect));

            BEAST_EXPECT(!isOffer(env, gw, usd(0), XRP(0)));
        }
    }

    void
    testReExecuteDirectStep(FeatureBitset features)
    {
        testcase("ReExecuteDirectStep");

        using namespace jtx;
        Env env(*this, features);

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];
        auto const usdC = usd.currency;

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env(trust(alice, usd(100)));
        env.close();

        BEAST_EXPECT(!getNoRippleFlag(env, gw, alice, usdC));

        env(
            pay(gw,
                alice,
                // 12.55....
                STAmount{usd, std::uint64_t(1255555555555555ull), -14, false}));

        env(offer(
            gw,
            // 5.0...
            STAmount{usd, std::uint64_t(5000000000000000ull), -15, false},
            XRP(1000)));

        env(offer(
            gw,
            // .555...
            STAmount{usd, std::uint64_t(5555555555555555ull), -16, false},
            XRP(10)));

        env(offer(
            gw,
            // 4.44....
            STAmount{usd, std::uint64_t(4444444444444444ull), -15, false},
            XRP(.1)));

        env(offer(
            alice,
            // 17
            STAmount{usd, std::uint64_t(1700000000000000ull), -14, false},
            XRP(.001)));

        env(pay(alice, bob, XRP(10000)),
            Path(~XRP),
            Sendmax(usd(100)),
            Txflags(tfPartialPayment | tfNoRippleDirect));
    }

    void
    testRIPD1443()
    {
        testcase("ripd1443");

        using namespace jtx;
        Env env(*this);
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account("gw");

        env.fund(XRP(100000000), alice, noripple(bob), carol, gw);
        env.close();
        env.trust(gw["USD"](10000), alice, carol);
        env(trust(bob, gw["USD"](10000), tfSetNoRipple));
        env.trust(gw["USD"](10000), bob);
        env.close();

        // set no ripple between bob and the gateway

        env(pay(gw, alice, gw["USD"](1000)));
        env.close();

        env(offer(alice, bob["USD"](1000), XRP(1)));
        env.close();

        env(pay(alice, alice, XRP(1)),
            Path(gw, bob, ~XRP),
            Sendmax(gw["USD"](1000)),
            Txflags(tfNoRippleDirect),
            Ter(tecPATH_DRY));
        env.close();

        env.trust(bob["USD"](10000), alice);
        env(pay(bob, alice, bob["USD"](1000)));

        env(offer(alice, XRP(1000), bob["USD"](1000)));
        env.close();

        env(pay(carol, carol, gw["USD"](1000)),
            Path(~bob["USD"], gw),
            Sendmax(XRP(100000)),
            Txflags(tfNoRippleDirect),
            Ter(tecPATH_DRY));
        env.close();

        pass();
    }

    void
    testRIPD1449()
    {
        testcase("ripd1449");

        using namespace jtx;
        Env env(*this);

        // pay alice -> xrp -> USD/bob -> bob -> gw -> alice
        // set no ripple on bob's side of the bob/gw trust line
        // carol has the bob/USD and makes an offer, bob has USD/gw

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];

        env.fund(XRP(100000000), alice, bob, carol, gw);
        env.close();
        env.trust(usd(10000), alice, carol);
        env(trust(bob, usd(10000), tfSetNoRipple));
        env.trust(usd(10000), bob);
        env.trust(bob["USD"](10000), carol);
        env.close();

        env(pay(bob, carol, bob["USD"](1000)));
        env(pay(gw, bob, usd(1000)));
        env.close();

        env(offer(carol, XRP(1), bob["USD"](1000)));
        env.close();

        env(pay(alice, alice, usd(1000)),
            Path(~bob["USD"], bob, gw),
            Sendmax(XRP(1)),
            Txflags(tfNoRippleDirect),
            Ter(tecPATH_DRY));
        env.close();
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
        auto const ctb = gw["CTB"];

        auto const fee = env.current()->fees().base;
        env.fund(reserve(env, 2) + drops(9999640) + fee, ann);
        env.fund(reserve(env, 2) + fee * 4, gw);
        env.close();

        env(rate(gw, 1.002));
        env(trust(ann, ctb(10)));
        env.close();

        env(pay(gw, ann, ctb(2.856)));
        env.close();

        env(offer(ann, drops(365611702030), ctb(5.713)));
        env.close();

        // This payment caused the assert.
        env(pay(ann, ann, ctb(0.687)), Sendmax(drops(20000000000)), Txflags(tfPartialPayment));
    }

    void
    testEmptyStrand(FeatureBitset features)
    {
        testcase("Empty Strand");
        using namespace jtx;

        auto const alice = Account("alice");

        Env env(*this, features);

        env.fund(XRP(10000), alice);
        env.close();

        env(pay(alice, alice, alice["USD"](100)), Path(~alice["USD"]), Ter(temBAD_PATH));
    }

    void
    testXRPPathLoop()
    {
        testcase("Circular XRP");

        using namespace jtx;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        {
            // Payment path starting with XRP
            Env env(*this, testableAmendments());
            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(usd(1000), alice, bob);
            env.trust(eur(1000), alice, bob);
            env.close();
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

            pass();
        }
        {
            // Payment path ending with XRP
            Env env(*this);
            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(usd(1000), alice, bob);
            env.trust(eur(1000), alice, bob);
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
        }
        {
            // Payment where loop is formed in the middle of the path, not on an
            // endpoint
            auto const jpy = gw["JPY"];
            Env env(*this);
            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(usd(1000), alice, bob);
            env.trust(eur(1000), alice, bob);
            env.trust(jpy(1000), alice, bob);
            env.close();
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
        env.close();

        // alice creates a ticket for the payment.
        std::uint32_t const ticketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, 1));

        // Make a payment using the ticket.
        env(pay(alice, bob, XRP(1000)), ticket::Use(ticketSeq));
        env.close();
        env.require(Balance(bob, XRP(1000)));
        env.require(Balance(alice, XRP(9000) - (env.current()->fees().base * 2)));
    }

    void
    testWithFeats(FeatureBitset features)
    {
        using namespace jtx;
        FeatureBitset const reducedOffersV2(fixReducedOffersV2);

        testLineQuality(features);
        testFalseDry(features);
        testBookStep(features - reducedOffersV2);
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
        testTicketPay(features);
    }

    void
    run() override
    {
        testLimitQuality();
        testXRPPathLoop();
        testRIPD1443();
        testRIPD1449();

        using namespace jtx;
        auto const sa = testableAmendments();
        testWithFeats(sa - featurePermissionedDEX);
        testWithFeats(sa);
        testEmptyStrand(sa);
    }
};

struct Flow_manual_test : public Flow_test
{
    void
    run() override
    {
        using namespace jtx;
        auto const all = testableAmendments();
        FeatureBitset const permDex{featurePermissionedDEX};

        testWithFeats(all - permDex);
        testWithFeats(all);

        testEmptyStrand(all - permDex);
        testEmptyStrand(all);
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(Flow, app, xrpl, 2);
BEAST_DEFINE_TESTSUITE_MANUAL_PRIO(Flow_manual, app, xrpl, 4);

}  // namespace xrpl::test
