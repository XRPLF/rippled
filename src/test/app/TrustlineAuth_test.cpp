//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2025 Ripple Labs Inc.

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
#include <test/jtx/AMMTest.h>

#include <xrpld/app/misc/AMMHelpers.h>
#include <xrpld/app/misc/AMMUtils.h>
#include <xrpld/app/paths/AMMContext.h>
#include <xrpld/app/paths/AMMOffer.h>
#include <xrpld/app/tx/detail/AMMBid.h>
#include <xrpld/ledger/ApplyViewImpl.h>
#include <xrpld/ledger/View.h>
#include <xrpld/rpc/RPCHandler.h>
#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/basics/Number.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/resource/Fees.h>

#include <boost/regex.hpp>

#include <chrono>
#include <tuple>
#include <utility>
#include <vector>
namespace ripple {
namespace test {
using namespace jtx;

class TrustlineAuth_test : public jtx::AMMTest
{
    [[nodiscard]] bool
    offerExists(Env const& env, Account const& account, std::uint32_t offerSeq)
    {
        return static_cast<bool>(env.le(keylet::offer(account.id(), offerSeq)));
    }

    [[nodiscard]] bool
    checkOffer(
        Env const& env,
        Account const& account,
        std::uint32_t offerSeq,
        STAmount const& takerPays,
        STAmount const& takerGets)
    {
        auto const sle = env.le(keylet::offer(account.id(), offerSeq));
        if (!sle)
            return false;
        if (sle->getFieldAmount(sfTakerGets) != takerGets)
            return false;
        if (sle->getFieldAmount(sfTakerPays) != takerPays)
            return false;
        return true;
    }

    Issue
    setup(Env& env)
    {
        env.fund(XRP(1000), gw, alice, carol, bob);
        env(fset(gw, asfRequireAuth));
        env.close();

        // gateway authorizes alice
        auto authAndFund = [&](Account const& account,
                               std::string const currency) {
            env(trust(gw, account[currency](100'000)), txflags(tfSetfAuth));
            env(trust(account, gw[currency](100'000)));
            env.close();
            env(pay(gw, account, gw[currency](30'000)));
            env.close();
        };

        // carol has BTC line but not USD line
        // bob has USD line but not BTC line
        // alice has both USD and BTC line
        authAndFund(alice, "BTC");
        authAndFund(alice, "USD");
        authAndFund(bob, "BTC");
        authAndFund(carol, "USD");

        AMM ammAlice(env, alice, USD(20'000), BTC(10'000));

        // bob single side deposits with BTC
        ammAlice.deposit(bob, BTC(1000));

        // carol single side deposits with USD
        ammAlice.deposit(carol, USD(2000));

        // increase limit for lptoken lines so that they can transfer lptokens
        // to each other
        auto const lpIssue = ammAlice.lptIssue();
        env.trust(STAmount{lpIssue, 50000000}, alice);
        env.trust(STAmount{lpIssue, 50000000}, bob);
        env.trust(STAmount{lpIssue, 50000000}, carol);
        env.close();

        env.enableFeature(featureAMMClawback);
        env.close();

        return lpIssue;
    }

    void
    testLPTokenDirectStep(FeatureBitset features)
    {
        testcase("LPToken direct step");

        using namespace jtx;

        // disable AMMClawback to allow single side deposit without owning one
        // of the assets
        Env env(*this, features - featureAMMClawback - fixEnforceTrustlineAuth);
        auto const lpIssue = setup(env);

        // re-enable the amendment if the test run has enabled it
        // originally
        if (features[fixEnforceTrustlineAuth])
        {
            env.enableFeature(fixEnforceTrustlineAuth);
            env.close();
        }

        // transfer LPToken between alice, bob and carol and validate expected
        // result
        auto executeLPTokenPayments = [&](TER const code) {
            env(pay(carol, alice, STAmount{lpIssue, 1}), ter(code));
            env.close();
            env(pay(alice, carol, STAmount{lpIssue, 1}), ter(code));
            env.close();
            env(pay(bob, alice, STAmount{lpIssue, 1}), ter(code));
            env.close();
            env(pay(alice, bob, STAmount{lpIssue, 1}), ter(code));
            env.close();
            env(pay(bob, carol, STAmount{lpIssue, 1}), ter(code));
            env.close();
            env(pay(carol, bob, STAmount{lpIssue, 1}), ter(code));
            env.close();
        };

        // We are going to test the behavior when bob and carol tries to send
        // lptoken pre/post amendment.
        //
        // With fixEnforceTrustlineAuth amendment, carol and bob cannot receive
        // nor send lptoken if they doesn't have one of the trustlines
        if (features[fixEnforceTrustlineAuth])
        {
            executeLPTokenPayments(tecNO_LINE);
        }
        // without fixEnforceTrustlineAuth, carol and bob can still receive and
        // send lptoken freely even tho they don't have trustlines for one of
        // the assets
        else
        {
            executeLPTokenPayments(tesSUCCESS);
        }

        // bob and carol create trustlines for the assets that they are
        // missing.
        // HOWEVER, they are still unauthorized!
        env(trust(bob, gw["USD"](100'000)));
        env(trust(carol, gw["BTC"](100'000)));
        env.close();

        // With fixEnforceTrustlineAuth amendment, carol and bob cannot
        // receive nor send lptoken if they have unauthorized trustlines
        if (features[fixEnforceTrustlineAuth])
        {
            executeLPTokenPayments(tecNO_AUTH);
        }
        // without fixEnforceTrustlineAuth, carol and bob can still receive
        // and send lptoken freely even tho they don't have authorized
        // trustlines
        else
        {
            executeLPTokenPayments(tesSUCCESS);
        }

        // gateway authorizes bob and carol for their respective trustlines
        env(trust(gw, bob["USD"](100'000)), txflags(tfSetfAuth));
        env.close();
        env(trust(gw, carol["BTC"](100'000)), txflags(tfSetfAuth));
        env.close();

        // bob and carol can now transfer lptoken freely since they have
        // authorized lines for both assets
        executeLPTokenPayments(tesSUCCESS);
    }

    void
    testLPTokenBookStep(FeatureBitset features)
    {
        testcase("LPToken book step");

        using namespace jtx;

        // temporarily disable AMMClawback to allow single side deposit without
        // owning one of the assets
        // temporarily disable fixEnforceTrustlineAuth
        // to allow creation of unauthorized offers
        Env env(*this, features - featureAMMClawback - fixEnforceTrustlineAuth);
        auto const lpIssue = setup(env);

        // re-enable the amendment if the test run has enabled it
        // originally
        if (features[fixEnforceTrustlineAuth])
        {
            env.enableFeature(fixEnforceTrustlineAuth);
            env.close();
        }

        auto bobOfferSeq = env.seq(bob);
        auto carolOfferSeq = env.seq(carol);

        auto const createOffers = [&]() {
            {
                // disable the amendment temporarily to create offers
                env.disableFeature(fixEnforceTrustlineAuth);
                env.close();

                carolOfferSeq = env.seq(carol);
                env(offer(carol, XRP(10), STAmount{lpIssue, 10}),
                    txflags(tfPassive));
                env.close();
                BEAST_EXPECT(checkOffer(
                    env, carol, carolOfferSeq, XRP(10), STAmount{lpIssue, 10}));

                bobOfferSeq = env.seq(bob);
                env(offer(bob, STAmount{lpIssue, 10}, XRP(5)),
                    txflags(tfPassive));
                env.close();
                BEAST_EXPECT(checkOffer(
                    env, bob, bobOfferSeq, STAmount{lpIssue, 10}, XRP(5)));

                // re-enable the amendment if the test run has enabled it
                // originally
                if (features[fixEnforceTrustlineAuth])
                {
                    env.enableFeature(fixEnforceTrustlineAuth);
                    env.close();
                }
            }
        };

        auto const checkOfferCrossing = [&]() {
            if (!features[fixEnforceTrustlineAuth])
            {
                // Without fixEnforceTrustlineAuth, offers with
                // unauthorized assets can still be crossed in the order
                // book

                auto aliceOfferSeq{env.seq(alice)};
                env(offer(alice, STAmount{lpIssue, 10}, XRP(10)));
                env.close();

                // alice's offer crossed with carol's even though carol's
                // offer is unfunded
                BEAST_EXPECT(!offerExists(env, carol, carolOfferSeq));
                BEAST_EXPECT(!offerExists(env, alice, aliceOfferSeq));

                aliceOfferSeq = env.seq(alice);
                env(offer(alice, XRP(5), STAmount{lpIssue, 10}));
                env.close();

                // alice's offer crossed with bob's even though bob's offer
                // is unfunded
                BEAST_EXPECT(!offerExists(env, bob, bobOfferSeq));
                BEAST_EXPECT(!offerExists(env, alice, aliceOfferSeq));
            }
            else
            {
                // with fixEnforceTrustlineAuth, offers with unauthorized
                // assets are considered to be unfunded and cannot be
                // crossed

                BEAST_EXPECT(checkOffer(
                    env, carol, carolOfferSeq, XRP(10), STAmount{lpIssue, 10}));

                auto aliceOfferSeq{env.seq(alice)};
                env(offer(alice, STAmount{lpIssue, 10}, XRP(10)));
                env.close();

                // carol's unfunded offer is removed
                BEAST_EXPECT(!offerExists(env, carol, carolOfferSeq));
                BEAST_EXPECT(checkOffer(
                    env, alice, aliceOfferSeq, STAmount{lpIssue, 10}, XRP(10)));

                BEAST_EXPECT(checkOffer(
                    env, bob, bobOfferSeq, STAmount{lpIssue, 10}, XRP(5)));

                aliceOfferSeq = env.seq(alice);
                env(offer(alice, XRP(5), STAmount{lpIssue, 10}));
                env.close();

                // bob's unfunded offer is removed
                BEAST_EXPECT(!offerExists(env, bob, bobOfferSeq));
                BEAST_EXPECT(checkOffer(
                    env, alice, aliceOfferSeq, XRP(5), STAmount{lpIssue, 10}));
            }
        };

        // create offers for bob and carol
        createOffers();

        // Test when LPT holder doesn't own a trustline for an asset associated
        // with the LPToken
        checkOfferCrossing();

        // bob and carol create trustlines for the assets that they are
        // missing.
        // HOWEVER, they are still unauthorized!
        env(trust(bob, gw["USD"](100'000)));
        env(trust(carol, gw["BTC"](100'000)));
        env.close();

        // Recreate offers for bob and carol if fixEnforceTrustlineAuth is
        // disabled, since these two offers have been consumed by the
        // previous test
        createOffers();

        // Test when LPT holder has an "unauthorized" trustline for an asset
        // associated with the LPToken
        checkOfferCrossing();

        // gateway authorizes bob and carol for their respective trustlines
        env(trust(gw, bob["USD"](100'000)), txflags(tfSetfAuth));
        env.close();
        env(trust(gw, carol["BTC"](100'000)), txflags(tfSetfAuth));
        env.close();

        // recreate offers for bob and carol
        createOffers();

        // alice can now consume bob and carol's offers
        auto aliceOfferSeq{env.seq(alice)};
        env(offer(alice, STAmount{lpIssue, 10}, XRP(10)));
        env.close();

        BEAST_EXPECT(!offerExists(env, carol, carolOfferSeq));
        BEAST_EXPECT(!offerExists(env, alice, aliceOfferSeq));

        aliceOfferSeq = env.seq(alice);
        env(offer(alice, XRP(5), STAmount{lpIssue, 10}));
        env.close();

        BEAST_EXPECT(!offerExists(env, bob, bobOfferSeq));
        BEAST_EXPECT(!offerExists(env, alice, aliceOfferSeq));
    }

    void
    testLPTokenOfferCreate(FeatureBitset features)
    {
        testcase("LPToken OfferCreate");

        using namespace jtx;

        // temporarily disable AMMClawback to allow single side deposit without
        // owning one of the assets
        Env env(*this, features - featureAMMClawback - fixEnforceTrustlineAuth);
        auto const lpIssue = setup(env);

        // re-enable the amendment if the test run has enabled it
        // originally
        if (features[fixEnforceTrustlineAuth])
        {
            env.enableFeature(fixEnforceTrustlineAuth);
            env.close();
        }

        // bob can still create offers to buy LPToken regardless of the
        // fixEnforceTrustlineAuth because we do not require the offer creator
        // to own the trustline to the buying asset at the time of offer
        // creation
        auto bobOfferSeq{env.seq(bob)};
        env(offer(bob, STAmount{lpIssue, 10}, XRP(5)), txflags(tfPassive));
        env.close();
        BEAST_EXPECT(
            checkOffer(env, bob, bobOfferSeq, STAmount{lpIssue, 10}, XRP(5)));

        if (features[fixEnforceTrustlineAuth])
        {
            auto carolOfferSeq{env.seq(carol)};
            env(offer(carol, XRP(10), STAmount{lpIssue, 10}),
                txflags(tfPassive),
                ter(tecUNFUNDED_OFFER));
            env.close();
            BEAST_EXPECT(!offerExists(env, carol, carolOfferSeq));
        }
        else
        {
            auto carolOfferSeq{env.seq(carol)};
            env(offer(carol, XRP(10), STAmount{lpIssue, 10}),
                txflags(tfPassive));
            env.close();
            BEAST_EXPECT(checkOffer(
                env, carol, carolOfferSeq, XRP(10), STAmount{lpIssue, 10}));
        }

        // bob and carol create trustlines for the assets that they are
        // missing.
        // HOWEVER, they are still unauthorized!
        env(trust(bob, gw["USD"](100'000)));
        env(trust(carol, gw["BTC"](100'000)));
        env.close();

        if (features[fixEnforceTrustlineAuth])
        {
            auto carolOfferSeq{env.seq(carol)};
            env(offer(carol, XRP(10), STAmount{lpIssue, 10}),
                txflags(tfPassive),
                ter(tecUNFUNDED_OFFER));
            env.close();
            BEAST_EXPECT(!offerExists(env, carol, carolOfferSeq));
        }
        else
        {
            auto carolOfferSeq{env.seq(carol)};
            env(offer(carol, XRP(10), STAmount{lpIssue, 10}),
                txflags(tfPassive));
            env.close();
            BEAST_EXPECT(checkOffer(
                env, carol, carolOfferSeq, XRP(10), STAmount{lpIssue, 10}));
        }

        // gateway authorizes carol for BTC
        env(trust(gw, carol["BTC"](100'000)), txflags(tfSetfAuth));
        env.close();

        // carol can finally create an offer successfully after being authorized
        auto carolOfferSeq{env.seq(carol)};
        env(offer(carol, XRP(10), STAmount{lpIssue, 10}), txflags(tfPassive));
        env.close();
        BEAST_EXPECT(offerExists(env, carol, carolOfferSeq));
    }

    auto
    mintAndOfferNFT(
        test::jtx::Env& env,
        test::jtx::Account const& account,
        test::jtx::PrettyAmount const& currency,
        uint32_t xfee = 0u)
    {
        using namespace test::jtx;
        auto const nftID{
            token::getNextID(env, account, 0u, tfTransferable, xfee)};
        env(token::mint(account, 0),
            token::xferFee(xfee),
            txflags(tfTransferable));
        env.close();

        auto const sellIdx = keylet::nftoffer(account, env.seq(account)).key;
        env(token::createOffer(account, nftID, currency),
            txflags(tfSellNFToken));
        env.close();

        return std::make_tuple(nftID, sellIdx);
    }

    void
    testDirectStep(FeatureBitset features)
    {
        testcase("Direct step");

        using namespace test::jtx;

        // disable fixEnforceNFTokenTrustlineV2 amendment to allow creation of
        // unauthorized funds
        Env env(
            *this,
            features - fixEnforceNFTokenTrustlineV2 - fixEnforceTrustlineAuth);

        auto const USD{gw["USD"]};

        env.fund(XRP(10000), gw, alice, bob);
        env(fset(gw, asfRequireAuth));
        env.close();

        auto const limit = USD(10000);

        env(trust(alice, limit));
        env.close();
        env(trust(gw, limit, alice, tfSetfAuth));
        env.close();
        env(pay(gw, alice, USD(1000)));
        env.close();

        env(trust(bob, USD(100'000)));
        env.close();

        auto const [nftID, _] = mintAndOfferNFT(env, bob, drops(1));
        auto const buyIdx = keylet::nftoffer(alice, env.seq(alice)).key;

        // It should be possible to create a buy offer even if NFT owner is
        // not authorized
        env(token::createOffer(alice, nftID, USD(10)), token::owner(bob));
        env.close();
        env(token::acceptBuyOffer(bob, buyIdx));
        env.close();

        // bob has unauthorized funds
        BEAST_EXPECT(env.balance(bob, USD) == USD(10));

        if (features[fixEnforceTrustlineAuth])
        {
            env.enableFeature(fixEnforceTrustlineAuth);
            env.close();

            BEAST_EXPECT(
                USD(0) ==
                accountHolds(
                    *env.closed(),
                    bob,
                    USD.currency,
                    gw,
                    fhIGNORE_FREEZE,
                    ahZERO_IF_UNAUTHORIZED,
                    env.journal));

            env(pay(bob, alice, USD(1)), ter(tecNO_AUTH));
            env.close();
            BEAST_EXPECT(env.balance(bob, USD) == USD(10));

            env(pay(alice, bob, USD(2)), ter(tecNO_AUTH));
            env.close();

            BEAST_EXPECT(env.balance(bob, USD) == USD(10));
        }
        else
        {
            BEAST_EXPECT(
                USD(10) ==
                accountHolds(
                    *env.closed(),
                    bob,
                    USD.currency,
                    gw,
                    fhIGNORE_FREEZE,
                    ahZERO_IF_UNAUTHORIZED,
                    env.journal));

            env(pay(bob, alice, USD(1)));
            env.close();
            BEAST_EXPECT(env.balance(bob, USD) == USD(9));

            env(pay(alice, bob, USD(2)));
            env.close();

            BEAST_EXPECT(env.balance(bob, USD) == USD(11));
        }
    }

    void
    testBookStep(FeatureBitset features)
    {
        testcase("Book step");

        using namespace test::jtx;

        // disable fixEnforceNFTokenTrustlineV2 amendment to allow creation of
        // unauthorized funds
        Env env(
            *this,
            features - fixEnforceNFTokenTrustlineV2 - fixEnforceTrustlineAuth);

        auto const USD{gw["USD"]};

        env.fund(XRP(10000), gw, alice, bob);
        env(fset(gw, asfRequireAuth));
        env.close();

        auto const limit = USD(10000);

        env(trust(alice, limit));
        env.close();
        env(trust(gw, limit, alice, tfSetfAuth));
        env.close();
        env(pay(gw, alice, USD(1000)));
        env.close();

        env(trust(bob, USD(100'000)));
        env.close();

        auto const [nftID, _] = mintAndOfferNFT(env, bob, drops(1));
        auto const buyIdx = keylet::nftoffer(alice, env.seq(alice)).key;

        // It should be possible to create a buy offer even if NFT owner is
        // not authorized
        env(token::createOffer(alice, nftID, USD(10)), token::owner(bob));
        env.close();
        env(token::acceptBuyOffer(bob, buyIdx));
        env.close();

        // bob has unauthorized funds
        BEAST_EXPECT(env.balance(bob, USD) == USD(10));

        if (features[fixEnforceTrustlineAuth])
        {
            BEAST_EXPECT(env.balance(bob, USD) == USD(10));
            BEAST_EXPECT(env.balance(alice, USD) == USD(990));

            // creating an offer where bob is selling unauthorized USD
            auto const bobOfferSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob, bobOfferSeq, XRP(10), USD(10)));

            // enable it again
            env.enableFeature(fixEnforceTrustlineAuth);
            env.close();

            // alice creates an offer that would remove bob's unfunded offer
            auto const aliceOfferSeq{env.seq(alice)};
            env(offer(alice, USD(10), XRP(10)));
            env.close();

            // bob's unfunded offer is removed and he still has 10 USD
            BEAST_EXPECT(!offerExists(env, bob, bobOfferSeq));
            BEAST_EXPECT(
                checkOffer(env, alice, aliceOfferSeq, USD(10), XRP(10)));
            BEAST_EXPECT(env.balance(bob, USD) == USD(10));
            BEAST_EXPECT(env.balance(alice, USD) == USD(990));
        }
        else
        {
            BEAST_EXPECT(env.balance(bob, USD) == USD(10));
            BEAST_EXPECT(env.balance(alice, USD) == USD(990));

            auto const bobOfferSeq{env.seq(bob)};
            env(offer(bob, XRP(10), USD(10)));
            env.close();
            BEAST_EXPECT(checkOffer(env, bob, bobOfferSeq, XRP(10), USD(10)));

            // alice's offer can consume bob's unfunded offer
            auto const aliceOfferSeq{env.seq(alice)};
            env(offer(alice, USD(10), XRP(10)));
            env.close();

            BEAST_EXPECT(!offerExists(env, bob, bobOfferSeq));
            BEAST_EXPECT(!offerExists(env, alice, aliceOfferSeq));
            BEAST_EXPECT(env.balance(bob, USD) == USD(0));
            BEAST_EXPECT(env.balance(alice, USD) == USD(1000));
        }
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{testable_amendments()};

        for (auto const features : {all, all - fixEnforceTrustlineAuth})
        {
            testLPTokenDirectStep(features);
            testLPTokenBookStep(features);
            testLPTokenOfferCreate(features);
            testDirectStep(features);
            testBookStep(features);

            // TODO: add tests for AMM with MPTs after it is supported
        }
    }
};

BEAST_DEFINE_TESTSUITE(TrustlineAuth, app, ripple);
}  // namespace test
}  // namespace ripple
