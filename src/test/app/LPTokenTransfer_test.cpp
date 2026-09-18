#include <test/jtx/AMM.h>
#include <test/jtx/AMMTest.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/check.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
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
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <cstdint>
#include <optional>

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
        uint256 const carolChkId{keylet::check(carol_, SeqProxy::rawSequence(env.seq(carol_))).key};
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
        uint256 const bobChkId{keylet::check(bob_, SeqProxy::rawSequence(env.seq(bob_))).key};
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
        uint256 const sellOfferIndex =
            keylet::nftokenOffer(bob_, SeqProxy::rawSequence(env.seq(bob_))).key;
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
            uint256 const buyOfferIndex =
                keylet::nftokenOffer(bob_, SeqProxy::rawSequence(env.seq(bob_))).key;
            env(token::createOffer(bob_, nftID, STAmount{lpIssue, 10}), token::Owner(carol_));
            env.close();

            // carol_ accepts bob_'s offer
            env(token::acceptBuyOffer(carol_, buyOfferIndex));
            env.close();
        }
    }

    void
    testMPTCanTransferDirectStep(FeatureBitset features)
    {
        testcase("MPT CanTransfer DirectStep");

        using namespace jtx;

        // An MPT can only be an AMM pool asset once featureMPTokensV2 is
        // enabled, so this behavior is only meaningful when V2 is present, and
        // is independent of fixFrozenLPTokenTransfer.
        if (!features[featureMPTokensV2])
            return;

        // gw issues an MPT used as one of the AMM pool assets. gw (the MPT
        // issuer) seeds the pool and hands LP tokens to alice. Transferring LP
        // tokens between two non-issuer holders is only permitted when the
        // pool MPT allows transfers (lsfMPTCanTransfer); issuer-involving
        // transfers are always permitted. The check fires on the redeem step
        // against the AMM account via canTransferLPToken().
        auto testLPTokenTransfer = [&](std::uint32_t mptFlags, bool poolXrpToBtc) {
            Env env{*this, features};
            env.fund(XRP(30'000), gw_, alice_, bob_);
            env.close();

            // gw is the MPT issuer, so it may seed the pool regardless of
            // whether the MPT permits third-party transfers.
            MPT const btc = MPTTester(
                {.env = env, .issuer = gw_, .holders = {alice_}, .pay = 1'000, .flags = mptFlags});

            auto const asset1 = poolXrpToBtc ? XRP(10'000) : btc(10'000);
            auto const asset2 = poolXrpToBtc ? btc(10'000) : XRP(10'000);
            AMM const amm(env, gw_, asset1, asset2);
            auto const lpIssue = amm.lptIssue();

            env.trust(STAmount{lpIssue, 100'000}, alice_);
            env.trust(STAmount{lpIssue, 100'000}, bob_);
            env.close();

            // Issuer-involving LP token transfer is always allowed (gw is the
            // pool MPT's issuer), even when the MPT lacks CanTransfer.
            env(pay(gw_, alice_, STAmount{lpIssue, 1'000}));
            env.close();

            // Transfer between two non-issuer holders is allowed only if the
            // pool MPT has CanTransfer set; otherwise the redeem step against
            // the AMM account blocks it with tecNO_AUTH.
            if ((mptFlags & tfMPTCanTransfer) != 0u)
            {
                env(pay(alice_, bob_, STAmount{lpIssue, 100}));
            }
            else
            {
                env(pay(alice_, bob_, STAmount{lpIssue, 100}), Ter(tecNO_AUTH));
            }
            env.close();
        };

        // Pool MPT without CanTransfer blocks third-party LP token transfers.
        testLPTokenTransfer(tfMPTCanTrade, true);
        testLPTokenTransfer(tfMPTCanTrade, false);

        // Pool MPT with CanTransfer allows them.
        testLPTokenTransfer(tfMPTCanTrade | tfMPTCanTransfer, true);
        testLPTokenTransfer(tfMPTCanTrade | tfMPTCanTransfer, false);
    }

    void
    testMPTCanTransferOffer(FeatureBitset features)
    {
        testcase("MPT CanTransfer Offer");

        using namespace jtx;

        if (!features[featureMPTokensV2])
            return;

        // Parity with frozen LP tokens for the order book: a non-transferable
        // pool MPT makes the LP token un-spendable (canTransferLPToken zeroes
        // the spendable balance in accountHolds, just as isLPTokenFrozen does),
        // so an offer to sell it cannot be funded - the same tecUNFUNDED_OFFER
        // outcome as freezing a pool asset (see testOfferCreation).
        auto testLPTokenTransfer = [&](std::uint32_t mptFlags, bool poolXrpToBtc) {
            Env env{*this, features};
            env.fund(XRP(30'000), gw_, carol_);
            env.close();

            MPT const btc = MPTTester(
                {.env = env, .issuer = gw_, .holders = {carol_}, .pay = 1'000, .flags = mptFlags});

            auto const asset1 = poolXrpToBtc ? XRP(10'000) : btc(10'000);
            auto const asset2 = poolXrpToBtc ? btc(10'000) : XRP(10'000);
            AMM const amm(env, gw_, asset1, asset2);
            auto const lpIssue = amm.lptIssue();

            env.trust(STAmount{lpIssue, 100'000}, carol_);
            env.close();

            // gw (the pool MPT issuer) seeds carol_ with LP tokens; issuer
            // involving transfers are always allowed.
            env(pay(gw_, carol_, STAmount{lpIssue, 1'000}));
            env.close();

            // carol_ tries to create an offer to sell the LP token.
            if ((mptFlags & tfMPTCanTransfer) != 0u)
            {
                env(offer(carol_, XRP(10), STAmount{lpIssue, 10}), Txflags(tfPassive));
                env.close();
                BEAST_EXPECT(expectOffers(env, carol_, 1));
            }
            else
            {
                // Non-transferable pool MPT => LP token un-spendable => the
                // sell offer is unfunded, just as if a pool asset were frozen.
                env(offer(carol_, XRP(10), STAmount{lpIssue, 10}),
                    Txflags(tfPassive),
                    Ter(tecUNFUNDED_OFFER));
                env.close();
                BEAST_EXPECT(expectOffers(env, carol_, 0));
            }
        };

        // Pool MPT without CanTransfer: LP token sell offer is unfunded.
        testLPTokenTransfer(tfMPTCanTrade, true);
        testLPTokenTransfer(tfMPTCanTrade, false);

        // Pool MPT with CanTransfer: LP token sell offer is created.
        testLPTokenTransfer(tfMPTCanTrade | tfMPTCanTransfer, true);
        testLPTokenTransfer(tfMPTCanTrade | tfMPTCanTransfer, false);
    }

    // Common fixture for the RequireAuth tests: gw2 issues eur2 under
    // lsfRequireAuth while gw_'s USD needs no authorization; alice_ is
    // authorized for eur2 and creates a USD/eur2 pool. An LPToken of that
    // pool is a claim on both assets, so post fixCleanup3_5_0 acquiring one
    // requires authorization for both (see checkLPTokenAuthorization) --
    // otherwise transferability sidesteps the checks AMMDeposit enforces,
    // and an AMMClawback of USD against the holder would then deliver the
    // eur2 as a real balance.
    jtx::AMM
    setupRequireAuthPool(jtx::Env& env, jtx::Account const& gw2, jtx::IOU const& eur2)
    {
        using namespace jtx;
        fund(env, gw_, {alice_, bob_, carol_}, {USD(20'000)}, Fund::All);
        env.fund(XRP(30'000), gw2);
        env(fset(gw2, asfRequireAuth));
        env.close();

        env(trust(gw2, eur2(0), alice_, tfSetfAuth));
        env.trust(eur2(20'000), alice_);
        env.close();
        env(pay(gw2, alice_, eur2(10'000)));
        env.close();

        return AMM(env, alice_, USD(10'000), eur2(10'000));
    }

    void
    testRequireAuthPayment(FeatureBitset features)
    {
        testcase("RequireAuth payment");

        using namespace jtx;
        Env env{*this, features};
        Account const gw2{"gw2"};
        auto const eur2 = gw2["EUR"];
        AMM const ammAlice = setupRequireAuthPool(env, gw2, eur2);
        auto const lpIssue = ammAlice.lptIssue();

        env.trust(STAmount{lpIssue, 1'000'000}, bob_);
        env.close();

        if (features[fixCleanup3_5_0])
        {
            // bob_ has no eur2 trust line at all.
            env(pay(alice_, bob_, STAmount{lpIssue, 100}), Ter(tecNO_LINE));
            env.close();

            // An unauthorized eur2 line is not enough.
            env.trust(eur2(100), bob_);
            env.close();
            env(pay(alice_, bob_, STAmount{lpIssue, 100}), Ter(tecNO_AUTH));
            env.close();

            // A check is cashed through the same payment engine.
            uint256 const chkId{keylet::check(alice_, SeqProxy::rawSequence(env.seq(alice_))).key};
            env(check::create(alice_, bob_, STAmount{lpIssue, 100}));
            env.close();
            env(check::cash(bob_, chkId, STAmount{lpIssue, 100}), Ter(tecNO_AUTH));
            env.close();

            // Once gw2 authorizes bob_ he may hold both pool assets, so the
            // transfer settles.
            env(trust(gw2, eur2(0), bob_, tfSetfAuth));
            env.close();
            env(pay(alice_, bob_, STAmount{lpIssue, 100}));
            env.close();
            BEAST_EXPECT(expectHolding(env, bob_, STAmount{lpIssue, 100}));
        }
        else
        {
            // Pre-amendment nothing checks the recipient's authorization for
            // the pool assets: bob_ takes on eur2 exposure that gw2 never
            // approved (see XRPLF/rippled issue #5450).
            env(pay(alice_, bob_, STAmount{lpIssue, 100}));
            env.close();
            BEAST_EXPECT(expectHolding(env, bob_, STAmount{lpIssue, 100}));
        }
    }

    void
    testRequireAuthOffer(FeatureBitset features)
    {
        testcase("RequireAuth offer");

        using namespace jtx;
        Env env{*this, features};
        Account const gw2{"gw2"};
        auto const eur2 = gw2["EUR"];
        AMM const ammAlice = setupRequireAuthPool(env, gw2, eur2);
        auto const lpIssue = ammAlice.lptIssue();

        env.trust(STAmount{lpIssue, 1'000'000}, bob_);
        env.trust(eur2(100), bob_);
        env.close();

        if (features[fixCleanup3_5_0])
        {
            // An offer to buy the LPToken is an offer to take on the pool
            // assets' exposure, so it is rejected up front.
            env(offer(bob_, STAmount{lpIssue, 100}, XRP(100)), Ter(tecNO_AUTH));
            env.close();
            BEAST_EXPECT(expectOffers(env, bob_, 0));
        }
        else
        {
            // Pre-amendment the offer is accepted and rests on the book.
            env(offer(bob_, STAmount{lpIssue, 100}, XRP(100)));
            env.close();
            BEAST_EXPECT(expectOffers(env, bob_, 1));

            // Once the amendment activates, filling such an offer would
            // hand bob_ the exposure, so the book step removes it during
            // crossing instead of leaving it to poison the book.
            env.enableFeature(fixCleanup3_5_0);
            env.close();

            env(offer(alice_, XRP(100), STAmount{lpIssue, 100}));
            env.close();
            BEAST_EXPECT(expectOffers(env, bob_, 0));
            BEAST_EXPECT(expectOffers(env, alice_, 1));
            BEAST_EXPECT(expectHolding(env, bob_, STAmount{lpIssue, 0}));
        }
    }

    void
    testRequireAuthNFTOffer(FeatureBitset features)
    {
        testcase("RequireAuth NFT offer");

        using namespace jtx;
        Env env{*this, features};
        Account const gw2{"gw2"};
        auto const eur2 = gw2["EUR"];
        AMM const ammAlice = setupRequireAuthPool(env, gw2, eur2);
        auto const lpIssue = ammAlice.lptIssue();

        // carol_ (never authorized for eur2) sells an NFT priced in the
        // LPToken; accepting would deliver LPTokens to carol_ by direct
        // book-keeping (accountSend), outside the payment engine.
        env.trust(STAmount{lpIssue, 1'000'000}, carol_);
        env.close();
        uint256 const nftID{token::getNextID(env, carol_, 0u, tfTransferable)};
        env(token::mint(carol_, 0), Txflags(tfTransferable));
        env.close();

        if (features[fixCleanup3_5_0])
        {
            // The sell offer cannot even be created.
            env(token::createOffer(carol_, nftID, STAmount{lpIssue, 10}),
                Txflags(tfSellNFToken),
                Ter(tecNO_LINE));
            env.close();

            // Authorized, the offer is created and the trade settles.
            env(trust(gw2, eur2(0), carol_, tfSetfAuth));
            env.close();
            uint256 const sellOfferIndex =
                keylet::nftokenOffer(carol_, SeqProxy::rawSequence(env.seq(carol_))).key;
            env(token::createOffer(carol_, nftID, STAmount{lpIssue, 10}), Txflags(tfSellNFToken));
            env.close();
            env(token::acceptSellOffer(alice_, sellOfferIndex));
            env.close();
        }
        else
        {
            // Pre-amendment the offer is created freely and rests.
            uint256 const sellOfferIndex =
                keylet::nftokenOffer(carol_, SeqProxy::rawSequence(env.seq(carol_))).key;
            env(token::createOffer(carol_, nftID, STAmount{lpIssue, 10}), Txflags(tfSellNFToken));
            env.close();

            // Once the amendment activates, the resting offer cannot be
            // accepted while carol_ is unauthorized.
            env.enableFeature(fixCleanup3_5_0);
            env.close();
            env(token::acceptSellOffer(alice_, sellOfferIndex), Ter(tecNO_LINE));
            env.close();

            env(trust(gw2, eur2(0), carol_, tfSetfAuth));
            env.close();
            env(token::acceptSellOffer(alice_, sellOfferIndex));
            env.close();
        }
        BEAST_EXPECT(expectHolding(env, carol_, STAmount{lpIssue, 10}));
    }

    void
    testRequireAuthBid(FeatureBitset features)
    {
        testcase("RequireAuth AMMBid");

        using namespace jtx;
        Env env{*this, features};
        Account const gw2{"gw2"};
        auto const eur2 = gw2["EUR"];
        AMM ammAlice = setupRequireAuthPool(env, gw2, eur2);

        // bob_ becomes an authorized LP so he can bid later.
        env(trust(gw2, eur2(0), bob_, tfSetfAuth));
        env.trust(eur2(20'000), bob_);
        env.close();
        env(pay(gw2, bob_, eur2(1'000)));
        env.close();
        ammAlice.deposit(bob_, 1'000);

        // alice_ wins the auction slot, then divests her eur2 line
        // entirely: the balance is zero (all of it sits in the pool) and
        // the issuer-side authorization flag does not pin the line.
        env(ammAlice.bid({.account = alice_}));
        env.close();
        env(trust(alice_, eur2(0)));
        env.close();
        BEAST_EXPECT(!env.le(keylet::trustLine(alice_.id(), eur2)));

        if (features[fixCleanup3_5_0])
        {
            // Outbidding would refund LPTokens to alice_, exposure she is
            // no longer authorized for, so bob_'s bid is rejected cleanly.
            env(ammAlice.bid({.account = bob_, .bidMin = 200}), Ter(tecNO_LINE));
            env.close();

            // And alice_ cannot hold the slot again herself.
            env(ammAlice.bid({.account = alice_, .bidMin = 200}), Ter(tecNO_LINE));
            env.close();
        }
        else
        {
            // Pre-amendment nothing checks the previous owner's
            // authorization, so the same outbid settles (the invariant only
            // logs the refund).
            env(ammAlice.bid({.account = bob_, .bidMin = 200}));
            env.close();
        }
    }

    void
    testRequireAuthDepositEdges(FeatureBitset features)
    {
        testcase("RequireAuth deposit edge cases");

        using namespace jtx;
        Env env{*this, features};
        Account const gw2{"gw2"};
        auto const eur2 = gw2["EUR"];
        AMM ammAlice = setupRequireAuthPool(env, gw2, eur2);

        // bob_, authorized, funds a new pool with his entire eur2 balance,
        // held on a line in default shape: bob_ opened it himself (so gw2's
        // side carries no reserve) and set his limit back to zero. AMMCreate
        // transfers the exact amount, so the line is deleted in the very
        // transaction that delivers the LPTokens; the invariant recognizes
        // the pre-transaction authorization on the spending side.
        env.trust(eur2(1'000), bob_);
        env.close();
        env(trust(gw2, eur2(0), bob_, tfSetfAuth));
        env.close();
        env(pay(gw2, bob_, eur2(1'000)));
        env.close();
        env(trust(bob_, eur2(0)));
        env.close();
        AMM ammBob(env, bob_, XRP(1'000), eur2(1'000));
        BEAST_EXPECT(!env.le(keylet::trustLine(bob_.id(), eur2)));

        // A full withdrawal burns the LPTokens (no receivers) and finally
        // deletes the AMM account itself; neither trips the invariant even
        // with a RequireAuth pool asset. bob_ is re-authorized first so the
        // withdrawal may recreate his eur2 line.
        env(trust(gw2, eur2(0), bob_, tfSetfAuth));
        env.close();
        ammBob.withdrawAll(bob_);
        BEAST_EXPECT(!ammBob.ammExists());
        ammAlice.withdrawAll(alice_);
        BEAST_EXPECT(!ammAlice.ammExists());
    }

    void
    testRequireAuthClawback(FeatureBitset features)
    {
        testcase("RequireAuth AMMClawback");

        using namespace jtx;
        Env env{*this, features};

        // gwc enables clawback before issuing; gw2 requires authorization
        // for eur2. alice_, authorized, pools her entire eur2 balance; bob_
        // is authorized and only serves as a would-be recipient.
        Account const gwc{"gwc"};
        Account const gw2{"gw2"};
        auto const usdC = gwc["USD"];
        auto const eur2 = gw2["EUR"];
        env.fund(XRP(30'000), gwc, gw2, alice_, bob_);
        env.close();
        env(fset(gwc, asfAllowTrustLineClawback));
        env(fset(gw2, asfRequireAuth));
        env.close();

        env.trust(usdC(2'000), alice_);
        env.trust(eur2(2'000), alice_);
        env(trust(gw2, eur2(0), alice_, tfSetfAuth));
        env.trust(eur2(2'000), bob_);
        env(trust(gw2, eur2(0), bob_, tfSetfAuth));
        env.close();
        env(pay(gwc, alice_, usdC(1'000)));
        env(pay(gw2, alice_, eur2(1'000)));
        env.close();

        AMM const amm(env, alice_, usdC(1'000), eur2(1'000));

        // alice_ divests her now-empty eur2 line; only the pool holds eur2.
        env(trust(alice_, eur2(0)));
        env.close();
        BEAST_EXPECT(!env.le(keylet::trustLine(alice_.id(), eur2)));

        // The clawback succeeds in both eras: issuer-driven remediation is
        // exempt from ValidTrustLineAuth, so gwc is never held hostage by
        // alice_'s missing eur2 authorization. The paired eur2 lands on a
        // recreated, unauthorized line.
        env(amm::ammClawback(gwc, alice_, usdC, eur2, std::nullopt));
        env.close();
        BEAST_EXPECT(expectHolding(env, alice_, eur2(1'000)));

        if (features[fixCleanup3_5_0])
        {
            // The recreated balance is constrained like any other
            // unauthorized balance: it cannot reach a third party...
            env(pay(alice_, bob_, eur2(100)), Ter(tecNO_AUTH));
            env.close();

            // ...but returning it to the issuer stays legal.
            env(pay(alice_, gw2, eur2(1'000)));
            env.close();
        }
        else
        {
            // Pre-amendment the unauthorized balance is freely spendable.
            env(pay(alice_, bob_, eur2(100)));
            env.close();
        }
    }

    void
    testRequireAuthMPT(FeatureBitset features)
    {
        testcase("RequireAuth MPT pool asset");

        using namespace jtx;

        // An MPT can only be an AMM pool asset once featureMPTokensV2 is
        // enabled.
        if (!features[featureMPTokensV2])
            return;

        Env env{*this, features};
        env.fund(XRP(30'000), gw_, alice_, bob_);
        env.close();

        // gw_ issues an MPT that requires authorization; alice_ is
        // authorized, bob_ is not.
        MPTTester btc(env, gw_, {.holders = {alice_, bob_}, .fund = false});
        btc.create(
            {.maxAmt = 1'000'000,
             .authorize = {{alice_}},
             .pay = {{{alice_}, 10'000}},
             .flags = tfMPTRequireAuth | tfMPTCanTrade | tfMPTCanTransfer,
             .authHolder = true});

        AMM const amm(env, alice_, XRP(10'000), btc(10'000));
        auto const lpIssue = amm.lptIssue();

        env.trust(STAmount{lpIssue, 1'000'000}, bob_);
        env.close();

        if (features[fixCleanup3_5_0])
        {
            env(pay(alice_, bob_, STAmount{lpIssue, 100}), Ter(tecNO_AUTH));
            env.close();

            // Authorized for the MPT, bob_ may receive the LPToken.
            btc.authorize({.account = bob_});
            btc.authorize({.account = gw_, .holder = bob_});
            env(pay(alice_, bob_, STAmount{lpIssue, 100}));
            env.close();
            BEAST_EXPECT(expectHolding(env, bob_, STAmount{lpIssue, 100}));
        }
        else
        {
            env(pay(alice_, bob_, STAmount{lpIssue, 100}));
            env.close();
            BEAST_EXPECT(expectHolding(env, bob_, STAmount{lpIssue, 100}));
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
            testMPTCanTransferDirectStep(features);
            testMPTCanTransferOffer(features);
        }

        for (auto const features : {all, all - fixCleanup3_5_0, all - fixEnforceNFTokenTrustlineV2})
        {
            testRequireAuthPayment(features);
            testRequireAuthOffer(features);
            testRequireAuthNFTOffer(features);
            testRequireAuthBid(features);
            testRequireAuthDepositEdges(features);
            testRequireAuthClawback(features);
            testRequireAuthMPT(features);
        }
    }
};

BEAST_DEFINE_TESTSUITE(LPTokenTransfer, app, xrpl);
}  // namespace xrpl::test
