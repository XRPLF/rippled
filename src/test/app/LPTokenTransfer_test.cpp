#include <test/jtx/AMM.h>
#include <test/jtx/AMMTest.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/check.h>
#include <test/jtx/offer.h>
#include <test/jtx/owners.h>  // IWYU pragma: keep
#include <test/jtx/pay.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/ter.h>
#include <test/jtx/token.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace xrpl::test {

class LPTokenTransfer_test : public jtx::AMMTest
{
    void
    testDirectStep(FeatureBitset features)
    {
        testcase("DirectStep");

        using namespace jtx;
        Env env{*this, features};
        fund(env, gw_, {alice_}, {USD(20'000), BTC(0.5)}, Fund::All);
        env.close();

        AMM ammAlice(env, alice_, USD(20'000), BTC(0.5));
        BEAST_EXPECT(ammAlice.expectBalances(USD(20'000), BTC(0.5), IOUAmount{100, 0}));

        fund(env, gw_, {carol_}, {USD(4'000), BTC(1)}, Fund::Acct);
        ammAlice.deposit(carol_, 10);
        BEAST_EXPECT(ammAlice.expectBalances(USD(22'000), BTC(0.55), IOUAmount{110, 0}));

        fund(env, gw_, {bob_}, {USD(4'000), BTC(1)}, Fund::Acct);
        ammAlice.deposit(bob_, 10);
        BEAST_EXPECT(ammAlice.expectBalances(USD(24'000), BTC(0.60), IOUAmount{120, 0}));

        auto const lpIssue = ammAlice.lptIssue();
        env.trust(STAmount{lpIssue, 500}, alice_);
        env.trust(STAmount{lpIssue, 500}, bob_);
        env.trust(STAmount{lpIssue, 500}, carol_);
        env.close();

        // gateway freezes carol_'s USD
        env(trust(gw_, carol_["USD"](0), tfSetFreeze));
        env.close();

        // bob_ can still send lptoken to carol_ even tho carol_'s USD is
        // frozen, regardless of whether fixFrozenLPTokenTransfer is enabled or
        // not
        // Note: Deep freeze is not considered for LPToken transfer
        env(pay(bob_, carol_, STAmount{lpIssue, 5}));
        env.close();

        // cannot transfer to an amm account
        env(pay(carol_, lpIssue.getIssuer(), STAmount{lpIssue, 5}), Ter(tecNO_PERMISSION));
        env.close();

        if (features[fixFrozenLPTokenTransfer])
        {
            // carol_ is frozen on USD and therefore can't send lptoken to bob_
            env(pay(carol_, bob_, STAmount{lpIssue, 5}), Ter(tecPATH_DRY));
        }
        else
        {
            // carol_ can still send lptoken with frozen USD
            env(pay(carol_, bob_, STAmount{lpIssue, 5}));
        }
    }

    void
    testBookStep(FeatureBitset features)
    {
        testcase("BookStep");

        using namespace jtx;
        Env env{*this, features};

        fund(env, gw_, {alice_, bob_, carol_}, {USD(10'000), EUR(10'000)}, Fund::All);
        AMM ammAlice(env, alice_, USD(10'000), EUR(10'000));
        ammAlice.deposit(carol_, 1'000);
        ammAlice.deposit(bob_, 1'000);

        auto const lpIssue = ammAlice.lptIssue();

        // carols creates an offer to sell lptoken
        env(offer(carol_, XRP(10), STAmount{lpIssue, 10}), Txflags(tfPassive));
        env.close();
        BEAST_EXPECT(expectOffers(env, carol_, 1));

        env.trust(STAmount{lpIssue, 1'000'000'000}, alice_);
        env.trust(STAmount{lpIssue, 1'000'000'000}, bob_);
        env.trust(STAmount{lpIssue, 1'000'000'000}, carol_);
        env.close();

        // gateway freezes carol_'s USD
        env(trust(gw_, carol_["USD"](0), tfSetFreeze));
        env.close();

        // exercises alice_'s ability to consume carol_'s offer to sell lptoken
        // when carol_'s USD is frozen pre/post fixFrozenLPTokenTransfer
        // amendment
        if (features[fixFrozenLPTokenTransfer])
        {
            // with fixFrozenLPTokenTransfer, alice_ fails to consume carol_'s
            // offer since carol_'s USD is frozen
            env(pay(alice_, bob_, STAmount{lpIssue, 10}),
                Txflags(tfPartialPayment),
                Sendmax(XRP(10)),
                Ter(tecPATH_DRY));
            env.close();
            BEAST_EXPECT(expectOffers(env, carol_, 1));

            // gateway unfreezes carol_'s USD
            env(trust(gw_, carol_["USD"](1'000'000'000), tfClearFreeze));
            env.close();

            // alice_ successfully consumes carol_'s offer
            env(pay(alice_, bob_, STAmount{lpIssue, 10}),
                Txflags(tfPartialPayment),
                Sendmax(XRP(10)));
            env.close();
            BEAST_EXPECT(expectOffers(env, carol_, 0));
        }
        else
        {
            // without fixFrozenLPTokenTransfer, alice_ can consume carol_'s offer
            // even when carol_'s USD is frozen
            env(pay(alice_, bob_, STAmount{lpIssue, 10}),
                Txflags(tfPartialPayment),
                Sendmax(XRP(10)));
            env.close();
            BEAST_EXPECT(expectOffers(env, carol_, 0));
        }

        // make sure carol_'s USD is not frozen
        env(trust(gw_, carol_["USD"](1'000'000'000), tfClearFreeze));
        env.close();

        // ensure that carol_'s offer to buy lptoken can be consumed by alice_
        // even when carol_'s USD is frozen
        {
            // carol_ creates an offer to buy lptoken
            env(offer(carol_, STAmount{lpIssue, 10}, XRP(10)), Txflags(tfPassive));
            env.close();
            BEAST_EXPECT(expectOffers(env, carol_, 1));

            // gateway freezes carol_'s USD
            env(trust(gw_, carol_["USD"](0), tfSetFreeze));
            env.close();

            // alice_ successfully consumes carol_'s offer
            env(pay(alice_, bob_, XRP(10)),
                Txflags(tfPartialPayment),
                Sendmax(STAmount{lpIssue, 10}));
            env.close();
            BEAST_EXPECT(expectOffers(env, carol_, 0));
        }
    }

    void
    testOfferCreation(FeatureBitset features)
    {
        testcase("Create offer");

        using namespace jtx;
        Env env{*this, features};

        fund(env, gw_, {alice_, bob_, carol_}, {USD(10'000), EUR(10'000)}, Fund::All);
        AMM ammAlice(env, alice_, USD(10'000), EUR(10'000));
        ammAlice.deposit(carol_, 1'000);
        ammAlice.deposit(bob_, 1'000);

        auto const lpIssue = ammAlice.lptIssue();

        // gateway freezes carol_'s USD
        env(trust(gw_, carol_["USD"](0), tfSetFreeze));
        env.close();

        // exercises carol_'s ability to create a new offer to sell lptoken with
        // frozen USD, before and after fixFrozenLPTokenTransfer
        if (features[fixFrozenLPTokenTransfer])
        {
            // with fixFrozenLPTokenTransfer, carol_ can't create an offer to
            // sell lptoken when one of the assets is frozen

            // carol_ can't create an offer to sell lptoken
            env(offer(carol_, XRP(10), STAmount{lpIssue, 10}),
                Txflags(tfPassive),
                Ter(tecUNFUNDED_OFFER));
            env.close();
            BEAST_EXPECT(expectOffers(env, carol_, 0));

            // gateway unfreezes carol_'s USD
            env(trust(gw_, carol_["USD"](1'000'000'000), tfClearFreeze));
            env.close();

            // carol_ can create an offer to sell lptoken after USD is unfrozen
            env(offer(carol_, XRP(10), STAmount{lpIssue, 10}), Txflags(tfPassive));
            env.close();
            BEAST_EXPECT(expectOffers(env, carol_, 1));
        }
        else
        {
            // without fixFrozenLPTokenTransfer, carol_ can create an offer
            env(offer(carol_, XRP(10), STAmount{lpIssue, 10}), Txflags(tfPassive));
            env.close();
            BEAST_EXPECT(expectOffers(env, carol_, 1));
        }

        // gateway freezes carol_'s USD
        env(trust(gw_, carol_["USD"](0), tfSetFreeze));
        env.close();

        // carol_ can create offer to buy lptoken even if USD is frozen
        env(offer(carol_, STAmount{lpIssue, 10}, XRP(5)), Txflags(tfPassive));
        env.close();
        BEAST_EXPECT(expectOffers(env, carol_, 2));
    }

    void
    testOfferCrossing(FeatureBitset features)
    {
        testcase("Offer crossing");

        using namespace jtx;
        Env env{*this, features};

        // Offer crossing with two AMM LPTokens.
        fund(env, gw_, {alice_, carol_}, {USD(10'000)}, Fund::All);
        AMM ammAlice1(env, alice_, XRP(10'000), USD(10'000));
        ammAlice1.deposit(carol_, 10'000'000);

        fund(env, gw_, {alice_, carol_}, {EUR(10'000)}, Fund::TokenOnly);
        AMM ammAlice2(env, alice_, XRP(10'000), EUR(10'000));
        ammAlice2.deposit(carol_, 10'000'000);
        auto const token1 = ammAlice1.lptIssue();
        auto const token2 = ammAlice2.lptIssue();

        // carol_ creates offer
        env(offer(carol_, STAmount{token2, 100}, STAmount{token1, 100}));
        env.close();
        BEAST_EXPECT(expectOffers(env, carol_, 1));

        // gateway freezes carol_'s USD, carol_'s token1 should be frozen as well
        env(trust(gw_, carol_["USD"](0), tfSetFreeze));
        env.close();

        // alice_ creates an offer which exhibits different behavior on offer
        // crossing depending on if fixFrozenLPTokenTransfer is enabled
        env(offer(alice_, STAmount{token1, 100}, STAmount{token2, 100}));
        env.close();

        // exercises carol_'s offer's ability to cross with alice_'s offer when
        // carol_'s USD is frozen, before and after fixFrozenLPTokenTransfer
        if (features[fixFrozenLPTokenTransfer])
        {
            // with fixFrozenLPTokenTransfer enabled, alice_'s offer can no
            // longer cross with carol_'s offer
            BEAST_EXPECT(
                expectHolding(env, alice_, STAmount{token1, 10'000'000}) &&
                expectHolding(env, alice_, STAmount{token2, 10'000'000}));
            BEAST_EXPECT(
                expectHolding(env, carol_, STAmount{token2, 10'000'000}) &&
                expectHolding(env, carol_, STAmount{token1, 10'000'000}));
            BEAST_EXPECT(expectOffers(env, alice_, 1) && expectOffers(env, carol_, 0));
        }
        else
        {
            // alice_'s offer still crosses with carol_'s offer despite carol_'s
            // token1 is frozen
            BEAST_EXPECT(
                expectHolding(env, alice_, STAmount{token1, 10'000'100}) &&
                expectHolding(env, alice_, STAmount{token2, 9'999'900}));
            BEAST_EXPECT(
                expectHolding(env, carol_, STAmount{token2, 10'000'100}) &&
                expectHolding(env, carol_, STAmount{token1, 9'999'900}));
            BEAST_EXPECT(expectOffers(env, alice_, 0) && expectOffers(env, carol_, 0));
        }
    }

    void
    testCheck(FeatureBitset features)
    {
        testcase("Check");

        using namespace jtx;
        Env env{*this, features};

        fund(env, gw_, {alice_, bob_, carol_}, {USD(10'000), EUR(10'000)}, Fund::All);
        AMM ammAlice(env, alice_, USD(10'000), EUR(10'000));
        ammAlice.deposit(carol_, 1'000);
        ammAlice.deposit(bob_, 1'000);

        auto const lpIssue = ammAlice.lptIssue();

        // gateway freezes carol_'s USD
        env(trust(gw_, carol_["USD"](0), tfSetFreeze));
        env.close();

        // carol_ can always create a check with lptoken that has frozen
        // token
        uint256 const carolChkId{keylet::check(carol_, env.seq(carol_)).key};
        env(check::create(carol_, bob_, STAmount{lpIssue, 10}));
        env.close();

        // with fixFrozenLPTokenTransfer enabled, bob_ fails to cash the check
        if (features[fixFrozenLPTokenTransfer])
        {
            env(check::cash(bob_, carolChkId, STAmount{lpIssue, 10}), Ter(tecPATH_PARTIAL));
        }
        else
        {
            env(check::cash(bob_, carolChkId, STAmount{lpIssue, 10}));
        }

        env.close();

        // bob_ creates a check
        uint256 const bobChkId{keylet::check(bob_, env.seq(bob_)).key};
        env(check::create(bob_, carol_, STAmount{lpIssue, 10}));
        env.close();

        // carol_ cashes the bob_'s check. Even though carol_ is frozen, she can
        // still receive LPToken
        env(check::cash(carol_, bobChkId, STAmount{lpIssue, 10}));
        env.close();
    }

    void
    testNFTOffers(FeatureBitset features)
    {
        testcase("NFT Offers");
        using namespace test::jtx;

        Env env{*this, features};

        // Setup AMM
        fund(env, gw_, {alice_, bob_, carol_}, {USD(10'000), EUR(10'000)}, Fund::All);
        AMM ammAlice(env, alice_, USD(10'000), EUR(10'000));
        ammAlice.deposit(carol_, 1'000);
        ammAlice.deposit(bob_, 1'000);

        auto const lpIssue = ammAlice.lptIssue();

        // bob_ mints a nft
        uint256 const nftID{token::getNextID(env, bob_, 0u, tfTransferable)};
        env(token::mint(bob_, 0), Txflags(tfTransferable));
        env.close();

        // bob_ creates a sell offer for lptoken
        uint256 const sellOfferIndex = keylet::nftoffer(bob_, env.seq(bob_)).key;
        env(token::createOffer(bob_, nftID, STAmount{lpIssue, 10}), Txflags(tfSellNFToken));
        env.close();

        // gateway freezes carol_'s USD
        env(trust(gw_, carol_["USD"](0), tfSetFreeze));
        env.close();

        // exercises one's ability to transfer NFT using lptoken when one of the
        // assets is frozen
        if (features[fixFrozenLPTokenTransfer])
        {
            // with fixFrozenLPTokenTransfer, freezing USD will prevent buy/sell
            // offers with lptokens from being created/accepted

            // carol_ fails to accept bob_'s offer with lptoken because carol_'s
            // USD is frozen
            env(token::acceptSellOffer(carol_, sellOfferIndex), Ter(tecINSUFFICIENT_FUNDS));
            env.close();

            // gateway unfreezes carol_'s USD
            env(trust(gw_, carol_["USD"](1'000'000), tfClearFreeze));
            env.close();

            // carol_ can now accept the offer and own the nft
            env(token::acceptSellOffer(carol_, sellOfferIndex));
            env.close();

            // gateway freezes bob_'s USD
            env(trust(gw_, bob_["USD"](0), tfSetFreeze));
            env.close();

            // bob_ fails to create a buy offer with lptoken for carol_'s nft
            // since bob_'s USD is frozen
            env(token::createOffer(bob_, nftID, STAmount{lpIssue, 10}),
                token::Owner(carol_),
                Ter(tecUNFUNDED_OFFER));
            env.close();

            // gateway unfreezes bob_'s USD
            env(trust(gw_, bob_["USD"](1'000'000), tfClearFreeze));
            env.close();

            // bob_ can now create a buy offer
            env(token::createOffer(bob_, nftID, STAmount{lpIssue, 10}), token::Owner(carol_));
            env.close();
        }
        else
        {
            // without fixFrozenLPTokenTransfer, freezing USD will still allow
            // buy/sell offers to be created/accepted with lptoken

            // carol_ can still accept bob_'s offer despite carol_'s USD is frozen
            env(token::acceptSellOffer(carol_, sellOfferIndex));
            env.close();

            // gateway freezes bob_'s USD
            env(trust(gw_, bob_["USD"](0), tfSetFreeze));
            env.close();

            // bob_ creates a buy offer with lptoken despite bob_'s USD is frozen
            uint256 const buyOfferIndex = keylet::nftoffer(bob_, env.seq(bob_)).key;
            env(token::createOffer(bob_, nftID, STAmount{lpIssue, 10}), token::Owner(carol_));
            env.close();

            // carol_ accepts bob_'s offer
            env(token::acceptBuyOffer(carol_, buyOfferIndex));
            env.close();
        }
    }

public:
    void
    run() override
    {
        FeatureBitset const all{jtx::testableAmendments()};

        for (auto const features : {all, all - fixFrozenLPTokenTransfer})
        {
            testDirectStep(features);
            testBookStep(features);
            testOfferCreation(features);
            testOfferCrossing(features);
            testCheck(features);
            testNFTOffers(features);
        }
    }
};

BEAST_DEFINE_TESTSUITE(LPTokenTransfer, app, xrpl);
}  // namespace xrpl::test
