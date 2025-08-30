//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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
#include <test/jtx/AMM.h>
#include <test/jtx/AMMTest.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>

#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/TER.h>

namespace ripple {
namespace test {

struct AMMConcentratedLiquidity_test : public jtx::AMMTest
{
    void
    testConcentratedLiquidityCreate()
    {
        testcase("Concentrated Liquidity Create");

        using namespace jtx;

        // Test with feature enabled
        {
            Env env{
                *this,
                FeatureBitset{featureAMM, featureAMMConcentratedLiquidity}};
            fund(env, gw, {alice}, {USD(1000), BTC(1)}, Fund::All);

            // Create concentrated liquidity AMM
            auto const ammCreate = env.tx()
                                       .type(ttAMM_CONCENTRATED_CREATE)
                                       .account(alice)
                                       .amount(USD(100))
                                       .amount2(BTC(0.1))
                                       .asset(USD)
                                       .asset2(BTC)
                                       .tradingFee(30)
                                       .tickLower(-1000)
                                       .tickUpper(1000)
                                       .liquidity(IOUAmount{1000000, 0})
                                       .tickSpacing(10)
                                       .fee(XRP(10))
                                       .seq(env.seq(alice));

            env(ammCreate);
            env.close();

            // Verify AMM was created
            auto const ammKeylet = keylet::amm(USD.issue(), BTC.issue());
            auto const ammSle = env.le(ammKeylet);
            BEAST_EXPECT(ammSle);
            BEAST_EXPECT(ammSle->getFieldU32(sfTickSpacing) == 10);
            BEAST_EXPECT(ammSle->getFieldU32(sfCurrentTick) == -1000);
        }

        // Test with feature disabled
        {
            Env env{
                *this,
                FeatureBitset{featureAMM}};  // Concentrated liquidity disabled
            fund(env, gw, {alice}, {USD(1000), BTC(1)}, Fund::All);

            auto const ammCreate = env.tx()
                                       .type(ttAMM_CONCENTRATED_CREATE)
                                       .account(alice)
                                       .amount(USD(100))
                                       .amount2(BTC(0.1))
                                       .asset(USD)
                                       .asset2(BTC)
                                       .tradingFee(30)
                                       .tickLower(-1000)
                                       .tickUpper(1000)
                                       .liquidity(IOUAmount{1000000, 0})
                                       .tickSpacing(10)
                                       .fee(XRP(10))
                                       .seq(env.seq(alice));

            env(ammCreate, ter(temDISABLED));
        }
    }

    void
    testConcentratedLiquidityValidation()
    {
        testcase("Concentrated Liquidity Validation");

        using namespace jtx;

        Env env{
            *this, FeatureBitset{featureAMM, featureAMMConcentratedLiquidity}};
        fund(env, gw, {alice}, {USD(1000), BTC(1)}, Fund::All);

        // Test invalid tick range (lower >= upper)
        {
            auto const ammCreate = env.tx()
                                       .type(ttAMM_CONCENTRATED_CREATE)
                                       .account(alice)
                                       .amount(USD(100))
                                       .amount2(BTC(0.1))
                                       .asset(USD)
                                       .asset2(BTC)
                                       .tradingFee(30)
                                       .tickLower(1000)
                                       .tickUpper(1000)  // Same as lower
                                       .liquidity(IOUAmount{1000000, 0})
                                       .tickSpacing(10)
                                       .fee(XRP(10))
                                       .seq(env.seq(alice));

            env(ammCreate, ter(temBAD_AMM_TOKENS));
        }

        // Test invalid tick range (lower > upper)
        {
            auto const ammCreate = env.tx()
                                       .type(ttAMM_CONCENTRATED_CREATE)
                                       .account(alice)
                                       .amount(USD(100))
                                       .amount2(BTC(0.1))
                                       .asset(USD)
                                       .asset2(BTC)
                                       .tradingFee(30)
                                       .tickLower(1000)
                                       .tickUpper(-1000)  // Lower than lower
                                       .liquidity(IOUAmount{1000000, 0})
                                       .tickSpacing(10)
                                       .fee(XRP(10))
                                       .seq(env.seq(alice));

            env(ammCreate, ter(temBAD_AMM_TOKENS));
        }

        // Test invalid tick spacing
        {
            auto const ammCreate = env.tx()
                                       .type(ttAMM_CONCENTRATED_CREATE)
                                       .account(alice)
                                       .amount(USD(100))
                                       .amount2(BTC(0.1))
                                       .asset(USD)
                                       .asset2(BTC)
                                       .tradingFee(30)
                                       .tickLower(-1000)
                                       .tickUpper(1000)
                                       .liquidity(IOUAmount{1000000, 0})
                                       .tickSpacing(0)  // Invalid spacing
                                       .fee(XRP(10))
                                       .seq(env.seq(alice));

            env(ammCreate, ter(temBAD_AMM_TOKENS));
        }

        // Test insufficient liquidity
        {
            auto const ammCreate = env.tx()
                                       .type(ttAMM_CONCENTRATED_CREATE)
                                       .account(alice)
                                       .amount(USD(100))
                                       .amount2(BTC(0.1))
                                       .asset(USD)
                                       .asset2(BTC)
                                       .tradingFee(30)
                                       .tickLower(-1000)
                                       .tickUpper(1000)
                                       .liquidity(IOUAmount{100, 0})  // Too low
                                       .tickSpacing(10)
                                       .fee(XRP(10))
                                       .seq(env.seq(alice));

            env(ammCreate, ter(temBAD_AMM_TOKENS));
        }
    }

    void
    testConcentratedLiquidityCalculations()
    {
        testcase("Concentrated Liquidity Calculations");

        // Test tick to sqrt price conversion
        {
            auto const tick = 1000;
            auto const sqrtPrice = tickToSqrtPriceX64(tick);
            auto const convertedTick = sqrtPriceX64ToTick(sqrtPrice);
            BEAST_EXPECT(
                std::abs(convertedTick - tick) <=
                1);  // Allow small rounding error
        }

        // Test negative tick
        {
            auto const tick = -1000;
            auto const sqrtPrice = tickToSqrtPriceX64(tick);
            auto const convertedTick = sqrtPriceX64ToTick(sqrtPrice);
            BEAST_EXPECT(std::abs(convertedTick - tick) <= 1);
        }

        // Test zero tick
        {
            auto const tick = 0;
            auto const sqrtPrice = tickToSqrtPriceX64(tick);
            auto const convertedTick = sqrtPriceX64ToTick(sqrtPrice);
            BEAST_EXPECT(convertedTick == 0);
        }

        // Test tick range validation
        {
            BEAST_EXPECT(isValidTickRange(-1000, 1000, 10));
            BEAST_EXPECT(!isValidTickRange(1000, -1000, 10));  // Invalid order
            BEAST_EXPECT(!isValidTickRange(-1000, 1000, 0));  // Invalid spacing
            BEAST_EXPECT(
                !isValidTickRange(-1000, 1000, 3));  // Not aligned with spacing
        }

        void testMultipleFeeTiers()
        {
            testcase("Multiple Fee Tiers");

            using namespace jtx;
            using namespace test::jtx;

            Env env(*this);

            Account const alice("alice");
            Account const bob("bob");

            env.fund(XRP(10000), alice, bob);
            env.close();

            // Test all fee tiers
            std::vector<std::pair<std::uint16_t, std::uint16_t>> feeTiers = {
                {CONCENTRATED_LIQUIDITY_FEE_TIER_0_01,
                 CONCENTRATED_LIQUIDITY_TICK_SPACING_0_01},
                {CONCENTRATED_LIQUIDITY_FEE_TIER_0_05,
                 CONCENTRATED_LIQUIDITY_TICK_SPACING_0_05},
                {CONCENTRATED_LIQUIDITY_FEE_TIER_0_3,
                 CONCENTRATED_LIQUIDITY_TICK_SPACING_0_3},
                {CONCENTRATED_LIQUIDITY_FEE_TIER_1_0,
                 CONCENTRATED_LIQUIDITY_TICK_SPACING_1_0}};

            for (auto const& [fee, expectedTickSpacing] : feeTiers)
            {
                // Create AMM with this fee tier
                env(
                    amm(alice,
                        USD(1000),
                        BTC(100),
                        fee,
                        expectedTickSpacing,
                        -1000,
                        1000,
                        1000000));
                env.close();

                // Verify AMM was created with correct fee tier
                auto const ammSle =
                    env.le(keylet::amm(USD.issue(), BTC.issue()));
                BEAST_EXPECT(ammSle);
                BEAST_EXPECT(ammSle->getFieldU16(sfTradingFee) == fee);
                BEAST_EXPECT(
                    ammSle->getFieldU16(sfTickSpacing) == expectedTickSpacing);

                // Verify fee tier validation
                BEAST_EXPECT(isValidConcentratedLiquidityFeeTier(fee));
                BEAST_EXPECT(
                    getConcentratedLiquidityTickSpacing(fee) ==
                    expectedTickSpacing);
                BEAST_EXPECT(
                    getConcentratedLiquidityFeeTier(expectedTickSpacing) ==
                    fee);

                // Test tick validation for this fee tier
                auto const validTick =
                    expectedTickSpacing * 10;  // Multiple of tick spacing
                auto const invalidTick = expectedTickSpacing * 10 +
                    1;  // Not multiple of tick spacing

                BEAST_EXPECT(isValidTickForFeeTier(validTick, fee));
                BEAST_EXPECT(!isValidTickForFeeTier(invalidTick, fee));

                // Clean up for next iteration
                env(ammDelete(alice, USD, BTC));
                env.close();
            }

            // Test invalid fee tier
            BEAST_EXPECT(
                !isValidConcentratedLiquidityFeeTier(999));  // Invalid fee
            BEAST_EXPECT(
                !isValidConcentratedLiquidityFeeTier(1001));  // Invalid fee
        }
    }

    void
    testConcentratedLiquidityPositionManagement()
    {
        testcase("Concentrated Liquidity Position Management");

        using namespace jtx;

        Env env{
            *this, FeatureBitset{featureAMM, featureAMMConcentratedLiquidity}};
        fund(env, gw, {alice, bob}, {USD(1000), BTC(1)}, Fund::All);

        // Create concentrated liquidity AMM
        auto const ammCreate = env.tx()
                                   .type(ttAMM_CONCENTRATED_CREATE)
                                   .account(alice)
                                   .amount(USD(100))
                                   .amount2(BTC(0.1))
                                   .asset(USD)
                                   .asset2(BTC)
                                   .tradingFee(30)
                                   .tickLower(-1000)
                                   .tickUpper(1000)
                                   .liquidity(IOUAmount{1000000, 0})
                                   .tickSpacing(10)
                                   .fee(XRP(10))
                                   .seq(env.seq(alice));

        env(ammCreate);
        env.close();

        // Verify position was created
        auto const positionKey =
            getConcentratedLiquidityPositionKey(alice.id(), -1000, 1000, 0);
        auto const positionKeylet = keylet::child(positionKey);
        auto const positionSle = env.le(positionKeylet);
        BEAST_EXPECT(positionSle);
        BEAST_EXPECT(positionSle->getFieldAccount(sfAccount) == alice.id());
        BEAST_EXPECT(positionSle->getFieldU32(sfTickLower) == -1000);
        BEAST_EXPECT(positionSle->getFieldU32(sfTickUpper) == 1000);
        BEAST_EXPECT(
            positionSle->getFieldAmount(sfLiquidity) == IOUAmount{1000000, 0});

        // Verify ticks were initialized
        auto const tickLowerKey = getConcentratedLiquidityTickKey(-1000);
        auto const tickUpperKey = getConcentratedLiquidityTickKey(1000);
        auto const tickLowerSle = env.le(keylet::child(tickLowerKey));
        auto const tickUpperSle = env.le(keylet::child(tickUpperKey));
        BEAST_EXPECT(tickLowerSle);
        BEAST_EXPECT(tickUpperSle);
        BEAST_EXPECT(tickLowerSle->getFieldU8(sfTickInitialized) == true);
        BEAST_EXPECT(tickUpperSle->getFieldU8(sfTickInitialized) == true);
    }

    void
    testConcentratedLiquidityIntegration()
    {
        testcase("Concentrated Liquidity Integration");

        using namespace jtx;

        Env env{
            *this, FeatureBitset{featureAMM, featureAMMConcentratedLiquidity}};
        fund(env, gw, {alice, bob}, {USD(1000), BTC(1)}, Fund::All);

        // Create concentrated liquidity AMM
        auto const ammCreate = env.tx()
                                   .type(ttAMM_CONCENTRATED_CREATE)
                                   .account(alice)
                                   .amount(USD(100))
                                   .amount2(BTC(0.1))
                                   .asset(USD)
                                   .asset2(BTC)
                                   .tradingFee(30)
                                   .tickLower(-1000)
                                   .tickUpper(1000)
                                   .liquidity(IOUAmount{1000000, 0})
                                   .tickSpacing(10)
                                   .fee(XRP(10))
                                   .seq(env.seq(alice));

        env(ammCreate);
        env.close();

        // Verify AMM account was created
        auto const ammKeylet = keylet::amm(USD.issue(), BTC.issue());
        auto const ammSle = env.le(ammKeylet);
        BEAST_EXPECT(ammSle);

        auto const ammAccountID = ammSle->getFieldAccount(sfAccount);
        BEAST_EXPECT(ammAccountID != alice.id());

        // Verify AMM account has the assets
        auto const ammAccountSle = env.le(keylet::account(ammAccountID));
        BEAST_EXPECT(ammAccountSle);
        BEAST_EXPECT(ammAccountSle->getFieldAmount(sfBalance) == XRP(100));

        // Verify trust lines were created

        // Test position directory management
        auto const ownerDir = env.le(keylet::ownerDir(alice.id()));
        BEAST_EXPECT(ownerDir);
        bool foundPosition = false;
        for (auto const& item : ownerDir->getFieldV256(sfIndexes))
        {
            auto const sle = env.le(keylet::child(item));
            if (sle && sle->getType() == ltCONCENTRATED_LIQUIDITY_POSITION)
            {
                foundPosition = true;
                break;
            }
        }
        BEAST_EXPECT(foundPosition);

        // Test AMM directory integration
        auto const ammDir = env.le(keylet::ammDir(USD.issue(), BTC.issue()));
        BEAST_EXPECT(ammDir);
        bool foundAMM = false;
        for (auto const& item : ammDir->getFieldV256(sfIndexes))
        {
            auto const sle = env.le(keylet::child(item));
            if (sle && sle->getType() == ltAMM)
            {
                foundAMM = true;
                break;
            }
        }
        BEAST_EXPECT(foundAMM);
        auto const usdTrustLine =
            env.le(keylet::line(ammAccountID, USD.issue()));
        auto const btcTrustLine =
            env.le(keylet::line(ammAccountID, BTC.issue()));
        BEAST_EXPECT(usdTrustLine);
        BEAST_EXPECT(btcTrustLine);
        BEAST_EXPECT(usdTrustLine->getFieldAmount(sfBalance) == USD(100));
        BEAST_EXPECT(btcTrustLine->getFieldAmount(sfBalance) == BTC(0.1));
    }

    void
    run() override
    {
        testConcentratedLiquidityCreate();
        testConcentratedLiquidityValidation();
        testConcentratedLiquidityCalculations();
        testConcentratedLiquidityPositionManagement();
        testConcentratedLiquidityIntegration();
        testMultipleFeeTiers();
    }
};

BEAST_DEFINE_TESTSUITE(AMMConcentratedLiquidity, app, ripple);

}  // namespace test
}  // namespace ripple
