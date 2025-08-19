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

#include <ripple/app/tx/detail/AMMCreate.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/TER.h>
#include <test/jtx.h>

namespace ripple {
namespace test {

struct AMMHybrid_test : public jtx::AMMTest
{
    void
    testHybridAMMCreate()
    {
        testcase("Hybrid AMM Create - Traditional AMM");

        using namespace jtx;

        // Test traditional AMM creation (without concentrated liquidity fields)
        {
            Env env{*this, FeatureBitset{featureAMM}};
            fund(env, gw, {alice}, {USD(1000), BTC(1)}, Fund::All);

            // Create traditional AMM
            auto const ammCreate = env.tx()
                                       .type(ttAMM_CREATE)
                                       .account(alice)
                                       .amount(USD(100))
                                       .amount2(BTC(0.1))
                                       .tradingFee(30)
                                       .fee(XRP(10))
                                       .seq(env.seq(alice));

            env(ammCreate);
            env.close();

            // Verify AMM was created
            auto const ammKeylet = keylet::amm(USD.issue(), BTC.issue());
            auto const ammSle = env.le(ammKeylet);
            BEAST_EXPECT(ammSle);
            
            // Verify it's a traditional AMM (no concentrated liquidity fields)
            BEAST_EXPECT(!ammSle->isFieldPresent(sfCurrentTick));
            BEAST_EXPECT(!ammSle->isFieldPresent(sfTickSpacing));
            BEAST_EXPECT(!ammSle->isFieldPresent(sfAggregatedLiquidity));
        }
    }

    void
    testHybridAMMCreateConcentrated()
    {
        testcase("Hybrid AMM Create - Concentrated Liquidity");

        using namespace jtx;

        // Test concentrated liquidity AMM creation
        {
            Env env{
                *this,
                FeatureBitset{featureAMM, featureAMMConcentratedLiquidity}};
            fund(env, gw, {alice}, {USD(1000), BTC(1)}, Fund::All);

            // Create concentrated liquidity AMM
            auto const ammCreate = env.tx()
                                       .type(ttAMM_CREATE)
                                       .account(alice)
                                       .amount(USD(100))
                                       .amount2(BTC(0.1))
                                       .tradingFee(30)
                                       .tickLower(-1000)
                                       .tickUpper(1000)
                                       .liquidity(IOUAmount{1000000, 0})
                                       .tickSpacing(10)
                                       .fee(XRP(10))
                                       .seq(env.seq(alice));

            env(ammCreate);
            env.close();

            // Verify AMM was created with concentrated liquidity fields
            auto const ammKeylet = keylet::amm(USD.issue(), BTC.issue());
            auto const ammSle = env.le(ammKeylet);
            BEAST_EXPECT(ammSle);
            
            // Verify concentrated liquidity fields are present
            BEAST_EXPECT(ammSle->isFieldPresent(sfCurrentTick));
            BEAST_EXPECT(ammSle->isFieldPresent(sfTickSpacing));
            BEAST_EXPECT(ammSle->isFieldPresent(sfAggregatedLiquidity));
            BEAST_EXPECT(ammSle->isFieldPresent(sfFeeGrowthGlobal0X128));
            BEAST_EXPECT(ammSle->isFieldPresent(sfFeeGrowthGlobal1X128));
            
            // Verify field values
            BEAST_EXPECT(ammSle->getFieldU32(sfTickSpacing) == 10);
            BEAST_EXPECT(ammSle->getFieldU32(sfCurrentTick) == -1000);
            BEAST_EXPECT(ammSle->getFieldAmount(sfAggregatedLiquidity) == IOUAmount{1000000, 0});

            // Verify position was created
            auto const positionKey = getConcentratedLiquidityPositionKey(
                alice.id(), -1000, 1000, 0);
            auto const positionSle = env.le(keylet::unchecked(positionKey));
            BEAST_EXPECT(positionSle);
            BEAST_EXPECT(positionSle->getFieldU32(sfTickLower) == -1000);
            BEAST_EXPECT(positionSle->getFieldU32(sfTickUpper) == 1000);
            BEAST_EXPECT(positionSle->getFieldAmount(sfLiquidity) == IOUAmount{1000000, 0});

            // Verify ticks were initialized
            auto const tickLowerKey = getConcentratedLiquidityTickKey(-1000);
            auto const tickUpperKey = getConcentratedLiquidityTickKey(1000);
            auto const tickLowerSle = env.le(keylet::unchecked(tickLowerKey));
            auto const tickUpperSle = env.le(keylet::unchecked(tickUpperKey));
            BEAST_EXPECT(tickLowerSle);
            BEAST_EXPECT(tickUpperSle);
            BEAST_EXPECT(tickLowerSle->getFieldU8(sfTickInitialized) == 1);
            BEAST_EXPECT(tickUpperSle->getFieldU8(sfTickInitialized) == 1);
        }
    }

    void
    testHybridAMMCreateFeatureDisabled()
    {
        testcase("Hybrid AMM Create - Feature Disabled");

        using namespace jtx;

        // Test that concentrated liquidity fields are rejected when feature is disabled
        {
            Env env{*this, FeatureBitset{featureAMM}};
            fund(env, gw, {alice}, {USD(1000), BTC(1)}, Fund::All);

            // Try to create AMM with concentrated liquidity fields (should fail)
            auto const ammCreate = env.tx()
                                       .type(ttAMM_CREATE)
                                       .account(alice)
                                       .amount(USD(100))
                                       .amount2(BTC(0.1))
                                       .tradingFee(30)
                                       .tickLower(-1000)
                                       .tickUpper(1000)
                                       .liquidity(IOUAmount{1000000, 0})
                                       .tickSpacing(10)
                                       .fee(XRP(10))
                                       .seq(env.seq(alice));

            env(ammCreate, ter(temDISABLED));
            env.close();

            // Verify AMM was not created
            auto const ammKeylet = keylet::amm(USD.issue(), BTC.issue());
            auto const ammSle = env.le(ammKeylet);
            BEAST_EXPECT(!ammSle);
        }
    }

    void
    testHybridAMMCreateValidation()
    {
        testcase("Hybrid AMM Create - Validation");

        using namespace jtx;

        // Test validation of concentrated liquidity parameters
        {
            Env env{
                *this,
                FeatureBitset{featureAMM, featureAMMConcentratedLiquidity}};
            fund(env, gw, {alice}, {USD(1000), BTC(1)}, Fund::All);

            // Test invalid tick range (lower >= upper)
            {
                auto const ammCreate = env.tx()
                                           .type(ttAMM_CREATE)
                                           .account(alice)
                                           .amount(USD(100))
                                           .amount2(BTC(0.1))
                                           .tradingFee(30)
                                           .tickLower(1000)
                                           .tickUpper(1000)  // Same as lower
                                           .liquidity(IOUAmount{1000000, 0})
                                           .tickSpacing(10)
                                           .fee(XRP(10))
                                           .seq(env.seq(alice));

                env(ammCreate, ter(temBAD_AMM_TOKENS));
                env.close();
            }

            // Test invalid tick spacing
            {
                auto const ammCreate = env.tx()
                                           .type(ttAMM_CREATE)
                                           .account(alice)
                                           .amount(USD(100))
                                           .amount2(BTC(0.1))
                                           .tradingFee(30)
                                           .tickLower(-1001)  // Not aligned with spacing
                                           .tickUpper(1000)
                                           .liquidity(IOUAmount{1000000, 0})
                                           .tickSpacing(10)
                                           .fee(XRP(10))
                                           .seq(env.seq(alice));

                env(ammCreate, ter(temBAD_AMM_TOKENS));
                env.close();
            }

            // Test invalid fee tier
            {
                auto const ammCreate = env.tx()
                                           .type(ttAMM_CREATE)
                                           .account(alice)
                                           .amount(USD(100))
                                           .amount2(BTC(0.1))
                                           .tradingFee(999)  // Invalid fee tier
                                           .tickLower(-1000)
                                           .tickUpper(1000)
                                           .liquidity(IOUAmount{1000000, 0})
                                           .tickSpacing(10)
                                           .fee(XRP(10))
                                           .seq(env.seq(alice));

                env(ammCreate, ter(temBAD_FEE));
                env.close();
            }
        }
    }

    void
    run() override
    {
        testHybridAMMCreate();
        testHybridAMMCreateConcentrated();
        testHybridAMMCreateFeatureDisabled();
        testHybridAMMCreateValidation();
    }
};

BEAST_DEFINE_TESTSUITE(AMMHybrid, app, ripple);

}  // namespace test
}  // namespace ripple
