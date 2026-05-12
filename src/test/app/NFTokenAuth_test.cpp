
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>  // IWYU pragma: keep
#include <test/jtx/flags.h>
#include <test/jtx/owners.h>  // IWYU pragma: keep
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/token.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <array>
#include <cstdint>
#include <memory>
#include <tuple>

namespace xrpl {

class NFTokenAuth_test : public beast::unit_test::Suite
{
    static auto
    mintAndOfferNFT(
        test::jtx::Env& env,
        test::jtx::Account const& account,
        test::jtx::PrettyAmount const& currency,
        uint32_t xfee = 0u)
    {
        using namespace test::jtx;
        auto const nftID{token::getNextID(env, account, 0u, tfTransferable, xfee)};
        env(token::mint(account, 0), token::XferFee(xfee), Txflags(tfTransferable));
        env.close();

        auto const sellIdx = keylet::nftoffer(account, env.seq(account)).key;
        env(token::createOffer(account, nftID, currency), Txflags(tfSellNFToken));
        env.close();

        return std::make_tuple(nftID, sellIdx);
    }

public:
    void
    testBuyOfferUnauthorizedSeller(FeatureBitset features)
    {
        testcase("Unauthorized seller tries to accept buy offer");
        using namespace test::jtx;

        Env env(*this, features);
        Account const g1{"G1"};
        Account const a1{"A1"};
        Account const a2{"A2"};
        auto const usd{g1["USD"]};

        env.fund(XRP(10000), g1, a1, a2);
        env(fset(g1, asfRequireAuth));
        env.close();

        auto const limit = usd(10000);

        env(trust(a1, limit));
        env(trust(g1, limit, a1, tfSetfAuth));
        env(pay(g1, a1, usd(1000)));

        auto const [nftID, _] = mintAndOfferNFT(env, a2, drops(1));
        auto const buyIdx = keylet::nftoffer(a1, env.seq(a1)).key;

        // It should be possible to create a buy offer even if NFT owner is not
        // authorized
        env(token::createOffer(a1, nftID, usd(10)), token::Owner(a2));

        if (features[fixEnforceNFTokenTrustlineV2])
        {
            // test: G1 requires authorization of A2, no trust line exists
            env(token::acceptBuyOffer(a2, buyIdx), Ter(tecNO_LINE));
            env.close();

            // trust line created, but not authorized
            env(trust(a2, limit));

            // test: G1 requires authorization of A2
            env(token::acceptBuyOffer(a2, buyIdx), Ter(tecNO_AUTH));
            env.close();
        }
        else
        {
            // Old behavior: it is possible to sell tokens and receive IOUs
            // without the authorization
            env(token::acceptBuyOffer(a2, buyIdx));
            env.close();

            BEAST_EXPECT(env.balance(a2, usd) == usd(10));
        }
    }

    void
    testCreateBuyOfferUnauthorizedBuyer(FeatureBitset features)
    {
        testcase("Unauthorized buyer tries to create buy offer");
        using namespace test::jtx;

        Env env(*this, features);
        Account const g1{"G1"};
        Account const a1{"A1"};
        Account const a2{"A2"};
        auto const usd{g1["USD"]};

        env.fund(XRP(10000), g1, a1, a2);
        env(fset(g1, asfRequireAuth));
        env.close();

        auto const [nftID, _] = mintAndOfferNFT(env, a2, drops(1));

        // test: check that buyer can't make an offer if they're not authorized.
        env(token::createOffer(a1, nftID, usd(10)), token::Owner(a2), Ter(tecUNFUNDED_OFFER));
        env.close();

        // Artificially create an unauthorized trustline with balance. Don't
        // close ledger before running the actual tests against this trustline.
        // After ledger is closed, the trustline will not exist.
        auto const unauthTrustline = [&](OpenView& view, beast::Journal) -> bool {
            auto const sleA1 = std::make_shared<SLE>(keylet::line(a1, g1, g1["USD"].currency));
            sleA1->setFieldAmount(sfBalance, a1["USD"](-1000));
            view.rawInsert(sleA1);
            return true;
        };
        env.app().getOpenLedger().modify(unauthTrustline);

        if (features[fixEnforceNFTokenTrustlineV2])
        {
            // test: check that buyer can't make an offer even with balance
            env(token::createOffer(a1, nftID, usd(10)), token::Owner(a2), Ter(tecNO_AUTH));
        }
        else
        {
            // old behavior: can create an offer if balance allows, regardless
            // ot authorization
            env(token::createOffer(a1, nftID, usd(10)), token::Owner(a2));
        }
    }

    void
    testAcceptBuyOfferUnauthorizedBuyer(FeatureBitset features)
    {
        testcase("Seller tries to accept buy offer from unauth buyer");
        using namespace test::jtx;

        Env env(*this, features);
        Account const g1{"G1"};
        Account const a1{"A1"};
        Account const a2{"A2"};
        auto const usd{g1["USD"]};

        env.fund(XRP(10000), g1, a1, a2);
        env(fset(g1, asfRequireAuth));
        env.close();

        auto const limit = usd(10000);

        auto const [nftID, _] = mintAndOfferNFT(env, a2, drops(1));

        // First we authorize buyer and seller so that he can create buy offer
        env(trust(a1, limit));
        env(trust(g1, limit, a1, tfSetfAuth));
        env(pay(g1, a1, usd(10)));
        env(trust(a2, limit));
        env(trust(g1, limit, a2, tfSetfAuth));
        env(pay(g1, a2, usd(10)));
        env.close();

        auto const buyIdx = keylet::nftoffer(a1, env.seq(a1)).key;
        env(token::createOffer(a1, nftID, usd(10)), token::Owner(a2));
        env.close();

        env(pay(a1, g1, usd(10)));
        env(trust(a1, usd(0)));
        env(trust(g1, a1["USD"](0)));
        env.close();

        // Replace an existing authorized trustline with artificial unauthorized
        // trustline with balance. Don't close ledger before running the actual
        // tests against this trustline. After ledger is closed, the trustline
        // will not exist.
        auto const unauthTrustline = [&](OpenView& view, beast::Journal) -> bool {
            auto const sleA1 = std::make_shared<SLE>(keylet::line(a1, g1, g1["USD"].currency));
            sleA1->setFieldAmount(sfBalance, a1["USD"](-1000));
            view.rawInsert(sleA1);
            return true;
        };
        env.app().getOpenLedger().modify(unauthTrustline);
        if (features[fixEnforceNFTokenTrustlineV2])
        {
            // test: check that offer can't be accepted even with balance
            env(token::acceptBuyOffer(a2, buyIdx), Ter(tecNO_AUTH));
        }
    }

    void
    testSellOfferUnauthorizedSeller(FeatureBitset features)
    {
        testcase(
            "Authorized buyer tries to accept sell offer from unauthorized "
            "seller");
        using namespace test::jtx;

        Env env(*this, features);
        Account const g1{"G1"};
        Account const a1{"A1"};
        Account const a2{"A2"};
        auto const usd{g1["USD"]};

        env.fund(XRP(10000), g1, a1, a2);
        env(fset(g1, asfRequireAuth));
        env.close();

        auto const limit = usd(10000);

        env(trust(a1, limit));
        env(trust(g1, limit, a1, tfSetfAuth));
        env(pay(g1, a1, usd(1000)));

        auto const [nftID, _] = mintAndOfferNFT(env, a2, drops(1));
        if (features[fixEnforceNFTokenTrustlineV2])
        {
            // test: can't create sell offer if there is no trustline but auth
            // required
            env(token::createOffer(a2, nftID, usd(10)), Txflags(tfSellNFToken), Ter(tecNO_LINE));

            env(trust(a2, limit));
            // test: can't create sell offer if not authorized to hold token
            env(token::createOffer(a2, nftID, usd(10)), Txflags(tfSellNFToken), Ter(tecNO_AUTH));

            // Authorizing trustline to make an offer creation possible
            env(trust(g1, usd(0), a2, tfSetfAuth));
            env.close();
            auto const sellIdx = keylet::nftoffer(a2, env.seq(a2)).key;
            env(token::createOffer(a2, nftID, usd(10)), Txflags(tfSellNFToken));
            env.close();
            //

            // Reseting trustline to delete it. This allows to check if
            // already existing offers handled correctly
            env(trust(a2, usd(0)));
            env.close();

            // test: G1 requires authorization of A1, no trust line exists
            env(token::acceptSellOffer(a1, sellIdx), Ter(tecNO_LINE));
            env.close();

            // trust line created, but not authorized
            env(trust(a2, limit));
            env.close();

            // test: G1 requires authorization of A1
            env(token::acceptSellOffer(a1, sellIdx), Ter(tecNO_AUTH));
            env.close();
        }
        else
        {
            auto const sellIdx = keylet::nftoffer(a2, env.seq(a2)).key;

            // Old behavior: sell offer can be created without authorization
            env(token::createOffer(a2, nftID, usd(10)), Txflags(tfSellNFToken));
            env.close();

            // Old behavior: it is possible to sell NFT and receive IOUs
            // without the authorization
            env(token::acceptSellOffer(a1, sellIdx));
            env.close();

            BEAST_EXPECT(env.balance(a2, usd) == usd(10));
        }
    }

    void
    testSellOfferUnauthorizedBuyer(FeatureBitset features)
    {
        testcase("Unauthorized buyer tries to accept sell offer");
        using namespace test::jtx;

        Env env(*this, features);
        Account const g1{"G1"};
        Account const a1{"A1"};
        Account const a2{"A2"};
        auto const usd{g1["USD"]};

        env.fund(XRP(10000), g1, a1, a2);
        env(fset(g1, asfRequireAuth));
        env.close();

        auto const limit = usd(10000);

        env(trust(a2, limit));
        env(trust(g1, limit, a2, tfSetfAuth));

        auto const [_, sellIdx] = mintAndOfferNFT(env, a2, usd(10));

        // test: check that buyer can't accept an offer if they're not
        // authorized.
        env(token::acceptSellOffer(a1, sellIdx), Ter(tecINSUFFICIENT_FUNDS));
        env.close();

        // Creating an artificial unauth trustline
        auto const unauthTrustline = [&](OpenView& view, beast::Journal) -> bool {
            auto const sleA1 = std::make_shared<SLE>(keylet::line(a1, g1, g1["USD"].currency));
            sleA1->setFieldAmount(sfBalance, a1["USD"](-1000));
            view.rawInsert(sleA1);
            return true;
        };
        env.app().getOpenLedger().modify(unauthTrustline);
        if (features[fixEnforceNFTokenTrustlineV2])
        {
            env(token::acceptSellOffer(a1, sellIdx), Ter(tecNO_AUTH));
        }
    }

    void
    testBrokeredAcceptOfferUnauthorizedBroker(FeatureBitset features)
    {
        testcase("Unauthorized broker bridges authorized buyer and seller.");
        using namespace test::jtx;

        Env env(*this, features);
        Account const g1{"G1"};
        Account const a1{"A1"};
        Account const a2{"A2"};
        Account const broker{"broker"};
        auto const usd{g1["USD"]};

        env.fund(XRP(10000), g1, a1, a2, broker);
        env(fset(g1, asfRequireAuth));
        env.close();

        auto const limit = usd(10000);

        env(trust(a1, limit));
        env(trust(g1, limit, a1, tfSetfAuth));
        env(pay(g1, a1, usd(1000)));
        env(trust(a2, limit));
        env(trust(g1, limit, a2, tfSetfAuth));
        env(pay(g1, a2, usd(1000)));
        env.close();

        auto const [nftID, sellIdx] = mintAndOfferNFT(env, a2, usd(10));
        auto const buyIdx = keylet::nftoffer(a1, env.seq(a1)).key;
        env(token::createOffer(a1, nftID, usd(11)), token::Owner(a2));
        env.close();

        if (features[fixEnforceNFTokenTrustlineV2])
        {
            // test: G1 requires authorization of broker, no trust line exists
            env(token::brokerOffers(broker, buyIdx, sellIdx),
                token::BrokerFee(usd(1)),
                Ter(tecNO_LINE));
            env.close();

            // trust line created, but not authorized
            env(trust(broker, limit));
            env.close();

            // test: G1 requires authorization of broker
            env(token::brokerOffers(broker, buyIdx, sellIdx),
                token::BrokerFee(usd(1)),
                Ter(tecNO_AUTH));
            env.close();

            // test: can still be brokered without broker fee.
            env(token::brokerOffers(broker, buyIdx, sellIdx));
            env.close();
        }
        else
        {
            // Old behavior: broker can receive IOUs without the authorization
            env(token::brokerOffers(broker, buyIdx, sellIdx), token::BrokerFee(usd(1)));
            env.close();

            BEAST_EXPECT(env.balance(broker, usd) == usd(1));
        }
    }

    void
    testBrokeredAcceptOfferUnauthorizedBuyer(FeatureBitset features)
    {
        testcase(
            "Authorized broker tries to bridge offers from unauthorized "
            "buyer.");
        using namespace test::jtx;

        Env env(*this, features);
        Account const g1{"G1"};
        Account const a1{"A1"};
        Account const a2{"A2"};
        Account const broker{"broker"};
        auto const usd{g1["USD"]};

        env.fund(XRP(10000), g1, a1, a2, broker);
        env(fset(g1, asfRequireAuth));
        env.close();

        auto const limit = usd(10000);

        env(trust(a1, limit));
        env(trust(g1, usd(0), a1, tfSetfAuth));
        env(pay(g1, a1, usd(1000)));
        env(trust(a2, limit));
        env(trust(g1, usd(0), a2, tfSetfAuth));
        env(pay(g1, a2, usd(1000)));
        env(trust(broker, limit));
        env(trust(g1, usd(0), broker, tfSetfAuth));
        env(pay(g1, broker, usd(1000)));
        env.close();

        auto const [nftID, sellIdx] = mintAndOfferNFT(env, a2, usd(10));
        auto const buyIdx = keylet::nftoffer(a1, env.seq(a1)).key;
        env(token::createOffer(a1, nftID, usd(11)), token::Owner(a2));
        env.close();

        // Resetting buyer's trust line to delete it
        env(pay(a1, g1, usd(1000)));
        env(trust(a1, usd(0)));
        env.close();

        auto const unauthTrustline = [&](OpenView& view, beast::Journal) -> bool {
            auto const sleA1 = std::make_shared<SLE>(keylet::line(a1, g1, g1["USD"].currency));
            sleA1->setFieldAmount(sfBalance, a1["USD"](-1000));
            view.rawInsert(sleA1);
            return true;
        };
        env.app().getOpenLedger().modify(unauthTrustline);

        if (features[fixEnforceNFTokenTrustlineV2])
        {
            // test: G1 requires authorization of A2
            env(token::brokerOffers(broker, buyIdx, sellIdx),
                token::BrokerFee(usd(1)),
                Ter(tecNO_AUTH));
            env.close();
        }
    }

    void
    testBrokeredAcceptOfferUnauthorizedSeller(FeatureBitset features)
    {
        testcase(
            "Authorized broker tries to bridge offers from unauthorized "
            "seller.");
        using namespace test::jtx;

        Env env(*this, features);
        Account const g1{"G1"};
        Account const a1{"A1"};
        Account const a2{"A2"};
        Account const broker{"broker"};
        auto const usd{g1["USD"]};

        env.fund(XRP(10000), g1, a1, a2, broker);
        env(fset(g1, asfRequireAuth));
        env.close();

        auto const limit = usd(10000);

        env(trust(a1, limit));
        env(trust(g1, limit, a1, tfSetfAuth));
        env(pay(g1, a1, usd(1000)));
        env(trust(broker, limit));
        env(trust(g1, limit, broker, tfSetfAuth));
        env(pay(g1, broker, usd(1000)));
        env.close();

        // Authorizing trustline to make an offer creation possible
        env(trust(g1, usd(0), a2, tfSetfAuth));
        env.close();

        auto const [nftID, sellIdx] = mintAndOfferNFT(env, a2, usd(10));
        auto const buyIdx = keylet::nftoffer(a1, env.seq(a1)).key;
        env(token::createOffer(a1, nftID, usd(11)), token::Owner(a2));
        env.close();

        // Reseting trustline to delete it. This allows to check if
        // already existing offers handled correctly
        env(trust(a2, usd(0)));
        env.close();

        if (features[fixEnforceNFTokenTrustlineV2])
        {
            // test: G1 requires authorization of broker, no trust line exists
            env(token::brokerOffers(broker, buyIdx, sellIdx),
                token::BrokerFee(usd(1)),
                Ter(tecNO_LINE));
            env.close();

            // trust line created, but not authorized
            env(trust(a2, limit));
            env.close();

            // test: G1 requires authorization of A2
            env(token::brokerOffers(broker, buyIdx, sellIdx),
                token::BrokerFee(usd(1)),
                Ter(tecNO_AUTH));
            env.close();

            // test: cannot be brokered even without broker fee.
            env(token::brokerOffers(broker, buyIdx, sellIdx), Ter(tecNO_AUTH));
            env.close();
        }
        else
        {
            // Old behavior: broker can receive IOUs without the authorization
            env(token::brokerOffers(broker, buyIdx, sellIdx), token::BrokerFee(usd(1)));
            env.close();

            BEAST_EXPECT(env.balance(a2, usd) == usd(10));
            return;
        }
    }

    void
    testTransferFeeUnauthorizedMinter(FeatureBitset features)
    {
        testcase("Unauthorized minter receives transfer fee.");
        using namespace test::jtx;

        Env env(*this, features);
        Account const g1{"G1"};
        Account const minter{"minter"};
        Account const a1{"A1"};
        Account const a2{"A2"};
        auto const usd{g1["USD"]};

        env.fund(XRP(10000), g1, minter, a1, a2);
        env(fset(g1, asfRequireAuth));
        env.close();

        auto const limit = usd(10000);

        env(trust(a1, limit));
        env(trust(g1, limit, a1, tfSetfAuth));
        env(pay(g1, a1, usd(1000)));
        env(trust(a2, limit));
        env(trust(g1, limit, a2, tfSetfAuth));
        env(pay(g1, a2, usd(1000)));

        env(trust(minter, limit));
        env.close();

        // We authorized A1 and A2, but not the minter.
        // Now mint NFT
        auto const [nftID, minterSellIdx] = mintAndOfferNFT(env, minter, drops(1), 1);
        env(token::acceptSellOffer(a1, minterSellIdx));

        uint256 const sellIdx = keylet::nftoffer(a1, env.seq(a1)).key;
        env(token::createOffer(a1, nftID, usd(100)), Txflags(tfSellNFToken));

        if (features[fixEnforceNFTokenTrustlineV2])
        {
            // test: G1 requires authorization
            env(token::acceptSellOffer(a2, sellIdx), Ter(tecNO_AUTH));
            env.close();
        }
        else
        {
            // Old behavior: can sell for USD. Minter can receive tokens
            env(token::acceptSellOffer(a2, sellIdx));
            env.close();

            BEAST_EXPECT(env.balance(minter, usd) == usd(0.001));
        }
    }

    void
    run() override
    {
        using namespace test::jtx;
        static FeatureBitset const kAll{testableAmendments()};

        static std::array const kFeatures = {kAll - fixEnforceNFTokenTrustlineV2, kAll};

        for (auto const feature : kFeatures)
        {
            testBuyOfferUnauthorizedSeller(feature);
            testCreateBuyOfferUnauthorizedBuyer(feature);
            testAcceptBuyOfferUnauthorizedBuyer(feature);
            testSellOfferUnauthorizedSeller(feature);
            testSellOfferUnauthorizedBuyer(feature);
            testBrokeredAcceptOfferUnauthorizedBroker(feature);
            testBrokeredAcceptOfferUnauthorizedBuyer(feature);
            testBrokeredAcceptOfferUnauthorizedSeller(feature);
            testTransferFeeUnauthorizedMinter(feature);
        }
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(NFTokenAuth, app, xrpl, 2);

}  // namespace xrpl
