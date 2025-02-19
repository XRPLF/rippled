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

#include <xrpld/app/tx/detail/NFTokenUtils.h>

namespace ripple {

class NFTokenAuth_test : public beast::unit_test::suite
{
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

public:
    void
    testBuyOffer_UnauthorizedSeller(FeatureBitset features)
    {
        testcase("Unauthorized seller tries to accept buy offer");
        using namespace test::jtx;

        Env env(*this, features);
        Account G1{"G1"};
        Account A1{"A1"};
        Account A2{"A2"};
        auto const USD{G1["USD"]};

        env.fund(XRP(10000), G1, A1, A2);
        env(fset(G1, asfRequireAuth));
        env.close();

        auto const limit = USD(10000);

        env(trust(A1, limit));
        env(trust(G1, limit, A1, tfSetfAuth));
        env(pay(G1, A1, USD(1000)));

        auto const [nftID, _] = mintAndOfferNFT(env, A2, drops(1));
        auto const buyIdx = keylet::nftoffer(A1, env.seq(A1)).key;

        // It should be possible to create a buy offer even if NFT owner is not
        // authorized
        env(token::createOffer(A1, nftID, USD(10)), token::owner(A2));

        if (features[fixEnforceNFTokenTrustlineV2])
        {
            // test: G1 requires authorization of A2, no trust line exists
            env(token::acceptBuyOffer(A2, buyIdx), ter(tecNO_LINE));
            env.close();

            // trust line created, but not authorized
            env(trust(A2, limit));

            // test: G1 requires authorization of A2
            env(token::acceptBuyOffer(A2, buyIdx), ter(tecNO_AUTH));
            env.close();
        }
        else
        {
            // Old behavior: it is possible to sell tokens and receive IOUs
            // without the authorization
            env(token::acceptBuyOffer(A2, buyIdx));
            env.close();

            BEAST_EXPECT(env.balance(A2, USD) == USD(10));
        }
    }

    void
    testBuyOffer_UnauthorizedBuyer(FeatureBitset features)
    {
        testcase("Unauthorized buyer tries to create buy offer");
        using namespace test::jtx;

        Env env(*this, features);
        Account G1{"G1"};
        Account A1{"A1"};
        Account A2{"A2"};
        auto const USD{G1["USD"]};

        env.fund(XRP(10000), G1, A1, A2);
        env(fset(G1, asfRequireAuth));
        env.close();

        auto const [nftID, _] = mintAndOfferNFT(env, A2, drops(1));

        // test: check that buyer can't make an offer if they're not authorized.
        env(token::createOffer(A1, nftID, USD(10)),
            token::owner(A2),
            ter(tecUNFUNDED_OFFER));
    }

    void
    testSellOffer_UnautharizedSeller(FeatureBitset features)
    {
        testcase(
            "Authorized buyer tries to accept sell offer from unathorized "
            "seller");
        using namespace test::jtx;

        Env env(*this, features);
        Account G1{"G1"};
        Account A1{"A1"};
        Account A2{"A2"};
        auto const USD{G1["USD"]};

        env.fund(XRP(10000), G1, A1, A2);
        env(fset(G1, asfRequireAuth));
        env.close();

        auto const limit = USD(10000);

        env(trust(A1, limit));
        env(trust(G1, limit, A1, tfSetfAuth));
        env(pay(G1, A1, USD(1000)));

        auto const [nftID, _] = mintAndOfferNFT(env, A2, drops(1));
        if (features[fixEnforceNFTokenTrustlineV2])
        {
            // test: can't create sell offer if there is no trustline but auth
            // required
            env(token::createOffer(A2, nftID, USD(10)),
                txflags(tfSellNFToken),
                ter(tecNO_LINE));

            env(trust(A2, limit));
            // test: can't create sell offer if not authorized to hold token
            env(token::createOffer(A2, nftID, USD(10)),
                txflags(tfSellNFToken),
                ter(tecNO_AUTH));

            // Authorizing trustline to make an offer creation possible
            env(trust(G1, USD(0), A2, tfSetfAuth));
            env.close();
            auto const sellIdx = keylet::nftoffer(A2, env.seq(A2)).key;
            env(token::createOffer(A2, nftID, USD(10)), txflags(tfSellNFToken));
            env.close();
            //

            // Reseting trustline to delete it. This allows to check if
            // already existing offers handled correctly
            env(trust(A2, USD(0)));
            env.close();

            // test: G1 requires authorization of A1, no trust line exists
            env(token::acceptSellOffer(A1, sellIdx), ter(tecNO_LINE));
            env.close();

            // trust line created, but not authorized
            env(trust(A2, limit));
            env.close();

            // test: G1 requires authorization of A1
            env(token::acceptSellOffer(A1, sellIdx), ter(tecNO_AUTH));
            env.close();
        }
        else
        {
            auto const sellIdx = keylet::nftoffer(A2, env.seq(A2)).key;

            // Old behavior: sell offer can be created without authorization
            env(token::createOffer(A2, nftID, USD(10)), txflags(tfSellNFToken));
            env.close();

            // Old behavior: it is possible to sell NFT and receive IOUs
            // without the authorization
            env(token::acceptSellOffer(A1, sellIdx));
            env.close();

            BEAST_EXPECT(env.balance(A2, USD) == USD(10));
        }
    }

    void
    testSellOffer_UnautharizedBuyer(FeatureBitset features)
    {
        testcase("Unauthorized buyer tries to accept sell offer");
        using namespace test::jtx;

        Env env(*this, features);
        Account G1{"G1"};
        Account A1{"A1"};
        Account A2{"A2"};
        auto const USD{G1["USD"]};

        env.fund(XRP(10000), G1, A1, A2);
        env(fset(G1, asfRequireAuth));
        env.close();

        auto const limit = USD(10000);

        env(trust(A2, limit));
        env(trust(G1, limit, A2, tfSetfAuth));

        auto const [_, sellIdx] = mintAndOfferNFT(env, A2, USD(10));

        // test: check that buyer can't accept an offer if they're not
        // authorized.
        env(token::acceptSellOffer(A1, sellIdx), ter(tecINSUFFICIENT_FUNDS));
        env.close();
    }

    void
    testBrokeredAcceptOffer_UnathorizedBroker(FeatureBitset features)
    {
        testcase("Unathorized broker bridges authorized buyer and seller.");
        using namespace test::jtx;

        Env env(*this, features);
        Account G1{"G1"};
        Account A1{"A1"};
        Account A2{"A2"};
        Account broker{"broker"};
        auto const USD{G1["USD"]};

        env.fund(XRP(10000), G1, A1, A2, broker);
        env(fset(G1, asfRequireAuth));
        env.close();

        auto const limit = USD(10000);

        env(trust(A1, limit));
        env(trust(G1, limit, A1, tfSetfAuth));
        env(pay(G1, A1, USD(1000)));
        env(trust(A2, limit));
        env(trust(G1, limit, A2, tfSetfAuth));
        env(pay(G1, A2, USD(1000)));
        env.close();

        auto const [nftID, sellIdx] = mintAndOfferNFT(env, A2, USD(10));
        auto const buyIdx = keylet::nftoffer(A1, env.seq(A1)).key;
        env(token::createOffer(A1, nftID, USD(11)), token::owner(A2));
        env.close();

        if (features[fixEnforceNFTokenTrustlineV2])
        {
            // test: G1 requires authorization of broker, no trust line exists
            env(token::brokerOffers(broker, buyIdx, sellIdx),
                token::brokerFee(USD(1)),
                ter(tecNO_LINE));
            env.close();

            // trust line created, but not authorized
            env(trust(broker, limit));
            env.close();

            // test: G1 requires authorization of broker
            env(token::brokerOffers(broker, buyIdx, sellIdx),
                token::brokerFee(USD(1)),
                ter(tecNO_AUTH));
            env.close();

            // test: can still be brokered without broker fee.
            env(token::brokerOffers(broker, buyIdx, sellIdx));
            env.close();
        }
        else
        {
            // Old behavior: broker can receive IOUs without the authorization
            env(token::brokerOffers(broker, buyIdx, sellIdx),
                token::brokerFee(USD(1)));
            env.close();

            BEAST_EXPECT(env.balance(broker, USD) == USD(1));
        }
    }

    void
    testBrokeredAcceptOffer_UnathorizedCounterparties(FeatureBitset features)
    {
        testcase(
            "Authorized broker tries to bridge offers from unathrorized "
            "counterparties.");
        using namespace test::jtx;

        Env env(*this, features);
        Account G1{"G1"};
        Account A1{"A1"};
        Account A2{"A2"};
        Account broker{"broker"};
        auto const USD{G1["USD"]};

        env.fund(XRP(10000), G1, A1, A2, broker);
        env(fset(G1, asfRequireAuth));
        env.close();

        auto const limit = USD(10000);

        env(trust(A1, limit));
        env(trust(G1, limit, A1, tfSetfAuth));
        env(pay(G1, A1, USD(1000)));
        env(trust(broker, limit));
        env(trust(G1, limit, broker, tfSetfAuth));
        env(pay(G1, broker, USD(1000)));
        env.close();

        // Authorizing trustline to make an offer creation possible
        env(trust(G1, USD(0), A2, tfSetfAuth));
        env.close();

        auto const [nftID, sellIdx] = mintAndOfferNFT(env, A2, USD(10));
        auto const buyIdx = keylet::nftoffer(A1, env.seq(A1)).key;
        env(token::createOffer(A1, nftID, USD(11)), token::owner(A2));
        env.close();

        // Reseting trustline to delete it. This allows to check if
        // already existing offers handled correctly
        env(trust(A2, USD(0)));
        env.close();

        if (features[fixEnforceNFTokenTrustlineV2])
        {
            // test: G1 requires authorization of broker, no trust line exists
            env(token::brokerOffers(broker, buyIdx, sellIdx),
                token::brokerFee(USD(1)),
                ter(tecNO_LINE));
            env.close();

            // trust line created, but not authorized
            env(trust(A2, limit));
            env.close();

            // test: G1 requires authorization of A2
            env(token::brokerOffers(broker, buyIdx, sellIdx),
                token::brokerFee(USD(1)),
                ter(tecNO_AUTH));
            env.close();

            // test: cannot be brokered even without broker fee.
            env(token::brokerOffers(broker, buyIdx, sellIdx), ter(tecNO_AUTH));
            env.close();
        }
        else
        {
            // Old behavior: broker can receive IOUs without the authorization
            env(token::brokerOffers(broker, buyIdx, sellIdx),
                token::brokerFee(USD(1)));
            env.close();

            BEAST_EXPECT(env.balance(A2, USD) == USD(10));
            return;
        }
    }

    void
    testTransferFee_UnathorizedMinter(FeatureBitset features)
    {
        testcase("Unathorized minter receives transfer fee.");
        using namespace test::jtx;

        Env env(*this, features);
        Account G1{"G1"};
        Account minter{"minter"};
        Account A1{"A1"};
        Account A2{"A2"};
        auto const USD{G1["USD"]};

        env.fund(XRP(10000), G1, minter, A1, A2);
        env(fset(G1, asfRequireAuth));
        env.close();

        auto const limit = USD(10000);

        env(trust(A1, limit));
        env(trust(G1, limit, A1, tfSetfAuth));
        env(pay(G1, A1, USD(1000)));
        env(trust(A2, limit));
        env(trust(G1, limit, A2, tfSetfAuth));
        env(pay(G1, A2, USD(1000)));

        env(trust(minter, limit));
        env.close();

        // We authorized A1 and A2, but not the minter.
        // Now mint NFT
        auto const [nftID, minterSellIdx] =
            mintAndOfferNFT(env, minter, drops(1), 1);
        env(token::acceptSellOffer(A1, minterSellIdx));

        uint256 const sellIdx = keylet::nftoffer(A1, env.seq(A1)).key;
        env(token::createOffer(A1, nftID, USD(100)), txflags(tfSellNFToken));

        if (features[fixEnforceNFTokenTrustlineV2])
        {
            // test: G1 requires authorization
            env(token::acceptSellOffer(A2, sellIdx), ter(tecNO_AUTH));
            env.close();
        }
        else
        {
            // Old behavior: can sell for USD. Minter can receive tokens
            env(token::acceptSellOffer(A2, sellIdx));
            env.close();

            BEAST_EXPECT(env.balance(minter, USD) == USD(0.001));
        }
    }

    void
    run() override
    {
        using namespace test::jtx;
        static FeatureBitset const all{supported_amendments()};

        static std::array const features = {
            all - fixEnforceNFTokenTrustlineV2, all};

        for (auto const feature : features)
        {
            testBuyOffer_UnauthorizedSeller(feature);
            testBuyOffer_UnauthorizedBuyer(feature);
            testSellOffer_UnautharizedSeller(feature);
            testSellOffer_UnautharizedBuyer(feature);
            testBrokeredAcceptOffer_UnathorizedBroker(feature);
            testBrokeredAcceptOffer_UnathorizedCounterparties(feature);
            testTransferFee_UnathorizedMinter(feature);
        }
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(NFTokenAuth, tx, ripple, 2);

}  // namespace ripple