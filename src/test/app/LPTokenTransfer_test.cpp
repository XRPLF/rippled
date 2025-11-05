#include <test/jtx.h>
#include <test/jtx/AMM.h>
#include <test/jtx/AMMTest.h>

namespace ripple {
namespace test {

class LPTokenTransfer_test : public jtx::AMMTest
{
    void
    testDirectStep(FeatureBitset features)
    {
        testcase("DirectStep");

        using namespace jtx;
        Env env{*this, features};
        fund(env, gw, {alice}, {USD(20'000), BTC(0.5)}, Fund::All);
        env.close();

        AMM ammAlice(env, alice, USD(20'000), BTC(0.5));
        BEAST_EXPECT(
            ammAlice.expectBalances(USD(20'000), BTC(0.5), IOUAmount{100, 0}));

        fund(env, gw, {carol}, {USD(4'000), BTC(1)}, Fund::Acct);
        ammAlice.deposit(carol, 10);
        BEAST_EXPECT(
            ammAlice.expectBalances(USD(22'000), BTC(0.55), IOUAmount{110, 0}));

        fund(env, gw, {bob}, {USD(4'000), BTC(1)}, Fund::Acct);
        ammAlice.deposit(bob, 10);
        BEAST_EXPECT(
            ammAlice.expectBalances(USD(24'000), BTC(0.60), IOUAmount{120, 0}));

        auto const lpIssue = ammAlice.lptIssue();
        env.trust(STAmount{lpIssue, 500}, alice);
        env.trust(STAmount{lpIssue, 500}, bob);
        env.trust(STAmount{lpIssue, 500}, carol);
        env.close();

        // gateway freezes carol's USD
        env(trust(gw, carol["USD"](0), tfSetFreeze));
        env.close();

        // bob can still send lptoken to carol even tho carol's USD is
        // frozen, regardless of whether fixFrozenLPTokenTransfer is enabled or
        // not
        // Note: Deep freeze is not considered for LPToken transfer
        env(pay(bob, carol, STAmount{lpIssue, 5}));
        env.close();

        // cannot transfer to an amm account
        env(pay(carol, lpIssue.getIssuer(), STAmount{lpIssue, 5}),
            ter(tecNO_PERMISSION));
        env.close();

        if (features[fixFrozenLPTokenTransfer])
        {
            // carol is frozen on USD and therefore can't send lptoken to bob
            env(pay(carol, bob, STAmount{lpIssue, 5}), ter(tecPATH_DRY));
        }
        else
        {
            // carol can still send lptoken with frozen USD
            env(pay(carol, bob, STAmount{lpIssue, 5}));
        }
    }

    void
    testBookStep(FeatureBitset features)
    {
        testcase("BookStep");

        using namespace jtx;
        Env env{*this, features};

        fund(
            env,
            gw,
            {alice, bob, carol},
            {USD(10'000), EUR(10'000)},
            Fund::All);
        AMM ammAlice(env, alice, USD(10'000), EUR(10'000));
        ammAlice.deposit(carol, 1'000);
        ammAlice.deposit(bob, 1'000);

        auto const lpIssue = ammAlice.lptIssue();

        // carols creates an offer to sell lptoken
        env(offer(carol, XRP(10), STAmount{lpIssue, 10}), txflags(tfPassive));
        env.close();
        BEAST_EXPECT(expectOffers(env, carol, 1));

        env.trust(STAmount{lpIssue, 1'000'000'000}, alice);
        env.trust(STAmount{lpIssue, 1'000'000'000}, bob);
        env.trust(STAmount{lpIssue, 1'000'000'000}, carol);
        env.close();

        // gateway freezes carol's USD
        env(trust(gw, carol["USD"](0), tfSetFreeze));
        env.close();

        // exercises alice's ability to consume carol's offer to sell lptoken
        // when carol's USD is frozen pre/post fixFrozenLPTokenTransfer
        // amendment
        if (features[fixFrozenLPTokenTransfer])
        {
            // with fixFrozenLPTokenTransfer, alice fails to consume carol's
            // offer since carol's USD is frozen
            env(pay(alice, bob, STAmount{lpIssue, 10}),
                txflags(tfPartialPayment),
                sendmax(XRP(10)),
                ter(tecPATH_DRY));
            env.close();
            BEAST_EXPECT(expectOffers(env, carol, 1));

            // gateway unfreezes carol's USD
            env(trust(gw, carol["USD"](1'000'000'000), tfClearFreeze));
            env.close();

            // alice successfully consumes carol's offer
            env(pay(alice, bob, STAmount{lpIssue, 10}),
                txflags(tfPartialPayment),
                sendmax(XRP(10)));
            env.close();
            BEAST_EXPECT(expectOffers(env, carol, 0));
        }
        else
        {
            // without fixFrozenLPTokenTransfer, alice can consume carol's offer
            // even when carol's USD is frozen
            env(pay(alice, bob, STAmount{lpIssue, 10}),
                txflags(tfPartialPayment),
                sendmax(XRP(10)));
            env.close();
            BEAST_EXPECT(expectOffers(env, carol, 0));
        }

        // make sure carol's USD is not frozen
        env(trust(gw, carol["USD"](1'000'000'000), tfClearFreeze));
        env.close();

        // ensure that carol's offer to buy lptoken can be consumed by alice
        // even when carol's USD is frozen
        {
            // carol creates an offer to buy lptoken
            env(offer(carol, STAmount{lpIssue, 10}, XRP(10)),
                txflags(tfPassive));
            env.close();
            BEAST_EXPECT(expectOffers(env, carol, 1));

            // gateway freezes carol's USD
            env(trust(gw, carol["USD"](0), tfSetFreeze));
            env.close();

            // alice successfully consumes carol's offer
            env(pay(alice, bob, XRP(10)),
                txflags(tfPartialPayment),
                sendmax(STAmount{lpIssue, 10}));
            env.close();
            BEAST_EXPECT(expectOffers(env, carol, 0));
        }
    }

    void
    testOfferCreation(FeatureBitset features)
    {
        testcase("Create offer");

        using namespace jtx;
        Env env{*this, features};

        fund(
            env,
            gw,
            {alice, bob, carol},
            {USD(10'000), EUR(10'000)},
            Fund::All);
        AMM ammAlice(env, alice, USD(10'000), EUR(10'000));
        ammAlice.deposit(carol, 1'000);
        ammAlice.deposit(bob, 1'000);

        auto const lpIssue = ammAlice.lptIssue();

        // gateway freezes carol's USD
        env(trust(gw, carol["USD"](0), tfSetFreeze));
        env.close();

        // exercises carol's ability to create a new offer to sell lptoken with
        // frozen USD, before and after fixFrozenLPTokenTransfer
        if (features[fixFrozenLPTokenTransfer])
        {
            // with fixFrozenLPTokenTransfer, carol can't create an offer to
            // sell lptoken when one of the assets is frozen

            // carol can't create an offer to sell lptoken
            env(offer(carol, XRP(10), STAmount{lpIssue, 10}),
                txflags(tfPassive),
                ter(tecUNFUNDED_OFFER));
            env.close();
            BEAST_EXPECT(expectOffers(env, carol, 0));

            // gateway unfreezes carol's USD
            env(trust(gw, carol["USD"](1'000'000'000), tfClearFreeze));
            env.close();

            // carol can create an offer to sell lptoken after USD is unfrozen
            env(offer(carol, XRP(10), STAmount{lpIssue, 10}),
                txflags(tfPassive));
            env.close();
            BEAST_EXPECT(expectOffers(env, carol, 1));
        }
        else
        {
            // without fixFrozenLPTokenTransfer, carol can create an offer
            env(offer(carol, XRP(10), STAmount{lpIssue, 10}),
                txflags(tfPassive));
            env.close();
            BEAST_EXPECT(expectOffers(env, carol, 1));
        }

        // gateway freezes carol's USD
        env(trust(gw, carol["USD"](0), tfSetFreeze));
        env.close();

        // carol can create offer to buy lptoken even if USD is frozen
        env(offer(carol, STAmount{lpIssue, 10}, XRP(5)), txflags(tfPassive));
        env.close();
        BEAST_EXPECT(expectOffers(env, carol, 2));
    }

    void
    testOfferCrossing(FeatureBitset features)
    {
        testcase("Offer crossing");

        using namespace jtx;
        Env env{*this, features};

        // Offer crossing with two AMM LPTokens.
        fund(env, gw, {alice, carol}, {USD(10'000)}, Fund::All);
        AMM ammAlice1(env, alice, XRP(10'000), USD(10'000));
        ammAlice1.deposit(carol, 10'000'000);

        fund(env, gw, {alice, carol}, {EUR(10'000)}, Fund::IOUOnly);
        AMM ammAlice2(env, alice, XRP(10'000), EUR(10'000));
        ammAlice2.deposit(carol, 10'000'000);
        auto const token1 = ammAlice1.lptIssue();
        auto const token2 = ammAlice2.lptIssue();

        // carol creates offer
        env(offer(carol, STAmount{token2, 100}, STAmount{token1, 100}));
        env.close();
        BEAST_EXPECT(expectOffers(env, carol, 1));

        // gateway freezes carol's USD, carol's token1 should be frozen as well
        env(trust(gw, carol["USD"](0), tfSetFreeze));
        env.close();

        // alice creates an offer which exhibits different behavior on offer
        // crossing depending on if fixFrozenLPTokenTransfer is enabled
        env(offer(alice, STAmount{token1, 100}, STAmount{token2, 100}));
        env.close();

        // exercises carol's offer's ability to cross with alice's offer when
        // carol's USD is frozen, before and after fixFrozenLPTokenTransfer
        if (features[fixFrozenLPTokenTransfer])
        {
            // with fixFrozenLPTokenTransfer enabled, alice's offer can no
            // longer cross with carol's offer
            BEAST_EXPECT(
                expectHolding(env, alice, STAmount{token1, 10'000'000}) &&
                expectHolding(env, alice, STAmount{token2, 10'000'000}));
            BEAST_EXPECT(
                expectHolding(env, carol, STAmount{token2, 10'000'000}) &&
                expectHolding(env, carol, STAmount{token1, 10'000'000}));
            BEAST_EXPECT(
                expectOffers(env, alice, 1) && expectOffers(env, carol, 0));
        }
        else
        {
            // alice's offer still crosses with carol's offer despite carol's
            // token1 is frozen
            BEAST_EXPECT(
                expectHolding(env, alice, STAmount{token1, 10'000'100}) &&
                expectHolding(env, alice, STAmount{token2, 9'999'900}));
            BEAST_EXPECT(
                expectHolding(env, carol, STAmount{token2, 10'000'100}) &&
                expectHolding(env, carol, STAmount{token1, 9'999'900}));
            BEAST_EXPECT(
                expectOffers(env, alice, 0) && expectOffers(env, carol, 0));
        }
    }

    void
    testCheck(FeatureBitset features)
    {
        testcase("Check");

        using namespace jtx;
        Env env{*this, features};

        fund(
            env,
            gw,
            {alice, bob, carol},
            {USD(10'000), EUR(10'000)},
            Fund::All);
        AMM ammAlice(env, alice, USD(10'000), EUR(10'000));
        ammAlice.deposit(carol, 1'000);
        ammAlice.deposit(bob, 1'000);

        auto const lpIssue = ammAlice.lptIssue();

        // gateway freezes carol's USD
        env(trust(gw, carol["USD"](0), tfSetFreeze));
        env.close();

        // carol can always create a check with lptoken that has frozen
        // token
        uint256 const carolChkId{keylet::check(carol, env.seq(carol)).key};
        env(check::create(carol, bob, STAmount{lpIssue, 10}));
        env.close();

        // with fixFrozenLPTokenTransfer enabled, bob fails to cash the check
        if (features[fixFrozenLPTokenTransfer])
            env(check::cash(bob, carolChkId, STAmount{lpIssue, 10}),
                ter(tecPATH_PARTIAL));
        else
            env(check::cash(bob, carolChkId, STAmount{lpIssue, 10}));

        env.close();

        // bob creates a check
        uint256 const bobChkId{keylet::check(bob, env.seq(bob)).key};
        env(check::create(bob, carol, STAmount{lpIssue, 10}));
        env.close();

        // carol cashes the bob's check. Even though carol is frozen, she can
        // still receive LPToken
        env(check::cash(carol, bobChkId, STAmount{lpIssue, 10}));
        env.close();
    }

    void
    testNFTOffers(FeatureBitset features)
    {
        testcase("NFT Offers");
        using namespace test::jtx;

        Env env{*this, features};

        // Setup AMM
        fund(
            env,
            gw,
            {alice, bob, carol},
            {USD(10'000), EUR(10'000)},
            Fund::All);
        AMM ammAlice(env, alice, USD(10'000), EUR(10'000));
        ammAlice.deposit(carol, 1'000);
        ammAlice.deposit(bob, 1'000);

        auto const lpIssue = ammAlice.lptIssue();

        // bob mints a nft
        uint256 const nftID{token::getNextID(env, bob, 0u, tfTransferable)};
        env(token::mint(bob, 0), txflags(tfTransferable));
        env.close();

        // bob creates a sell offer for lptoken
        uint256 const sellOfferIndex = keylet::nftoffer(bob, env.seq(bob)).key;
        env(token::createOffer(bob, nftID, STAmount{lpIssue, 10}),
            txflags(tfSellNFToken));
        env.close();

        // gateway freezes carol's USD
        env(trust(gw, carol["USD"](0), tfSetFreeze));
        env.close();

        // exercises one's ability to transfer NFT using lptoken when one of the
        // assets is frozen
        if (features[fixFrozenLPTokenTransfer])
        {
            // with fixFrozenLPTokenTransfer, freezing USD will prevent buy/sell
            // offers with lptokens from being created/accepted

            // carol fails to accept bob's offer with lptoken because carol's
            // USD is frozen
            env(token::acceptSellOffer(carol, sellOfferIndex),
                ter(tecINSUFFICIENT_FUNDS));
            env.close();

            // gateway unfreezes carol's USD
            env(trust(gw, carol["USD"](1'000'000), tfClearFreeze));
            env.close();

            // carol can now accept the offer and own the nft
            env(token::acceptSellOffer(carol, sellOfferIndex));
            env.close();

            // gateway freezes bobs's USD
            env(trust(gw, bob["USD"](0), tfSetFreeze));
            env.close();

            // bob fails to create a buy offer with lptoken for carol's nft
            // since bob's USD is frozen
            env(token::createOffer(bob, nftID, STAmount{lpIssue, 10}),
                token::owner(carol),
                ter(tecUNFUNDED_OFFER));
            env.close();

            // gateway unfreezes bob's USD
            env(trust(gw, bob["USD"](1'000'000), tfClearFreeze));
            env.close();

            // bob can now create a buy offer
            env(token::createOffer(bob, nftID, STAmount{lpIssue, 10}),
                token::owner(carol));
            env.close();
        }
        else
        {
            // without fixFrozenLPTokenTransfer, freezing USD will still allow
            // buy/sell offers to be created/accepted with lptoken

            // carol can still accept bob's offer despite carol's USD is frozen
            env(token::acceptSellOffer(carol, sellOfferIndex));
            env.close();

            // gateway freezes bob's USD
            env(trust(gw, bob["USD"](0), tfSetFreeze));
            env.close();

            // bob creates a buy offer with lptoken despite bob's USD is frozen
            uint256 const buyOfferIndex =
                keylet::nftoffer(bob, env.seq(bob)).key;
            env(token::createOffer(bob, nftID, STAmount{lpIssue, 10}),
                token::owner(carol));
            env.close();

            // carol accepts bob's offer
            env(token::acceptBuyOffer(carol, buyOfferIndex));
            env.close();
        }
    }

public:
    void
    run() override
    {
        FeatureBitset const all{jtx::testable_amendments()};

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

BEAST_DEFINE_TESTSUITE(LPTokenTransfer, app, ripple);
}  // namespace test
}  // namespace ripple
