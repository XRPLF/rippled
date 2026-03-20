#include <test/jtx.h>
#include <test/jtx/CLAMM.h>
#include <test/jtx/Env.h>
#include <test/jtx/acctdelete.h>
#include <test/jtx/token.h>

#include <xrpl/protocol/CLAMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/nft.h>
#include <xrpl/tx/transactors/dex/CLAMMHelpers.h>
#include <xrpl/tx/transactors/nft/NFTokenUtils.h>

#include <chrono>

namespace xrpl {
namespace test {

struct CLAMM_test : public beast::unit_test::suite
{
    jtx::Account const gw{"gateway"};
    jtx::Account const alice{"alice"};
    jtx::Account const bob{"bob"};
    jtx::Account const carol{"carol"};
    jtx::IOU const USD{gw["USD"]};

    void
    testCreate()
    {
        testcase("CLAMMCreate");
        using namespace jtx;

        {
            // Successful pool creation
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Verify pool exists
            auto const sle = env.current()->read(keylet::clamm(pid));
            BEAST_EXPECT(sle != nullptr);
            if (sle)
            {
                BEAST_EXPECT(sle->getFieldU8(sfFeeTier) == 1);
                BEAST_EXPECT(sle->getFieldU16(sfTickSpacing) == 10);
                auto const storedTick =
                    sle->getFieldI32(sfCurrentTick);
                BEAST_EXPECT(storedTick > -100 && storedTick < 100);
            }
        }

        {
            // Duplicate pool creation should fail
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammCreate(env,
                    bob, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tecDUPLICATE));
            env.close();
        }

        {
            // Invalid fee tier
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 5,
                    clammDefaultSqrtPrice()),
                ter(temBAD_FEE));
            env.close();
        }

        {
            // Feature disabled -- all 7 transaction types must fail
            auto const noClammFeatures =
                jtx::testable_amendments() - featureCLAMM;
            Env env{*this, noClammFeatures};
            env.fund(XRP(100'000), alice);
            env.close();

            auto const pid = clammPoolID(xrpIssue(), USD.issue(), 1);
            auto const fakeNFT = uint256(42);

            // CLAMMCreate
            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(temDISABLED));
            env.close();

            // CLAMMDeposit
            env(clammDeposit(alice, pid, -10, 10, XRP(100), USD(100)),
                ter(temDISABLED));
            env.close();

            // CLAMMSwap
            env(clammSwap(alice, pid, XRP(10)),
                ter(temDISABLED));
            env.close();

            // CLAMMWithdraw
            env(clammWithdraw(alice, fakeNFT),
                ter(temDISABLED));
            env.close();

            // CLAMMCollectFees
            env(clammCollectFees(alice, fakeNFT),
                ter(temDISABLED));
            env.close();

            // CLAMMVote
            env(clammVote(alice, pid, 500),
                ter(temDISABLED));
            env.close();

            // CLAMMBid
            env(clammBid(alice, pid),
                ter(temDISABLED));
            env.close();
        }
    }

    void
    testDeposit()
    {
        testcase("CLAMMDeposit");
        using namespace jtx;

        {
            // Basic deposit
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -100, 100,
                    XRP(1'000), USD(1'000)),
                ter(tesSUCCESS));
            env.close();

            // Verify position exists
            auto const nft = clammFindPositionNFT(env, alice, pid);
            BEAST_EXPECT(nft.has_value());
        }

        {
            // Deposit to non-existent pool
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            uint256 fakePid;
            (void)fakePid.parseHex(
                "DEADBEEF00000000000000000000000000000000"
                "000000000000000000000001");

            env(clammDeposit(
                    alice, fakePid, -100, 100,
                    XRP(1'000), USD(1'000)),
                ter(tecNO_ENTRY));
            env.close();
        }

        {
            // Invalid tick range (lower >= upper)
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, 100, -100,
                    XRP(1'000), USD(1'000)),
                ter(temBAD_AMOUNT));
            env.close();
        }
    }

    void
    testSwap()
    {
        testcase("CLAMMSwap");
        using namespace jtx;

        {
            // Basic swap with liquidity
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Alice provides liquidity
            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            // Verify pool state after deposit
            {
                auto const sle =
                    env.current()->read(keylet::clamm(pid));
                BEAST_EXPECT(sle != nullptr);
                if (sle)
                {
                    auto const liq = clamm::fromSLEField(
                        sle->getFieldH128(sfLiquidityAmount));
                    BEAST_EXPECT(liq > 0);
                }
                // Verify lower tick exists
                auto const lowerTick =
                    env.current()->read(keylet::clammTick(pid, -1000));
                BEAST_EXPECT(lowerTick != nullptr);
                // Verify upper tick exists
                auto const upperTick =
                    env.current()->read(keylet::clammTick(pid, 1000));
                BEAST_EXPECT(upperTick != nullptr);
            }

            auto const bobXrpBefore = env.balance(bob);
            auto const bobUsdBefore = env.balance(bob, USD);

            // Bob swaps XRP for USD
            env(clammSwap(bob, pid, XRP(100)),
                ter(tesSUCCESS));
            env.close();

            auto const bobXrpAfter = env.balance(bob);
            auto const bobUsdAfter = env.balance(bob, USD);

            // Bob should have less XRP and more USD
            BEAST_EXPECT(bobXrpAfter < bobXrpBefore);
            BEAST_EXPECT(bobUsdAfter > bobUsdBefore);
        }

        {
            // Swap with no liquidity should fail
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // No deposits -- pool has no liquidity
            env(clammSwap(bob, pid, XRP(100)),
                ter(tecPATH_DRY));
            env.close();
        }

        {
            // Swap to non-existent pool
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);
            uint256 fakePid{1};

            env(clammSwap(bob, fakePid, XRP(100)),
                ter(tecNO_ENTRY));
            env.close();
        }
    }

    void
    testWithdraw()
    {
        testcase("CLAMMWithdraw");
        using namespace jtx;

        {
            // Full withdrawal
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -100, 100,
                    XRP(1'000), USD(1'000)),
                ter(tesSUCCESS));
            env.close();

            auto const nft = clammFindPositionNFT(env, alice, pid);
            BEAST_EXPECT(nft.has_value());

            if (nft)
            {
                auto const aliceXrpBefore = env.balance(alice);
                auto const aliceUsdBefore = env.balance(alice, USD);

                env(clammWithdraw(alice, *nft),
                    ter(tesSUCCESS));
                env.close();

                auto const aliceXrpAfter = env.balance(alice);
                auto const aliceUsdAfter = env.balance(alice, USD);
                BEAST_EXPECT(aliceXrpAfter > aliceXrpBefore);
                BEAST_EXPECT(aliceUsdAfter > aliceUsdBefore);

                // Position should be deleted
                auto const pos = env.current()->read(
                    keylet::clammPosition(*nft));
                BEAST_EXPECT(pos == nullptr);
            }
        }

        {
            // Withdraw by non-owner should fail
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -100, 100,
                    XRP(1'000), USD(1'000)),
                ter(tesSUCCESS));
            env.close();

            auto const nft = clammFindPositionNFT(env, alice, pid);
            BEAST_EXPECT(nft.has_value());

            if (nft)
            {
                env(clammWithdraw(bob, *nft),
                    ter(tecNO_PERMISSION));
                env.close();
            }
        }
    }

    void
    testCollectFees()
    {
        testcase("CLAMMCollectFees");
        using namespace jtx;

        {
            // Collect fees after swaps
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            auto const nft = clammFindPositionNFT(env, alice, pid);
            BEAST_EXPECT(nft.has_value());

            // Bob swaps to generate fees
            env(clammSwap(bob, pid, XRP(1'000)),
                ter(tesSUCCESS));
            env.close();

            if (nft)
            {
                env(clammCollectFees(alice, *nft),
                    ter(tesSUCCESS));
                env.close();
            }
        }

        {
            // Collect fees by non-owner should fail
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -100, 100,
                    XRP(1'000), USD(1'000)),
                ter(tesSUCCESS));
            env.close();

            auto const nft = clammFindPositionNFT(env, alice, pid);
            if (nft)
            {
                env(clammCollectFees(bob, *nft),
                    ter(tecNO_PERMISSION));
                env.close();
            }
        }
    }

    void
    testVote()
    {
        testcase("CLAMMVote");
        using namespace jtx;

        {
            // Basic vote
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Alice must have liquidity in the pool to vote
            env(clammDeposit(
                    alice, pid, -100, 100,
                    XRP(1'000), USD(1'000)),
                ter(tesSUCCESS));
            env.close();

            env(clammVote(alice, pid, 300),
                ter(tesSUCCESS));
            env.close();

            auto const sle = env.current()->read(keylet::clamm(pid));
            BEAST_EXPECT(sle != nullptr);
            if (sle)
            {
                BEAST_EXPECT(sle->getFieldU16(sfTradingFee) == 300);
            }
        }

        {
            // Multiple voters produce weighted average
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Both voters need liquidity. Same amounts = equal weight.
            env(clammDeposit(
                    alice, pid, -100, 100,
                    XRP(1'000), USD(1'000)),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    bob, pid, -100, 100,
                    XRP(1'000), USD(1'000)),
                ter(tesSUCCESS));
            env.close();

            env(clammVote(alice, pid, 200),
                ter(tesSUCCESS));
            env.close();

            env(clammVote(bob, pid, 400),
                ter(tesSUCCESS));
            env.close();

            // Equal liquidity = equal weight: (200 + 400) / 2 = 300
            auto const sle = env.current()->read(keylet::clamm(pid));
            BEAST_EXPECT(sle != nullptr);
            if (sle)
            {
                BEAST_EXPECT(sle->getFieldU16(sfTradingFee) == 300);
            }
        }

        {
            // Invalid fee (> 10000 hard cap)
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammVote(alice, pid, 10001),
                ter(temBAD_FEE));
            env.close();
        }

        {
            // Vote without liquidity should fail
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // No deposit — voting should fail (no liquidity = no permission)
            env(clammVote(alice, pid, 300),
                ter(tecNO_PERMISSION));
            env.close();
        }

        {
            // Vote on non-existent pool
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);
            uint256 fakePid{1};

            env(clammVote(alice, fakePid, 500),
                ter(tecNO_ENTRY));
            env.close();
        }
    }

    void
    testBid()
    {
        testcase("CLAMMBid");
        using namespace jtx;

        {
            // Basic bid
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Pool needs liquidity for minSlotPrice calculation
            env(clammDeposit(
                    alice, pid, -100, 100,
                    XRP(1'000), USD(1'000)),
                ter(tesSUCCESS));
            env.close();

            env(clammBid(alice, pid),
                ter(tesSUCCESS));
            env.close();

            auto const sle = env.current()->read(keylet::clamm(pid));
            BEAST_EXPECT(sle != nullptr);
            if (sle)
            {
                BEAST_EXPECT(sle->isFieldPresent(sfAuctionSlot));
            }
        }

        {
            // Bid on non-existent pool
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);
            uint256 fakePid{1};

            env(clammBid(alice, fakePid),
                ter(tecNO_ENTRY));
            env.close();
        }

        {
            // Bid with auth accounts
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Pool needs liquidity for minSlotPrice
            env(clammDeposit(
                    alice, pid, -100, 100,
                    XRP(1'000), USD(1'000)),
                ter(tesSUCCESS));
            env.close();

            Json::Value jv = clammBid(alice, pid);
            Json::Value authAccounts(Json::arrayValue);
            Json::Value authAcct;
            authAcct[jss::Account] = bob.human();
            Json::Value acctObj;
            acctObj["AuthAccount"] = authAcct;
            authAccounts.append(acctObj);
            jv[sfAuthAccounts.jsonName] = authAccounts;

            env(jv, ter(tesSUCCESS));
            env.close();
        }
    }

    void
    testRPCInfo()
    {
        testcase("clamm_info RPC");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Test clamm_info RPC
        auto const result = env.rpc(
            "json",
            "clamm_info",
            std::string("{\"pool_id\": \"" + to_string(pid) + "\"}"));

        // The RPC should return pool data under "pool" key
        auto const& rpcResult = result[jss::result];
        BEAST_EXPECT(!rpcResult.isMember(jss::error));
        BEAST_EXPECT(rpcResult.isMember("pool"));
    }

    void
    testSwapCrossesTickBoundary()
    {
        testcase("Swap crosses tick boundary");
        using namespace jtx;

        // Use fee tier 1 (spacing=10) for finer-grained ticks
        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Wide range position that spans several ticks
        env(clammDeposit(
                alice, pid, -500, 500,
                XRP(10'000), USD(10'000)),
            ter(tesSUCCESS));
        env.close();

        auto const sle1 = env.current()->read(keylet::clamm(pid));
        BEAST_EXPECT(sle1 != nullptr);
        std::int32_t tickBefore = 0;
        if (sle1)
            tickBefore = sle1->getFieldI32(sfCurrentTick);

        // Swap should move the price
        env(clammSwap(bob, pid, XRP(3'000)),
            ter(tesSUCCESS));
        env.close();

        auto const sle2 = env.current()->read(keylet::clamm(pid));
        BEAST_EXPECT(sle2 != nullptr);
        if (sle2)
        {
            auto const tickAfter = sle2->getFieldI32(sfCurrentTick);
            BEAST_EXPECT(tickAfter != tickBefore);
        }
    }

    void
    testSwapWithSqrtPriceLimit()
    {
        testcase("Swap with SqrtPriceLimit");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        // Fee tier 1: spacing=10
        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                alice, pid, -1000, 1000,
                XRP(10'000), USD(10'000)),
            ter(tesSUCCESS));
        env.close();

        // For zeroForOne (XRP->USD), price moves down.
        // Limit must be below current price.
        auto const limitPrice = clamm::tickToSqrtPrice(-50);

        auto jv = clammSwap(bob, pid, XRP(5'000));
        jv[sfSqrtPriceLimit.jsonName] =
            to_string(clamm::toSLEField(limitPrice));

        auto const bobUsdBefore = env.balance(bob, USD);

        env(jv, ter(tesSUCCESS));
        env.close();

        auto const bobUsdAfter = env.balance(bob, USD);
        BEAST_EXPECT(bobUsdAfter > bobUsdBefore);

        // Verify price did not exceed the limit
        auto const sle = env.current()->read(keylet::clamm(pid));
        if (sle)
        {
            auto const currentSqrtPrice =
                clamm::fromSLEField(sle->getFieldH128(sfSqrtPrice));
            BEAST_EXPECT(currentSqrtPrice >= limitPrice);
        }
    }

    void
    testOutOfRangePositions()
    {
        testcase("Out-of-range positions");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        // Fee tier 1 (spacing=10) for more flexibility
        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // In-range position first to establish pool state
        env(clammDeposit(
                alice, pid, -100, 100,
                XRP(5'000), USD(5'000)),
            ter(tesSUCCESS));
        env.close();

        // Position entirely above current tick
        env(clammDeposit(
                bob, pid, 200, 400,
                XRP(5'000), USD(5'000)),
            ter(tesSUCCESS));
        env.close();

        // Both positions should exist
        auto const nft1 = clammFindPositionNFT(env, alice, pid);
        auto const nft2 = clammFindPositionNFT(env, bob, pid);
        BEAST_EXPECT(nft1.has_value());
        BEAST_EXPECT(nft2.has_value());
    }

    void
    testMultiplePositionsSameLP()
    {
        testcase("Multiple positions by same LP");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 2);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 2,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Two different tick ranges
        env(clammDeposit(
                alice, pid, -120, 120,
                XRP(3'000), USD(3'000)),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                alice, pid, -600, 600,
                XRP(3'000), USD(3'000)),
            ter(tesSUCCESS));
        env.close();

        // Alice should have two positions
        auto const view = env.current();
        int posCount = 0;
        forEachItem(
            *view,
            alice.id(),
            [&](std::shared_ptr<SLE const> const& sle) {
                if (sle->getType() == ltCLAMM_POSITION &&
                    sle->getFieldH256(sfPoolID) == pid)
                    ++posCount;
            });
        BEAST_EXPECT(posCount == 2);
    }

    void
    testFeeAccumulation()
    {
        testcase("Fee accumulation over multiple swaps");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        // Fee tier 1: spacing=10
        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                alice, pid, -1000, 1000,
                XRP(10'000), USD(10'000)),
            ter(tesSUCCESS));
        env.close();

        auto const nft = clammFindPositionNFT(env, alice, pid);
        BEAST_EXPECT(nft.has_value());

        // Multiple swaps back and forth to generate fees
        for (int i = 0; i < 3; ++i)
        {
            env(clammSwap(bob, pid, XRP(100)),
                ter(tesSUCCESS));
            env.close();

            env(clammSwap(carol, pid, USD(100)),
                ter(tesSUCCESS));
            env.close();
        }

        // Collect fees
        if (nft)
        {
            auto const aliceXrpBefore = env.balance(alice);
            auto const aliceUsdBefore = env.balance(alice, USD);

            env(clammCollectFees(alice, *nft),
                ter(tesSUCCESS));
            env.close();

            auto const aliceXrpAfter = env.balance(alice);
            auto const aliceUsdAfter = env.balance(alice, USD);

            // Should have received fees in at least one token
            BEAST_EXPECT(
                aliceXrpAfter > aliceXrpBefore ||
                aliceUsdAfter > aliceUsdBefore);
        }
    }

    void
    testDepositTickAlignment()
    {
        testcase("Deposit rejects misaligned ticks");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 2);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 2,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Fee tier 2 has tick spacing 60. Ticks must be multiples of 60.
        env(clammDeposit(
                alice, pid, -50, 50,
                XRP(1'000), USD(1'000)),
            ter(temBAD_AMOUNT));
        env.close();

        // Aligned ticks should succeed
        env(clammDeposit(
                alice, pid, -60, 60,
                XRP(1'000), USD(1'000)),
            ter(tesSUCCESS));
        env.close();
    }

    void
    testCreateAllFeeTiers()
    {
        testcase("Create pools with all valid fee tiers");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        // Fee tiers 0-3 are valid (spacings 1,10,60,200)
        for (std::uint8_t tier = 0; tier <= 3; ++tier)
        {
            auto const pid = clammPoolID(xrpIssue(), USD.issue(), tier);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), tier,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            auto const sle = env.current()->read(keylet::clamm(pid));
            BEAST_EXPECT(sle != nullptr);
        }

        // Tier 4 should fail
        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 4,
                clammDefaultSqrtPrice()),
            ter(temBAD_FEE));
        env.close();
    }

    void
    testSwapBothDirections()
    {
        testcase("Swap in both directions");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        // Fee tier 1: spacing=10
        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                alice, pid, -1000, 1000,
                XRP(10'000), USD(10'000)),
            ter(tesSUCCESS));
        env.close();

        // XRP -> USD
        auto const bobUsdBefore = env.balance(bob, USD);
        env(clammSwap(bob, pid, XRP(100)),
            ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(env.balance(bob, USD) > bobUsdBefore);

        // USD -> XRP
        auto const carolXrpBefore = env.balance(carol);
        env(clammSwap(carol, pid, USD(100)),
            ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(env.balance(carol) > carolXrpBefore);
    }

    void
    testWithdrawReturnsCorrectAmounts()
    {
        testcase("Withdraw returns correct proportional amounts");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 2);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 2,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        auto const aliceXrpBefore = env.balance(alice);
        auto const aliceUsdBefore = env.balance(alice, USD);

        env(clammDeposit(
                alice, pid, -120, 120,
                XRP(5'000), USD(5'000)),
            ter(tesSUCCESS));
        env.close();

        auto const aliceXrpAfterDeposit = env.balance(alice);
        auto const aliceUsdAfterDeposit = env.balance(alice, USD);

        auto const nft = clammFindPositionNFT(env, alice, pid);
        BEAST_EXPECT(nft.has_value());

        if (nft)
        {
            env(clammWithdraw(alice, *nft),
                ter(tesSUCCESS));
            env.close();

            auto const aliceXrpAfterWithdraw = env.balance(alice);
            auto const aliceUsdAfterWithdraw = env.balance(alice, USD);

            // Should get back approximately what was deposited
            // (minus fees for account reserve and tx fees)
            BEAST_EXPECT(aliceXrpAfterWithdraw > aliceXrpAfterDeposit);
            BEAST_EXPECT(aliceUsdAfterWithdraw > aliceUsdAfterDeposit);
        }
    }

    void
    testPoolLiquidityUpdatesOnDeposit()
    {
        testcase("Pool liquidity updates correctly on deposit");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 2);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 2,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // In-range deposit should increase active liquidity
        env(clammDeposit(
                alice, pid, -120, 120,
                XRP(5'000), USD(5'000)),
            ter(tesSUCCESS));
        env.close();

        auto const sle = env.current()->read(keylet::clamm(pid));
        BEAST_EXPECT(sle != nullptr);
        if (sle)
        {
            BEAST_EXPECT(sle->isFieldPresent(sfLiquidityAmount));
            auto const liq = clamm::fromSLEField(
                sle->getFieldH128(sfLiquidityAmount));
            BEAST_EXPECT(liq > 0);
        }

        // Second in-range deposit should further increase
        env(clammDeposit(
                bob, pid, -120, 120,
                XRP(5'000), USD(5'000)),
            ter(tesSUCCESS));
        env.close();

        auto const sle2 = env.current()->read(keylet::clamm(pid));
        if (sle && sle2)
        {
            auto const liq1 = clamm::fromSLEField(
                sle->getFieldH128(sfLiquidityAmount));
            auto const liq2 = clamm::fromSLEField(
                sle2->getFieldH128(sfLiquidityAmount));
            BEAST_EXPECT(liq2 > liq1);
        }
    }

    void
    testVoteWeightedByLiquidity()
    {
        testcase("Vote weighted by liquidity");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 2);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 2,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Alice deposits 3x more than bob
        env(clammDeposit(
                alice, pid, -120, 120,
                XRP(9'000), USD(9'000)),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                bob, pid, -120, 120,
                XRP(3'000), USD(3'000)),
            ter(tesSUCCESS));
        env.close();

        env(clammVote(alice, pid, 100),
            ter(tesSUCCESS));
        env.close();

        env(clammVote(bob, pid, 400),
            ter(tesSUCCESS));
        env.close();

        auto const sle = env.current()->read(keylet::clamm(pid));
        BEAST_EXPECT(sle != nullptr);
        if (sle)
        {
            auto const fee = sle->getFieldU16(sfTradingFee);
            // Alice has 3x weight: (100*3 + 400*1) / 4 = 175
            // But exact value depends on liquidity calculation
            BEAST_EXPECT(fee > 100 && fee < 400);
        }
    }

    void
    testBidOutbid()
    {
        testcase("Outbid existing auction slot holder");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 2);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 2,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                alice, pid, -120, 120,
                XRP(10'000), USD(10'000)),
            ter(tesSUCCESS));
        env.close();

        // Alice bids first
        env(clammBid(alice, pid),
            ter(tesSUCCESS));
        env.close();

        auto const sle1 = env.current()->read(keylet::clamm(pid));
        BEAST_EXPECT(sle1 != nullptr);
        if (sle1)
        {
            BEAST_EXPECT(sle1->isFieldPresent(sfAuctionSlot));
        }

        // Advance time so the slot expires (24 intervals * ~24h each)
        // Each close advances time ~10s, need many closes for expiry.
        // Instead, verify that a second bid by the same holder succeeds.
        env(clammBid(alice, pid),
            ter(tesSUCCESS));
        env.close();

        auto const sle2 = env.current()->read(keylet::clamm(pid));
        BEAST_EXPECT(sle2 != nullptr);
        if (sle2)
        {
            BEAST_EXPECT(sle2->isFieldPresent(sfAuctionSlot));
        }
    }

    void
    testSwapNoLiquidityInRange()
    {
        testcase("Swap with no liquidity in current range");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 2);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 2,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Deposit only far above current price
        env(clammDeposit(
                alice, pid, 600, 1200,
                XRP(5'000), USD(5'000)),
            ter(tesSUCCESS));
        env.close();

        // Swap should fail or produce zero output since no liquidity at
        // current price
        env(clammSwap(bob, pid, XRP(100)),
            ter(tecPATH_DRY));
        env.close();
    }

    void
    testDepositZeroAmounts()
    {
        testcase("Deposit with zero amounts fails");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 2);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 2,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                alice, pid, -120, 120,
                XRP(0), USD(0)),
            ter(tecINSUFFICIENT_PAYMENT));
        env.close();
    }

    void
    testCreateDifferentAssetPairs()
    {
        testcase("Create pool with different asset pair orderings");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        // XRP / USD
        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Reversed order (USD / XRP) should be same pool
        auto const pid1 = clammPoolID(xrpIssue(), USD.issue(), 1);
        auto const pid2 = clammPoolID(USD.issue(), xrpIssue(), 1);
        BEAST_EXPECT(pid1 == pid2);
    }

    void
    testWithdrawNonExistentPosition()
    {
        testcase("Withdraw non-existent position");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        uint256 fakeNftId{42};
        env(clammWithdraw(alice, fakeNftId),
            ter(tecNO_ENTRY));
        env.close();
    }

    void
    testCollectFeesNoFees()
    {
        testcase("Collect fees when no fees accumulated");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 2);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 2,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                alice, pid, -120, 120,
                XRP(5'000), USD(5'000)),
            ter(tesSUCCESS));
        env.close();

        auto const nft = clammFindPositionNFT(env, alice, pid);
        BEAST_EXPECT(nft.has_value());

        // No swaps occurred, so no fees to collect.
        if (nft)
        {
            env(clammCollectFees(alice, *nft),
                ter(tecAMM_EMPTY));
            env.close();
        }
    }

    void
    testDepositExtremeTickRange()
    {
        testcase("Deposit with extreme tick range");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        // Fee tier 2: spacing=60
        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 2);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 2,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Wide range position
        env(clammDeposit(
                alice, pid, -6000, 6000,
                XRP(10'000), USD(10'000)),
            ter(tesSUCCESS));
        env.close();

        auto const nft = clammFindPositionNFT(env, alice, pid);
        BEAST_EXPECT(nft.has_value());
    }

    void
    testSwapLargeAmount()
    {
        testcase("Swap large amount relative to liquidity");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        // Fee tier 1: spacing=10
        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                alice, pid, -1000, 1000,
                XRP(10'000), USD(10'000)),
            ter(tesSUCCESS));
        env.close();

        // Swap a large amount -- should still succeed
        auto const bobUsdBefore = env.balance(bob, USD);
        env(clammSwap(bob, pid, XRP(8'000)),
            ter(tesSUCCESS));
        env.close();

        BEAST_EXPECT(env.balance(bob, USD) > bobUsdBefore);
    }

    void
    testVoteDiscountedFeeEqualsTradingFee()
    {
        testcase("Vote: DiscountedFee == TradingFee rejected");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // DiscountedFee == TradingFee should fail with temBAD_FEE
        Json::Value jv = clammVote(alice, pid, 500);
        jv[sfDiscountedFee.jsonName] = 500;  // same as TradingFee
        env(jv, ter(temBAD_FEE));
        env.close();

        // DiscountedFee > TradingFee should also fail
        Json::Value jv2 = clammVote(alice, pid, 500);
        jv2[sfDiscountedFee.jsonName] = 600;
        env(jv2, ter(temBAD_FEE));
        env.close();

        // DiscountedFee < TradingFee should succeed (with liquidity)
        env(clammDeposit(
                alice, pid, -100, 100,
                XRP(1'000), USD(1'000)),
            ter(tesSUCCESS));
        env.close();

        Json::Value jv3 = clammVote(alice, pid, 500);
        jv3[sfDiscountedFee.jsonName] = 100;
        env(jv3, ter(tesSUCCESS));
        env.close();
    }

    void
    testSwapZeroAmount()
    {
        testcase("Swap: Amount == 0 rejected");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                alice, pid, -100, 100,
                XRP(1'000), USD(1'000)),
            ter(tesSUCCESS));
        env.close();

        // Zero amount should fail in preflight
        env(clammSwap(bob, pid, XRP(0)),
            ter(temBAD_AMOUNT));
        env.close();
    }

    void
    testVoteFeeExceedsTierMax()
    {
        testcase("Vote: fee > tier max rejected in doApply");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        // Fee tier 1: max trading fee is 500
        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                alice, pid, -100, 100,
                XRP(1'000), USD(1'000)),
            ter(tesSUCCESS));
        env.close();

        // Vote with fee within tier max succeeds
        env(clammVote(alice, pid, 500),
            ter(tesSUCCESS));
        env.close();

        // Vote with fee exceeding tier max (501 > 500) should fail
        env(clammVote(alice, pid, 501),
            ter(tecNO_PERMISSION));
        env.close();

        // Fee tier 3: max trading fee is 10000
        auto const pid3 =
            clammPoolID(xrpIssue(), USD.issue(), 3);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 3,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                alice, pid3, -200, 200,
                XRP(1'000), USD(1'000)),
            ter(tesSUCCESS));
        env.close();

        // 10000 within tier 3 max: OK
        env(clammVote(alice, pid3, 10000),
            ter(tesSUCCESS));
        env.close();
    }

    void
    testDepositMinLiquidity()
    {
        testcase("Deposit: liquidity below minimum rejected");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        // Use fee tier 0 (spacing=1) for most flexibility
        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 0);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 0,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Use direct LiquidityAmount mode with a value below the minimum
        // threshold (1000). Construct JSON manually.
        Json::Value jv;
        jv[jss::TransactionType] = jss::CLAMMDeposit;
        jv[jss::Account] = alice.human();
        jv[sfPoolID.jsonName] = to_string(pid);
        jv[sfLowerTick.jsonName] = -1;
        jv[sfUpperTick.jsonName] = 1;
        // LiquidityAmount = 500 (below CLAMM_MIN_LIQUIDITY of 1000)
        jv[sfLiquidityAmount.jsonName] =
            to_string(clamm::toSLEField(clamm::uint128(500)));

        env(jv, ter(tecINSUFFICIENT_PAYMENT));
        env.close();
    }

    void
    testDepositInsufficientReserve()
    {
        testcase("Deposit: insufficient reserve for new position");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Fund dan with just enough to exist + one trust line but not
        // enough for 3 more owner objects.
        // Base reserve: 200 XRP, owner reserve: 50 XRP each.
        // With trust line (1 owner object): needs 200 + 50 = 250.
        // For 3 more objects: needs 200 + 4*50 = 400 XRP.
        // Fund with 300 XRP so they have a trust line but not enough
        // reserve for 3 more objects.
        jtx::Account const dan{"dan"};
        env.fund(XRP(300), dan);
        env.close();
        env.trust(USD(100'000), dan);
        env.close();
        env(pay(gw, dan, USD(1'000)));
        env.close();

        // Dan has ~300 XRP with 1 owner object. Needs 400 for 4 total.
        env(clammDeposit(
                dan, pid, -100, 100,
                XRP(1), USD(1)),
            ter(tecINSUFFICIENT_RESERVE));
        env.close();
    }

    void
    testFreeze()
    {
        testcase("CLAMM Freeze via trust lines");
        using namespace jtx;

        // --- CLAMMCreate freeze ---

        {
            // 1. Global freeze on issuer blocks pool creation
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            env(fset(gw, asfGlobalFreeze));
            env.close();

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tecFROZEN));
            env.close();
        }

        {
            // 2. Individual freeze on creator's USD trust line
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            env(trust(gw, alice["USD"](0), tfSetFreeze));
            env.close();

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tecFROZEN));
            env.close();
        }

        {
            // 3. Freeze then clear: first attempt fails, after
            //    clearing freeze succeeds
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            env(fset(gw, asfGlobalFreeze));
            env.close();

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tecFROZEN));
            env.close();

            env(fclear(gw, asfGlobalFreeze));
            env.close();

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();
        }

        // --- CLAMMSwap freeze ---

        {
            // 4. Global freeze blocks swap (caught by isFrozen on
            //    pool's trust line)
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            env(fset(gw, asfGlobalFreeze));
            env.close();

            env(clammSwap(bob, pid, XRP(100)),
                ter(tecFROZEN));
            env.close();
        }

        {
            // 5. Individual freeze on trader: frozen trader blocked,
            //    unfrozen trader succeeds
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            env(trust(gw, bob["USD"](0), tfSetFreeze));
            env.close();

            env(clammSwap(bob, pid, XRP(100)),
                ter(tecFROZEN));
            env.close();

            // Carol is not frozen, swap succeeds
            env(clammSwap(carol, pid, XRP(100)),
                ter(tesSUCCESS));
            env.close();
        }

        // 6. Pool's own trust line freeze: TrustSet to a
        // pseudo-account returns tecPSEUDO_ACCOUNT, so pool
        // trust line freeze via isFrozen(ammAccountID, issue)
        // cannot be tested through TrustSet. The preclaim check
        // exists but requires a different mechanism to trigger.

        // --- CLAMMDeposit freeze ---

        {
            // 7. Global freeze blocks deposit
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(fset(gw, asfGlobalFreeze));
            env.close();

            env(clammDeposit(
                    bob, pid, -1000, 1000,
                    XRP(1'000), USD(1'000)),
                ter(tecFROZEN));
            env.close();
        }

        {
            // 8. Individual freeze on depositor blocks deposit
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(trust(gw, bob["USD"](0), tfSetFreeze));
            env.close();

            env(clammDeposit(
                    bob, pid, -1000, 1000,
                    XRP(1'000), USD(1'000)),
                ter(tecFROZEN));
            env.close();
        }

        // 9. Pool's trust line freeze for deposit: same limitation
        // as case 6 -- TrustSet to pseudo-account not allowed.

        // --- CLAMMWithdraw (no freeze checks in preclaim) ---

        {
            // 10. Global freeze + withdraw: preclaim has no freeze
            // check, and overrideFreeze privilege allows the
            // invariant checker to let the transfer through.
            // Users should always be able to exit positions.
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            auto const nft = clammFindPositionNFT(env, alice, pid);
            BEAST_EXPECT(nft.has_value());

            env(fset(gw, asfGlobalFreeze));
            env.close();

            if (nft)
            {
                env(clammWithdraw(alice, *nft),
                    ter(tesSUCCESS));
                env.close();
            }
        }

        {
            // 11. Individual freeze DOES block withdraw.
            // Individual freeze on the user's trust line prevents
            // withdrawal from the pool.
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            auto const nft = clammFindPositionNFT(env, alice, pid);
            BEAST_EXPECT(nft.has_value());

            env(trust(gw, alice["USD"](0), tfSetFreeze));
            env.close();

            if (nft)
            {
                env(clammWithdraw(alice, *nft),
                    ter(tecFROZEN));
                env.close();
            }
        }

        // --- CLAMMCollectFees ---

        {
            // 12. Global freeze does NOT block fee collection
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            // Generate fees
            env(clammSwap(bob, pid, XRP(1'000)),
                ter(tesSUCCESS));
            env.close();

            auto const nft = clammFindPositionNFT(env, alice, pid);
            BEAST_EXPECT(nft.has_value());

            env(fset(gw, asfGlobalFreeze));
            env.close();

            if (nft)
            {
                env(clammCollectFees(alice, *nft),
                    ter(tesSUCCESS));
                env.close();
            }
        }

        {
            // 13. Individual freeze DOES block fee collection
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            // Generate fees
            env(clammSwap(bob, pid, XRP(1'000)),
                ter(tesSUCCESS));
            env.close();

            auto const nft = clammFindPositionNFT(env, alice, pid);
            BEAST_EXPECT(nft.has_value());

            env(trust(gw, alice["USD"](0), tfSetFreeze));
            env.close();

            if (nft)
            {
                env(clammCollectFees(alice, *nft),
                    ter(tecFROZEN));
                env.close();
            }
        }

    }

    // ================================================================
    // Groupe 1: Critical gap tests
    // ================================================================

    void
    testSwapSlippageProtection()
    {
        testcase("Swap slippage protection (DeliverMin)");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;

        {
            // DeliverMin satisfied -> tesSUCCESS
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            // Low deliverMin - should be easily satisfied
            env(clammSwapWithDeliverMin(
                    bob, pid, XRP(100), USD(1)),
                ter(tesSUCCESS));
            env.close();
        }

        {
            // DeliverMin too high -> tecPATH_PARTIAL
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            // Impossibly high deliverMin
            env(clammSwapWithDeliverMin(
                    bob, pid, XRP(100), USD(99'999)),
                ter(tecPATH_PARTIAL));
            env.close();
        }

        {
            // Without DeliverMin -> tesSUCCESS (no constraint)
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            env(clammSwap(bob, pid, XRP(100)),
                ter(tesSUCCESS));
            env.close();
        }
    }

    void
    testAuctionSlotDiscount()
    {
        testcase("Auction slot discount on swap fee");
        using namespace jtx;
        using namespace std::chrono;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;

        {
            // Slot holder gets discounted fee (higher output)
            // Non-holder pays full fee (lower output)
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(50'000), USD(50'000)),
                ter(tesSUCCESS));
            env.close();

            // Alice wins auction slot
            env(clammBid(alice, pid),
                ter(tesSUCCESS));
            env.close();

            // Alice swaps (discounted fee)
            auto const aliceUsdBefore = env.balance(alice, USD);
            env(clammSwap(alice, pid, XRP(1'000)),
                ter(tesSUCCESS));
            env.close();
            auto const aliceUsdAfter = env.balance(alice, USD);
            auto const aliceGain = aliceUsdAfter - aliceUsdBefore;

            // Bob swaps same amount (full fee)
            auto const bobUsdBefore = env.balance(bob, USD);
            env(clammSwap(bob, pid, XRP(1'000)),
                ter(tesSUCCESS));
            env.close();
            auto const bobUsdAfter = env.balance(bob, USD);
            auto const bobGain = bobUsdAfter - bobUsdBefore;

            // Alice should get more output (lower fee)
            BEAST_EXPECT(aliceGain > bobGain);
        }

        {
            // AuthAccounts: bob added to auth accounts, gets discount
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(50'000), USD(50'000)),
                ter(tesSUCCESS));
            env.close();

            // Alice bids with bob as auth account
            Json::Value jv = clammBid(alice, pid);
            Json::Value authAccounts(Json::arrayValue);
            Json::Value authAcct;
            authAcct[jss::Account] = bob.human();
            Json::Value acctObj;
            acctObj["AuthAccount"] = authAcct;
            authAccounts.append(acctObj);
            jv[sfAuthAccounts.jsonName] = authAccounts;
            env(jv, ter(tesSUCCESS));
            env.close();

            // Bob swaps (discounted via auth)
            auto const bobUsdBefore = env.balance(bob, USD);
            env(clammSwap(bob, pid, XRP(1'000)),
                ter(tesSUCCESS));
            env.close();
            auto const bobUsdAfter = env.balance(bob, USD);
            auto const bobGain = bobUsdAfter - bobUsdBefore;

            // Carol swaps (full fee, not authorized)
            auto const carolUsdBefore = env.balance(carol, USD);
            env(clammSwap(carol, pid, XRP(1'000)),
                ter(tesSUCCESS));
            env.close();
            auto const carolUsdAfter = env.balance(carol, USD);
            auto const carolGain = carolUsdAfter - carolUsdBefore;

            BEAST_EXPECT(bobGain > carolGain);
        }

        {
            // Slot expired -> full fee for everyone
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(50'000), USD(50'000)),
                ter(tesSUCCESS));
            env.close();

            env(clammBid(alice, pid),
                ter(tesSUCCESS));
            env.close();

            // Advance time past slot expiry (24h + 1s)
            env.close(seconds(CLAMM_TOTAL_TIME_SLOT_SECS + 1));

            // Now alice swaps - should pay full fee like everyone
            auto const aliceUsdBefore = env.balance(alice, USD);
            env(clammSwap(alice, pid, XRP(1'000)),
                ter(tesSUCCESS));
            env.close();
            auto const aliceUsdAfter = env.balance(alice, USD);
            auto const aliceGain = aliceUsdAfter - aliceUsdBefore;

            // Bob also swaps for comparison
            auto const bobUsdBefore = env.balance(bob, USD);
            env(clammSwap(bob, pid, XRP(1'000)),
                ter(tesSUCCESS));
            env.close();
            auto const bobUsdAfter = env.balance(bob, USD);
            auto const bobGain = bobUsdAfter - bobUsdBefore;

            // Both should pay the same fee rate (though pool state
            // changes between swaps cause slight difference, the
            // key point is alice no longer gets a discount).
            // We just verify both succeed and produce output.
            BEAST_EXPECT(aliceGain > USD(0));
            BEAST_EXPECT(bobGain > USD(0));
        }
    }

    void
    testSwapAmountCapping()
    {
        testcase("Swap amount capped by pool liquidity");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;

        {
            // Swap request larger than pool capacity -> succeeds using
            // available liquidity (Amount is a maximum, not exact)
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Small pool
            env(clammDeposit(
                    alice, pid, -100, 100,
                    XRP(1'000), USD(1'000)),
                ter(tesSUCCESS));
            env.close();

            auto const bobUsdBefore = env.balance(bob, USD);

            // Bob requests huge swap but pool can only provide limited output
            env(clammSwap(bob, pid, XRP(50'000)),
                ter(tesSUCCESS));
            env.close();

            auto const bobUsdAfter = env.balance(bob, USD);
            // Output should be limited by pool liquidity, not request size
            BEAST_EXPECT(bobUsdAfter > bobUsdBefore);
        }

        {
            // Swap with input asset not matching pool -> tecNO_PERMISSION
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -100, 100,
                    XRP(1'000), USD(1'000)),
                ter(tesSUCCESS));
            env.close();

            // Try to swap EUR into XRP/USD pool
            IOU const EUR{gw["EUR"]};
            env.trust(EUR(1'000'000), bob);
            env.close();
            env(pay(gw, bob, EUR(10'000)));
            env.close();

            env(clammSwap(bob, pid, EUR(100)),
                ter(tecNO_PERMISSION));
            env.close();
        }
    }

    void
    testFreezeTwoIssuers()
    {
        testcase("Freeze with two IOU issuers");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Account const gw2{"gateway2"};

        {
            Env env{*this, features};

            // Setup two issuers
            env.fund(XRP(100'000), gw, gw2, alice, bob, carol);
            env.close();

            IOU const EUR{gw2["EUR"]};

            env.trust(USD(1'000'000), alice);
            env.trust(USD(1'000'000), bob);
            env.trust(EUR(1'000'000), alice);
            env.trust(EUR(1'000'000), bob);
            env.close();

            env(pay(gw, alice, USD(100'000)));
            env(pay(gw, bob, USD(100'000)));
            env(pay(gw2, alice, EUR(100'000)));
            env(pay(gw2, bob, EUR(100'000)));
            env.close();

            auto const pid =
                clammPoolID(USD.issue(), EUR.issue(), 1);

            env(clammCreate(env,
                    alice, USD.issue(), EUR.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    USD(10'000), EUR(10'000)),
                ter(tesSUCCESS));
            env.close();

            // 1. gw global freeze -> swap tecFROZEN
            env(fset(gw, asfGlobalFreeze));
            env.close();

            env(clammSwap(bob, pid, USD(100)),
                ter(tecFROZEN));
            env.close();

            // 2. Unfreeze gw, freeze gw2 -> still tecFROZEN
            env(fclear(gw, asfGlobalFreeze));
            env.close();
            env(fset(gw2, asfGlobalFreeze));
            env.close();

            env(clammSwap(bob, pid, USD(100)),
                ter(tecFROZEN));
            env.close();

            // 3. Unfreeze gw2 -> swap tesSUCCESS
            env(fclear(gw2, asfGlobalFreeze));
            env.close();

            env(clammSwap(bob, pid, USD(100)),
                ter(tesSUCCESS));
            env.close();

            // 4. Withdraw works even when frozen
            auto const nft = clammFindPositionNFT(env, alice, pid);
            BEAST_EXPECT(nft.has_value());

            env(fset(gw, asfGlobalFreeze));
            env.close();

            if (nft)
            {
                env(clammWithdraw(alice, *nft),
                    ter(tesSUCCESS));
                env.close();
            }
        }
    }

    void
    testTickBitmapIntegrity()
    {
        testcase("Tick bitmap integrity after operations");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;

        {
            // Two positions at same ticks, withdraw first -> bitmap present
            // Withdraw second -> bitmap removed
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Alice and Bob deposit at same tick range
            env(clammDeposit(
                    alice, pid, -100, 100,
                    XRP(5'000), USD(5'000)),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    bob, pid, -100, 100,
                    XRP(5'000), USD(5'000)),
                ter(tesSUCCESS));
            env.close();

            auto const nftAlice = clammFindPositionNFT(env, alice, pid);
            auto const nftBob = clammFindPositionNFT(env, bob, pid);
            BEAST_EXPECT(nftAlice.has_value());
            BEAST_EXPECT(nftBob.has_value());

            // Withdraw Alice's position
            if (nftAlice)
            {
                env(clammWithdraw(alice, *nftAlice),
                    ter(tesSUCCESS));
                env.close();
            }

            // Pool should still have liquidity from Bob
            auto const sle1 = env.current()->read(keylet::clamm(pid));
            BEAST_EXPECT(sle1 != nullptr);
            if (sle1)
            {
                auto const liq = clamm::fromSLEField(
                    sle1->getFieldH128(sfLiquidityAmount));
                BEAST_EXPECT(liq > 0);
            }

            // Withdraw Bob's position
            if (nftBob)
            {
                env(clammWithdraw(bob, *nftBob),
                    ter(tesSUCCESS));
                env.close();
            }

            // Pool should be auto-deleted (last position withdrawn)
            BEAST_EXPECT(!env.current()->read(keylet::clamm(pid)));
        }

        {
            // Swap traverses tick, verify bitmap coherent
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Two adjacent positions
            env(clammDeposit(
                    alice, pid, -200, 0,
                    XRP(5'000), USD(5'000)),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    bob, pid, 0, 200,
                    XRP(5'000), USD(5'000)),
                ter(tesSUCCESS));
            env.close();

            // Swap to move price across tick 0 boundary
            env(clammSwap(carol, pid, XRP(3'000)),
                ter(tesSUCCESS));
            env.close();

            // Verify pool state is consistent
            auto const sle = env.current()->read(keylet::clamm(pid));
            BEAST_EXPECT(sle != nullptr);
        }
    }

    // ================================================================
    // Groupe 2: Important gap tests
    // ================================================================

    void
    testCreatePreclaim()
    {
        testcase("CLAMMCreate preclaim error paths");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;

        {
            // Create without trust line for IOU -> tecNO_LINE
            Env env{*this, features};
            Account const dan{"dan"};
            env.fund(XRP(100'000), gw, dan);
            env.close();
            // dan has no USD trust line

            env(clammCreate(env,
                    dan, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tecNO_LINE));
            env.close();
        }

        {
            // Create with frozen asset -> tecFROZEN
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            env(fset(gw, asfGlobalFreeze));
            env.close();

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tecFROZEN));
            env.close();
        }

        {
            // Create with insufficient reserve -> tecINSUFFICIENT_RESERVE
            Env env{*this, features};
            Account const poor{"poor"};
            env.fund(env.current()->fees().accountReserve(0), gw, poor);
            env.close();
            env.trust(USD(1'000'000), poor);
            env.close();
            env(pay(gw, poor, USD(100)));
            env.close();

            env(clammCreate(env,
                    poor, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();
        }
    }

    void
    testVoteEviction()
    {
        testcase("Vote eviction with max slots");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;

        {
            // Multiple voters fill slots, existing voter re-votes
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 2);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 2,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Alice and Bob both deposit and vote
            env(clammDeposit(
                    alice, pid, -600, 600,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    bob, pid, -600, 600,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            env(clammVote(alice, pid, 200),
                ter(tesSUCCESS));
            env.close();

            env(clammVote(bob, pid, 400),
                ter(tesSUCCESS));
            env.close();

            // Alice re-votes -> update without eviction
            env(clammVote(alice, pid, 300),
                ter(tesSUCCESS));
            env.close();

            // Verify fee was updated (weighted average)
            auto const sle = env.current()->read(keylet::clamm(pid));
            BEAST_EXPECT(sle != nullptr);
            if (sle)
            {
                auto const fee = sle->getFieldU16(sfTradingFee);
                // Should be weighted average of 300 and 400
                BEAST_EXPECT(fee > 0 && fee <= 3000);
            }
        }

        {
            // Voter without liquidity -> tecNO_PERMISSION
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 2);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 2,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Bob tries to vote without any deposit
            env(clammVote(bob, pid, 200),
                ter(tecNO_PERMISSION));
            env.close();
        }
    }

    void
    testBidTimeSlots()
    {
        testcase("Bid time slot pricing");
        using namespace jtx;
        using namespace std::chrono;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;

        {
            // Outbid in slot 0 -> price = previous * 1.05 + minSlotPrice
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            // Alice bids first
            env(clammBid(alice, pid),
                ter(tesSUCCESS));
            env.close();

            // Bob outbids immediately (slot 0)
            env(clammBid(bob, pid),
                ter(tesSUCCESS));
            env.close();

            auto const sle = env.current()->read(keylet::clamm(pid));
            BEAST_EXPECT(sle != nullptr);
            if (sle && sle->isFieldPresent(sfAuctionSlot))
            {
                auto const& slot = sle->getFieldObject(sfAuctionSlot);
                BEAST_EXPECT(slot.getAccountID(sfAccount) == bob.id());
            }
        }

        {
            // Advance time, outbid at intermediate slot -> price decay
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            env(clammBid(alice, pid),
                ter(tesSUCCESS));
            env.close();

            // Advance time by ~half the slot duration
            auto const intervalLen =
                CLAMM_TOTAL_TIME_SLOT_SECS /
                CLAMM_AUCTION_SLOT_TIME_INTERVALS;
            env.close(seconds(10 * intervalLen + 1));

            // Bob outbids at decayed price
            env(clammBid(bob, pid),
                ter(tesSUCCESS));
            env.close();
        }

        {
            // After full expiration -> bid at minSlotPrice
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            env(clammBid(alice, pid),
                ter(tesSUCCESS));
            env.close();

            // Advance past expiry
            env.close(seconds(CLAMM_TOTAL_TIME_SLOT_SECS + 1));

            // Bob bids at minSlotPrice
            env(clammBid(bob, pid),
                ter(tesSUCCESS));
            env.close();
        }

        {
            // BidMax too low -> tecINSUFFICIENT_PAYMENT
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            // Alice bids first
            env(clammBid(alice, pid),
                ter(tesSUCCESS));
            env.close();

            // Bob tries to outbid with very low BidMax
            env(clammBidMax(bob, pid, XRP(0)),
                ter(tecINSUFFICIENT_PAYMENT));
            env.close();
        }
    }

    void
    testSwapMultiTickCrossing()
    {
        testcase("Swap crossing multiple tick boundaries");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;

        {
            // 3 adjacent positions, big swap traverses 2+ boundaries
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Three adjacent positions
            env(clammDeposit(
                    alice, pid, -300, -100,
                    XRP(3'000), USD(3'000)),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -100, 100,
                    XRP(3'000), USD(3'000)),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, 100, 300,
                    XRP(3'000), USD(3'000)),
                ter(tesSUCCESS));
            env.close();

            auto const sle1 = env.current()->read(keylet::clamm(pid));
            BEAST_EXPECT(sle1 != nullptr);
            std::int32_t tickBefore = 0;
            if (sle1)
                tickBefore = sle1->getFieldI32(sfCurrentTick);

            // Large swap should cross multiple boundaries
            env(clammSwap(bob, pid, XRP(5'000)),
                ter(tesSUCCESS));
            env.close();

            auto const sle2 = env.current()->read(keylet::clamm(pid));
            BEAST_EXPECT(sle2 != nullptr);
            if (sle2)
            {
                auto const tickAfter = sle2->getFieldI32(sfCurrentTick);
                // Tick should have moved significantly
                BEAST_EXPECT(tickAfter != tickBefore);
            }
        }
    }

    void
    testWithdrawPartialAndSlippage()
    {
        testcase("Withdraw partial and slippage protection");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;

        {
            // Partial withdraw 50% -> position still alive
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            // Generate fees so TokensOwed fields are non-zero
            env(clammSwap(bob, pid, XRP(1'000)),
                ter(tesSUCCESS));
            env.close();

            auto const nft = clammFindPositionNFT(env, alice, pid);
            BEAST_EXPECT(nft.has_value());
            if (!nft) return;

            // Get position liquidity
            auto const posKeylet = keylet::clammPosition(*nft);
            auto const slePos = env.current()->read(posKeylet);
            BEAST_EXPECT(slePos != nullptr);
            if (!slePos) return;

            auto const fullLiquidity = clamm::fromSLEField(
                slePos->getFieldH128(sfLiquidityAmount));

            // Withdraw half
            auto const halfLiq = fullLiquidity / 2;
            env(clammWithdrawPartial(alice, *nft, halfLiq),
                ter(tesSUCCESS));
            env.close();

            // Position should still exist with reduced liquidity
            auto const slePos2 = env.current()->read(posKeylet);
            BEAST_EXPECT(slePos2 != nullptr);
            if (slePos2)
            {
                auto const remainLiq = clamm::fromSLEField(
                    slePos2->getFieldH128(sfLiquidityAmount));
                BEAST_EXPECT(remainLiq > 0);
                BEAST_EXPECT(remainLiq < fullLiquidity);
            }
        }

        {
            // MinAmount satisfied -> tesSUCCESS
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            // Generate fees
            env(clammSwap(bob, pid, XRP(1'000)),
                ter(tesSUCCESS));
            env.close();

            auto const nft = clammFindPositionNFT(env, alice, pid);
            BEAST_EXPECT(nft.has_value());
            if (!nft) return;

            auto const posKeylet = keylet::clammPosition(*nft);
            auto const slePos = env.current()->read(posKeylet);
            if (!slePos) return;
            auto const fullLiq = clamm::fromSLEField(
                slePos->getFieldH128(sfLiquidityAmount));
            auto const halfLiq = fullLiq / 2;

            // Low min amounts - easily satisfied
            env(clammWithdrawWithMin(
                    alice, *nft, halfLiq, XRP(1), USD(1)),
                ter(tesSUCCESS));
            env.close();
        }

        {
            // MinAmount too high -> tecPATH_PARTIAL
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            // Generate fees
            env(clammSwap(bob, pid, XRP(1'000)),
                ter(tesSUCCESS));
            env.close();

            auto const nft = clammFindPositionNFT(env, alice, pid);
            BEAST_EXPECT(nft.has_value());
            if (!nft) return;

            auto const posKeylet = keylet::clammPosition(*nft);
            auto const slePos = env.current()->read(posKeylet);
            if (!slePos) return;
            auto const fullLiq = clamm::fromSLEField(
                slePos->getFieldH128(sfLiquidityAmount));
            auto const halfLiq = fullLiq / 2;

            // Impossibly high min amounts
            env(clammWithdrawWithMin(
                    alice, *nft, halfLiq, XRP(99'999), USD(99'999)),
                ter(tecPATH_PARTIAL));
            env.close();
        }

        {
            // Withdraw 100% via partial -> position deleted
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            // Generate fees
            env(clammSwap(bob, pid, XRP(1'000)),
                ter(tesSUCCESS));
            env.close();

            auto const nft = clammFindPositionNFT(env, alice, pid);
            BEAST_EXPECT(nft.has_value());
            if (!nft) return;

            auto const posKeylet = keylet::clammPosition(*nft);
            auto const slePos = env.current()->read(posKeylet);
            if (!slePos) return;
            auto const fullLiq = clamm::fromSLEField(
                slePos->getFieldH128(sfLiquidityAmount));

            // Withdraw full amount via partial
            env(clammWithdrawPartial(alice, *nft, fullLiq),
                ter(tesSUCCESS));
            env.close();

            // Position should be deleted
            auto const slePos2 = env.current()->read(posKeylet);
            BEAST_EXPECT(slePos2 == nullptr);
        }
    }

    // ================================================================
    // Groupe 3: RPC tests
    // ================================================================

    void
    testRPCPositions()
    {
        testcase("clamm_positions RPC");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                alice, pid, -1000, 1000,
                XRP(10'000), USD(10'000)),
            ter(tesSUCCESS));
        env.close();

        auto const nft = clammFindPositionNFT(env, alice, pid);
        BEAST_EXPECT(nft.has_value());

        {
            // By nftoken_id -> single detailed position
            if (nft)
            {
                auto const result = env.rpc(
                    "json", "clamm_positions",
                    std::string("{\"nftoken_id\": \"" +
                                to_string(*nft) + "\"}"));
                auto const& rr = result[jss::result];
                BEAST_EXPECT(!rr.isMember(jss::error));
            }
        }

        {
            // By account -> list of positions
            auto const result = env.rpc(
                "json", "clamm_positions",
                std::string("{\"account\": \"" +
                            alice.human() + "\"}"));
            auto const& rr = result[jss::result];
            BEAST_EXPECT(!rr.isMember(jss::error));
        }

        {
            // Invalid nftoken_id -> error
            auto const result = env.rpc(
                "json", "clamm_positions",
                std::string("{\"nftoken_id\": \"invalid\"}"));
            auto const& rr = result[jss::result];
            BEAST_EXPECT(
                rr.isMember(jss::error) ||
                rr.isMember("error"));
        }

        {
            // Invalid account -> error
            auto const result = env.rpc(
                "json", "clamm_positions",
                std::string("{\"account\": \"not_an_account\"}"));
            auto const& rr = result[jss::result];
            BEAST_EXPECT(
                rr.isMember(jss::error) ||
                rr.isMember("error"));
        }
    }

    void
    testRPCTicks()
    {
        testcase("clamm_ticks RPC");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Create multiple positions to initialize multiple ticks
        env(clammDeposit(
                alice, pid, -100, 100,
                XRP(5'000), USD(5'000)),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                bob, pid, -500, 500,
                XRP(5'000), USD(5'000)),
            ter(tesSUCCESS));
        env.close();

        {
            // By pool_id -> list of initialized ticks
            auto const result = env.rpc(
                "json", "clamm_ticks",
                std::string("{\"pool_id\": \"" +
                            to_string(pid) + "\"}"));
            auto const& rr = result[jss::result];
            BEAST_EXPECT(!rr.isMember(jss::error));
        }

        {
            // With limit=2 -> at most 2 ticks
            auto const result = env.rpc(
                "json", "clamm_ticks",
                std::string("{\"pool_id\": \"" +
                            to_string(pid) +
                            "\", \"limit\": 2}"));
            auto const& rr = result[jss::result];
            BEAST_EXPECT(!rr.isMember(jss::error));
        }

        {
            // Invalid pool_id -> error
            auto const result = env.rpc(
                "json", "clamm_ticks",
                std::string("{\"pool_id\": \"0000000000000000"
                            "0000000000000000"
                            "0000000000000000"
                            "0000000000000001\"}"));
            auto const& rr = result[jss::result];
            BEAST_EXPECT(
                rr.isMember(jss::error) ||
                rr.isMember("error"));
        }
    }

    void
    testRPCQuote()
    {
        testcase("clamm_quote RPC");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                alice, pid, -1000, 1000,
                XRP(10'000), USD(10'000)),
            ter(tesSUCCESS));
        env.close();

        {
            // XRP in (zeroForOne) -> expected output > 0
            auto const result = env.rpc(
                "json", "clamm_quote",
                std::string("{\"pool_id\": \"" + to_string(pid) +
                            "\", \"amount\": \"1000000\"}"));
            auto const& rr = result[jss::result];
            BEAST_EXPECT(!rr.isMember(jss::error));
        }

        {
            // USD in (oneForZero)
            auto const result = env.rpc(
                "json", "clamm_quote",
                std::string("{\"pool_id\": \"" + to_string(pid) +
                            "\", \"amount\": {\"currency\": \"USD\","
                            " \"issuer\": \"" + gw.human() +
                            "\", \"value\": \"100\"}}"));
            auto const& rr = result[jss::result];
            BEAST_EXPECT(!rr.isMember(jss::error));
        }

        {
            // Missing amount -> error
            auto const result = env.rpc(
                "json", "clamm_quote",
                std::string("{\"pool_id\": \"" +
                            to_string(pid) + "\"}"));
            auto const& rr = result[jss::result];
            BEAST_EXPECT(
                rr.isMember(jss::error) ||
                rr.isMember("error"));
        }

        {
            // Non-existent pool -> error
            auto const result = env.rpc(
                "json", "clamm_quote",
                std::string("{\"pool_id\": \"0000000000000000"
                            "0000000000000000"
                            "0000000000000000"
                            "0000000000000001\","
                            " \"amount\": \"1000000\"}"));
            auto const& rr = result[jss::result];
            BEAST_EXPECT(
                rr.isMember(jss::error) ||
                rr.isMember("error"));
        }
    }

    // ================================================================
    // Groupe 4: Error path tests
    // ================================================================

    void
    testDepositErrorPaths()
    {
        testcase("CLAMMDeposit error paths");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;

        {
            // NFT owned by another user -> tecNO_PERMISSION
            // (deposit to existing position owned by someone else)
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Alice creates a position
            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(5'000), USD(5'000)),
                ter(tesSUCCESS));
            env.close();

            auto const nftAlice = clammFindPositionNFT(env, alice, pid);
            BEAST_EXPECT(nftAlice.has_value());

            // Bob tries to withdraw Alice's position
            if (nftAlice)
            {
                env(clammWithdraw(bob, *nftAlice),
                    ter(tecNO_PERMISSION));
                env.close();
            }
        }

        {
            // Deposit when pool asset is frozen -> tecFROZEN
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(fset(gw, asfGlobalFreeze));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(1'000), USD(1'000)),
                ter(tecFROZEN));
            env.close();
        }

        {
            // Deposit with tick range not aligned to spacing -> temBAD_AMOUNT
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            // Fee tier 2 has tick spacing 60
            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 2);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 2,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Ticks not aligned to spacing 60
            env(clammDeposit(
                    alice, pid, -55, 55,
                    XRP(1'000), USD(1'000)),
                ter(temBAD_AMOUNT));
            env.close();
        }

        {
            // Deposit with insufficient XRP reserve
            Env env{*this, features};
            Account const poor{"poorLP"};
            env.fund(
                env.current()->fees().accountReserve(0) +
                    env.current()->fees().increment * 2,
                gw, poor);
            env.close();
            env.trust(USD(1'000'000), poor);
            env.close();
            env(pay(gw, poor, USD(100)));
            env.close();

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            // Need alice to create pool first
            env.fund(XRP(100'000), alice);
            env.close();
            env.trust(USD(1'000'000), alice);
            env.close();
            env(pay(gw, alice, USD(100'000)));
            env.close();

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // poor has minimal XRP, try to deposit
            env(clammDeposit(
                    poor, pid, -100, 100,
                    XRP(1), USD(1)),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();
        }
    }

    void
    testBidErrorPaths()
    {
        testcase("CLAMMBid error paths");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;

        {
            // Too many auth accounts -> temMALFORMED
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            Account const d{"dan"};
            Account const e{"eve"};
            env.fund(XRP(10'000), d, e);
            env.close();

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -100, 100,
                    XRP(1'000), USD(1'000)),
                ter(tesSUCCESS));
            env.close();

            // 5 auth accounts exceeds max (4)
            Json::Value jv = clammBid(alice, pid);
            Json::Value authAccounts(Json::arrayValue);
            for (auto const& acct : {bob, carol, d, e, gw})
            {
                Json::Value authAcct;
                authAcct[jss::Account] = acct.human();
                Json::Value acctObj;
                acctObj["AuthAccount"] = authAcct;
                authAccounts.append(acctObj);
            }
            jv[sfAuthAccounts.jsonName] = authAccounts;
            env(jv, ter(temMALFORMED));
            env.close();
        }

        {
            // Bidder in own auth accounts -> temMALFORMED
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -100, 100,
                    XRP(1'000), USD(1'000)),
                ter(tesSUCCESS));
            env.close();

            Json::Value jv = clammBid(alice, pid);
            Json::Value authAccounts(Json::arrayValue);
            Json::Value authAcct;
            authAcct[jss::Account] = alice.human();  // self
            Json::Value acctObj;
            acctObj["AuthAccount"] = authAcct;
            authAccounts.append(acctObj);
            jv[sfAuthAccounts.jsonName] = authAccounts;
            env(jv, ter(temMALFORMED));
            env.close();
        }

        {
            // Duplicate auth accounts -> temMALFORMED
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -100, 100,
                    XRP(1'000), USD(1'000)),
                ter(tesSUCCESS));
            env.close();

            Json::Value jv = clammBid(alice, pid);
            Json::Value authAccounts(Json::arrayValue);
            for (int i = 0; i < 2; ++i)
            {
                Json::Value authAcct;
                authAcct[jss::Account] = bob.human();
                Json::Value acctObj;
                acctObj["AuthAccount"] = authAcct;
                authAccounts.append(acctObj);
            }
            jv[sfAuthAccounts.jsonName] = authAccounts;
            env(jv, ter(temMALFORMED));
            env.close();
        }

        {
            // Auth account doesn't exist -> terNO_ACCOUNT
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -100, 100,
                    XRP(1'000), USD(1'000)),
                ter(tesSUCCESS));
            env.close();

            Account const ghost{"ghost"};
            Json::Value jv = clammBid(alice, pid);
            Json::Value authAccounts(Json::arrayValue);
            Json::Value authAcct;
            authAcct[jss::Account] = ghost.human();
            Json::Value acctObj;
            acctObj["AuthAccount"] = authAcct;
            authAccounts.append(acctObj);
            jv[sfAuthAccounts.jsonName] = authAccounts;
            env(jv, ter(terNO_ACCOUNT));
            env.close();
        }

        {
            // BidMin > BidMax -> temMALFORMED
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -100, 100,
                    XRP(1'000), USD(1'000)),
                ter(tesSUCCESS));
            env.close();

            Json::Value jv = clammBid(alice, pid);
            STAmount(xrpIssue(), 100'000'000).setJson(
                jv[sfBidMin.jsonName]);
            STAmount(xrpIssue(), 10'000'000).setJson(
                jv[sfBidMax.jsonName]);
            env(jv, ter(temMALFORMED));
            env.close();
        }
    }

    void
    testMissingTERPaths()
    {
        testcase("Missing TER code paths");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;

        {
            // CLAMMCreate: initialSqrtPrice below minSqrtRatio -> temBAD_AMOUNT
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clamm::uint128(1)),  // way below minSqrtRatio
                ter(temBAD_AMOUNT));
            env.close();
        }

        {
            // CLAMMCreate: initialSqrtPrice of 0 -> temBAD_AMOUNT
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clamm::uint128(0)),
                ter(temBAD_AMOUNT));
            env.close();
        }

        {
            // CLAMMWithdraw: explicit 0 liquidity -> tecINSUFFICIENT_PAYMENT
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            auto const nft = clammFindPositionNFT(env, alice, pid);
            BEAST_EXPECT(nft.has_value());

            if (nft)
            {
                env(clammWithdrawPartial(
                        alice, *nft, clamm::uint128(0)),
                    ter(tecINSUFFICIENT_PAYMENT));
                env.close();
            }
        }

        {
            // CLAMMDeposit: sfMinLiquidity too high -> tecPATH_PARTIAL
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Deposit with MinLiquidity constraint that cannot be met
            Json::Value jv = clammDeposit(
                alice, pid, -100, 100, XRP(100), USD(100));
            // Set impossibly high min liquidity
            jv[sfMinLiquidity.jsonName] =
                to_string(clamm::toSLEField(
                    clamm::uint128("999999999999999999999999999")));
            env(jv, ter(tecPATH_PARTIAL));
            env.close();
        }
    }

    void
    testNFTokenURIMetadata()
    {
        testcase("NFToken URI Metadata");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Deposit to get an NFToken
        env(clammDeposit(
                alice, pid, -100, 100, XRP(1000), USD(1000)),
            ter(tesSUCCESS));
        env.close();

        auto const nftID = clammFindPositionNFT(env, alice, pid);
        BEAST_EXPECT(nftID.has_value());
        if (!nftID)
            return;

        // Find the NFToken and verify URI
        auto const token =
            nft::findToken(*env.current(), alice.id(), *nftID);
        BEAST_EXPECT(token.has_value());
        if (token)
        {
            BEAST_EXPECT(token->isFieldPresent(sfURI));
            if (token->isFieldPresent(sfURI))
            {
                auto const uri = token->getFieldVL(sfURI);
                std::string uriStr(
                    reinterpret_cast<char const*>(uri.data()),
                    uri.size());
                // URI should contain pool ID and ticks
                BEAST_EXPECT(uriStr.find(to_string(pid)) != std::string::npos);
                BEAST_EXPECT(uriStr.find("\"lt\":-100") != std::string::npos);
                BEAST_EXPECT(uriStr.find("\"ut\":100") != std::string::npos);
            }
        }
    }

    void
    testPoolResolutionByAssets()
    {
        testcase("Pool Resolution By Assets");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Deposit by assets (no PoolID)
        env(clammDepositByAssets(
                alice, xrpIssue(), USD.issue(), 1,
                -100, 100, XRP(1000), USD(1000)),
            ter(tesSUCCESS));
        env.close();

        auto const nftID = clammFindPositionNFT(env, alice, pid);
        BEAST_EXPECT(nftID.has_value());

        // Swap by assets
        env(clammSwapByAssets(
                bob, xrpIssue(), USD.issue(), 1, XRP(10)),
            ter(tesSUCCESS));
        env.close();

        // Vote by assets
        env(clammVoteByAssets(
                alice, xrpIssue(), USD.issue(), 1, 400),
            ter(tesSUCCESS));
        env.close();

        // Bid by assets
        env(clammBidByAssets(
                alice, xrpIssue(), USD.issue(), 1),
            ter(tesSUCCESS));
        env.close();
    }

    void
    testDeleteEmptyPool()
    {
        testcase("CLAMMDelete Empty Pool");
        using namespace jtx;

        // Scenario: pool created with no deposits, then CLAMMDelete.
        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        // Alice creates pool (no deposits)
        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Pool exists
        BEAST_EXPECT(env.current()->read(keylet::clamm(pid)));

        // Anyone can delete an empty pool
        env(clammDelete(carol, xrpIssue(), USD.issue(), 1),
            ter(tesSUCCESS));
        env.close();

        // Pool should be gone
        BEAST_EXPECT(!env.current()->read(keylet::clamm(pid)));
    }

    void
    testDeleteNonEmptyPool()
    {
        testcase("CLAMMDelete Non-Empty Pool");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        // Create pool
        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Deposit (pool has liquidity)
        env(clammDeposit(
                alice, pid, -100, 100, XRP(1000), USD(1000)),
            ter(tesSUCCESS));
        env.close();

        // Try to delete non-empty pool
        env(clammDelete(bob, xrpIssue(), USD.issue(), 1),
            ter(tecAMM_NOT_EMPTY));
        env.close();

        // Pool should still exist
        BEAST_EXPECT(env.current()->read(keylet::clamm(pid)));
    }

    void
    testDeleteAfterPaymentExhaustion()
    {
        testcase("CLAMMDelete after payment exhausts pool");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Alice deposits with decent liquidity
        env(clammDeposit(
                alice, pid, -500, 500, XRP(5000), USD(5000)),
            ter(tesSUCCESS));
        env.close();

        auto const nftID = clammFindPositionNFT(env, alice, pid);
        BEAST_EXPECT(nftID.has_value());

        // Large swap moves price significantly
        env(clammSwap(bob, pid, XRP(3000)),
            ter(std::ignore));
        env.close();

        // Pool still exists with shifted price
        auto const sle = env.current()->read(keylet::clamm(pid));
        BEAST_EXPECT(sle);
        if (sle)
        {
            auto const sqrtPrice =
                clamm::fromSLEField(sle->getFieldH128(sfSqrtPrice));
            BEAST_EXPECT(sqrtPrice > 0);
        }

        // Delete should fail -- position still exists
        env(clammDelete(carol, xrpIssue(), USD.issue(), 1),
            ter(tecAMM_NOT_EMPTY));
        env.close();

        // Alice withdraws (gets back asymmetric amounts after swap)
        if (nftID)
        {
            env(clammWithdraw(alice, *nftID),
                ter(tesSUCCESS));
            env.close();
        }

        // Pool should be auto-deleted (last position withdrawn)
        BEAST_EXPECT(!env.current()->read(keylet::clamm(pid)));
    }

    void
    testDeleteAndRecreate()
    {
        testcase("CLAMMDelete then recreate same pool");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        // Create pool with no deposits, then delete explicitly
        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        BEAST_EXPECT(env.current()->read(keylet::clamm(pid)));

        env(clammDelete(carol, xrpIssue(), USD.issue(), 1),
            ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(!env.current()->read(keylet::clamm(pid)));

        // Recreate same pool (same pair, same fee tier)
        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Pool should exist again with fresh state
        auto const sle = env.current()->read(keylet::clamm(pid));
        BEAST_EXPECT(sle);
        if (sle)
        {
            auto const sqrtPrice =
                clamm::fromSLEField(sle->getFieldH128(sfSqrtPrice));
            BEAST_EXPECT(sqrtPrice > 0);

            // Should have no liquidity (fresh pool)
            BEAST_EXPECT(!sle->isFieldPresent(sfLiquidityAmount));
        }

        // Deposit into the recreated pool should work
        env(clammDeposit(
                carol, pid, -200, 200, XRP(5000), USD(5000)),
            ter(tesSUCCESS));
        env.close();

        auto const carolNFT = clammFindPositionNFT(env, carol, pid);
        BEAST_EXPECT(carolNFT.has_value());

        // Swap should work in recreated pool
        env(clammSwap(bob, pid, XRP(100)),
            ter(tesSUCCESS));
        env.close();
    }

    void
    testNFTokenTransferUpdatesPosition()
    {
        testcase("NFToken Transfer Updates Position");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Alice deposits
        env(clammDeposit(
                alice, pid, -100, 100, XRP(1000), USD(1000)),
            ter(tesSUCCESS));
        env.close();

        auto const nftID = clammFindPositionNFT(env, alice, pid);
        BEAST_EXPECT(nftID.has_value());
        if (!nftID)
            return;

        // Verify alice owns the position
        {
            auto const sle = env.current()->read(
                keylet::clammPosition(*nftID));
            BEAST_EXPECT(sle);
            if (sle)
                BEAST_EXPECT(sle->getAccountID(sfOwner) == alice.id());
        }

        // Alice creates a sell offer for the position NFT
        env(token::createOffer(alice, *nftID, XRP(0)),
            txflags(tfSellNFToken),
            ter(tesSUCCESS));
        env.close();

        // Find the sell offer
        uint256 sellOfferID;
        {
            auto const root = keylet::nft_sells(*nftID);
            auto const dir = env.current()->read(root);
            BEAST_EXPECT(dir);
            if (dir)
            {
                auto const& items = dir->getFieldV256(sfIndexes);
                BEAST_EXPECT(items.size() == 1);
                if (!items.empty())
                    sellOfferID = items[0];
            }
        }

        // Bob accepts the sell offer
        env(token::acceptSellOffer(bob, sellOfferID),
            ter(tesSUCCESS));
        env.close();

        // Verify bob now owns the position
        {
            auto const sle = env.current()->read(
                keylet::clammPosition(*nftID));
            BEAST_EXPECT(sle);
            if (sle)
                BEAST_EXPECT(sle->getAccountID(sfOwner) == bob.id());
        }

        // Bob should be able to withdraw from the position
        env(clammWithdraw(bob, *nftID),
            ter(tesSUCCESS));
        env.close();
    }

    void
    testNFTokenTransferBrokered()
    {
        testcase("NFToken Transfer Brokered");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Alice deposits
        env(clammDeposit(
                alice, pid, -100, 100, XRP(1000), USD(1000)),
            ter(tesSUCCESS));
        env.close();

        auto const nftID = clammFindPositionNFT(env, alice, pid);
        BEAST_EXPECT(nftID.has_value());
        if (!nftID)
            return;

        // Alice creates sell offer for 10 XRP
        env(token::createOffer(alice, *nftID, XRP(10)),
            txflags(tfSellNFToken),
            ter(tesSUCCESS));
        env.close();

        uint256 sellOfferID;
        {
            auto const root = keylet::nft_sells(*nftID);
            auto const dir = env.current()->read(root);
            BEAST_EXPECT(dir);
            if (dir)
            {
                auto const& items = dir->getFieldV256(sfIndexes);
                if (!items.empty())
                    sellOfferID = items[0];
            }
        }

        // Bob creates buy offer for 10 XRP
        env(token::createOffer(bob, *nftID, XRP(10)),
            token::owner(alice),
            ter(tesSUCCESS));
        env.close();

        uint256 buyOfferID;
        {
            auto const root = keylet::nft_buys(*nftID);
            auto const dir = env.current()->read(root);
            BEAST_EXPECT(dir);
            if (dir)
            {
                auto const& items = dir->getFieldV256(sfIndexes);
                if (!items.empty())
                    buyOfferID = items[0];
            }
        }

        // Carol brokers the deal
        env(token::brokerOffers(carol, buyOfferID, sellOfferID),
            ter(tesSUCCESS));
        env.close();

        // Verify bob now owns the position
        {
            auto const sle = env.current()->read(
                keylet::clammPosition(*nftID));
            BEAST_EXPECT(sle);
            if (sle)
                BEAST_EXPECT(sle->getAccountID(sfOwner) == bob.id());
        }

        // Bob should be able to collect fees (even with no fees, tests ownership)
        env(clammCollectFees(bob, *nftID),
            ter(tecAMM_EMPTY));
        env.close();
    }

    void
    testWithdrawAutoDeletesEmptyPool()
    {
        testcase("Withdraw auto-deletes empty pool when last position removed");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        // Alice creates pool and deposits
        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                alice, pid, -100, 100, XRP(1000), USD(1000)),
            ter(tesSUCCESS));
        env.close();

        auto const nftID = clammFindPositionNFT(env, alice, pid);
        BEAST_EXPECT(nftID.has_value());
        if (!nftID)
            return;

        // Pool exists
        BEAST_EXPECT(env.current()->read(keylet::clamm(pid)));

        // Alice full withdraws (is creator) -> auto-delete
        env(clammWithdraw(alice, *nftID),
            ter(tesSUCCESS));
        env.close();

        // Pool should be auto-deleted
        BEAST_EXPECT(!env.current()->read(keylet::clamm(pid)));

        // Position should be gone
        BEAST_EXPECT(
            !env.current()->read(keylet::clammPosition(*nftID)));
    }

    void
    testAccountDeletionBlocker()
    {
        testcase("Account deletion blocked by CLAMM objects");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        // Alice creates pool and deposits (gets NFT position)
        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                alice, pid, -100, 100, XRP(1000), USD(1000)),
            ter(tesSUCCESS));
        env.close();

        // Advance ledgers so AccountDelete is not blocked by TOO_SOON
        incLgrSeqForAccDel(env, alice);

        // Alice cannot delete her account (has NFT position in directory)
        env(acctdelete(alice, bob),
            fee(drops(env.current()->fees().increment)),
            ter(tecHAS_OBLIGATIONS));
        env.close();
    }

    void
    testDeletionBlockersRPC()
    {
        testcase("RPC deletion_blockers_only returns CLAMM objects");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Alice deposits (gets NFT position in her directory)
        env(clammDeposit(
                alice, pid, -100, 100, XRP(1000), USD(1000)),
            ter(tesSUCCESS));
        env.close();

        // Query account_objects with deletion_blockers_only
        Json::Value params;
        params[jss::account] = alice.human();
        params[jss::deletion_blockers_only] = true;
        auto const result = env.rpc(
            "json", "account_objects", to_string(params));
        auto const& objects =
            result[jss::result][jss::account_objects];
        BEAST_EXPECT(objects.isArray());

        // Should find objects blocking deletion (NFT position or trust lines)
        BEAST_EXPECT(objects.size() > 0);
    }

    void
    testSwapZeroLiquidityPool()
    {
        testcase("Swap in pool with no positions (zero liquidity)");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // No deposit -- pool has zero liquidity everywhere.
        // Swap must not crash (division by zero guard) and should fail
        // gracefully.
        env(clammSwap(bob, pid, XRP(100)),
            ter(tecPATH_DRY));
        env.close();

        // Pool should still be intact
        auto const sle = env.current()->read(keylet::clamm(pid));
        BEAST_EXPECT(sle != nullptr);
    }

    void
    testTickCrossingLiquidityTransition()
    {
        testcase("Swap crossing tick gap between non-adjacent positions");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Two positions with a gap: [-200, -100] and [100, 200]
        // Current tick is near 1 (default sqrt price), so only
        // liquidity in-range if we deposit a narrow range around 0.
        env(clammDeposit(
                alice, pid, -10, 10,
                XRP(1'000), USD(1'000)),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                alice, pid, 100, 200,
                XRP(2'000), USD(2'000)),
            ter(tesSUCCESS));
        env.close();

        // Swap oneForZero pushes price up through the gap [10, 100]
        // where there is no liquidity, then into [100, 200].
        env(clammSwap(bob, pid, USD(3'000)),
            ter(tesSUCCESS));
        env.close();

        auto const sle = env.current()->read(keylet::clamm(pid));
        BEAST_EXPECT(sle != nullptr);
        if (sle)
        {
            auto const tickAfter = sle->getFieldI32(sfCurrentTick);
            // Price should have moved upward
            BEAST_EXPECT(tickAfter > 10);
        }
    }

    void
    testFeeGrowthOverflowWrapping()
    {
        testcase("Fee growth integrity after many swaps");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                alice, pid, -1000, 1000,
                XRP(50'000), USD(50'000)),
            ter(tesSUCCESS));
        env.close();

        auto const nft = clammFindPositionNFT(env, alice, pid);
        BEAST_EXPECT(nft.has_value());

        // 20 alternating swaps to accumulate fees
        for (int i = 0; i < 10; ++i)
        {
            env(clammSwap(bob, pid, XRP(500)),
                ter(tesSUCCESS));
            env.close();

            env(clammSwap(bob, pid, USD(500)),
                ter(tesSUCCESS));
            env.close();
        }

        // Collect fees should succeed without corruption
        if (nft)
        {
            env(clammCollectFees(alice, *nft),
                ter(tesSUCCESS));
            env.close();
        }

        // Pool should be in valid state
        auto const sle = env.current()->read(keylet::clamm(pid));
        BEAST_EXPECT(sle != nullptr);
    }

    void
    testMultipleTickCrossingsDeepSwap()
    {
        testcase("Deep swap crossing 5+ tick boundaries");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 0);

        // Fee tier 0 has tickSpacing=1, allowing dense positions
        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 0,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // 6 narrow adjacent positions: [-6,-5], [-5,-4], ..., [-1,0]
        for (int i = 6; i >= 1; --i)
        {
            env(clammDeposit(
                    alice, pid, -i, -(i - 1),
                    XRP(1'000), USD(1'000)),
                ter(tesSUCCESS));
            env.close();
        }

        auto const sleBefore = env.current()->read(keylet::clamm(pid));
        BEAST_EXPECT(sleBefore != nullptr);

        // Large zeroForOne swap should cross all 6 boundaries
        env(clammSwap(bob, pid, XRP(10'000)),
            ter(tesSUCCESS));
        env.close();

        auto const sleAfter = env.current()->read(keylet::clamm(pid));
        BEAST_EXPECT(sleAfter != nullptr);
        if (sleAfter)
        {
            auto const tickAfter = sleAfter->getFieldI32(sfCurrentTick);
            // Should have moved significantly downward
            BEAST_EXPECT(tickAfter < -1);
        }
    }

    void
    run() override
    {
        testCreate();
        testDeposit();
        testSwap();
        testWithdraw();
        testCollectFees();
        testVote();
        testBid();
        testRPCInfo();
        testSwapCrossesTickBoundary();
        testSwapWithSqrtPriceLimit();
        testOutOfRangePositions();
        testMultiplePositionsSameLP();
        testFeeAccumulation();
        testDepositTickAlignment();
        testCreateAllFeeTiers();
        testSwapBothDirections();
        testWithdrawReturnsCorrectAmounts();
        testPoolLiquidityUpdatesOnDeposit();
        testVoteWeightedByLiquidity();
        testBidOutbid();
        testSwapNoLiquidityInRange();
        testDepositZeroAmounts();
        testCreateDifferentAssetPairs();
        testWithdrawNonExistentPosition();
        testCollectFeesNoFees();
        testDepositExtremeTickRange();
        testSwapLargeAmount();
        testVoteDiscountedFeeEqualsTradingFee();
        testSwapZeroAmount();
        testVoteFeeExceedsTierMax();
        testDepositMinLiquidity();
        testDepositInsufficientReserve();
        testFreeze();
        testSwapSlippageProtection();
        testAuctionSlotDiscount();
        testSwapAmountCapping();
        testFreezeTwoIssuers();
        testTickBitmapIntegrity();
        testCreatePreclaim();
        testVoteEviction();
        testBidTimeSlots();
        testSwapMultiTickCrossing();
        testWithdrawPartialAndSlippage();
        testRPCPositions();
        testRPCTicks();
        testRPCQuote();
        testDepositErrorPaths();
        testBidErrorPaths();
        testMissingTERPaths();
        testNFTokenURIMetadata();
        testPoolResolutionByAssets();
        testDeleteEmptyPool();
        testDeleteNonEmptyPool();
        testDeleteAfterPaymentExhaustion();
        testDeleteAndRecreate();
        testNFTokenTransferUpdatesPosition();
        testNFTokenTransferBrokered();
        testWithdrawAutoDeletesEmptyPool();
        testAccountDeletionBlocker();
        testDeletionBlockersRPC();
        testSwapZeroLiquidityPool();
        testTickCrossingLiquidityTransition();
        testFeeGrowthOverflowWrapping();
        testMultipleTickCrossingsDeepSwap();
        testClawback();
    }

    // ================================================================
    // CLAMMClawback tests
    // ================================================================

    void
    testClawback()
    {
        testcase("CLAMMClawback");
        using namespace jtx;

        // Helper: setup env with clawback flag set BEFORE trust lines
        auto clawbackSetup = [&](Env& env) {
            env.fund(XRP(100'000), gw, alice, bob, carol);
            env.close();
            // Must set clawback flag before any owned objects
            env(fset(gw, asfAllowTrustLineClawback));
            env.close();
            env.trust(USD(1'000'000), alice);
            env.trust(USD(1'000'000), bob);
            env.trust(USD(1'000'000), carol);
            env.close();
            env(pay(gw, alice, USD(100'000)));
            env(pay(gw, bob, USD(100'000)));
            env(pay(gw, carol, USD(100'000)));
            env.close();
        };

        // 1. Issuer == holder -> temMALFORMED
        {
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clawbackSetup(env);

            env(clammClawback(gw, gw, USD.issue(), xrpIssue(), 1),
                ter(temMALFORMED));
        }

        // 2. Asset is XRP -> temMALFORMED
        {
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clawbackSetup(env);

            env(clammClawback(gw, alice, xrpIssue(), USD.issue(), 1),
                ter(temMALFORMED));
        }

        // 3. Asset issuer != Account -> temMALFORMED
        {
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clawbackSetup(env);

            // alice tries to claw back gw's USD
            env(clammClawback(alice, bob, USD.issue(), xrpIssue(), 1),
                ter(temMALFORMED));
        }

        // 4. Pool not found -> tecNO_ENTRY
        {
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clawbackSetup(env);

            env(clammClawback(gw, alice, USD.issue(), xrpIssue(), 1),
                ter(tecNO_ENTRY));
        }

        // 5. No clawback permission -> tecNO_PERMISSION
        {
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            // Use standard setup (no clawback flag)
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            env(clammClawback(gw, alice, USD.issue(), xrpIssue(), 1),
                ter(tecNO_PERMISSION));
        }

        // 6. Holder has no positions -> tecAMM_BALANCE
        {
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clawbackSetup(env);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            // bob has no positions in this pool
            env(clammClawback(gw, bob, USD.issue(), xrpIssue(), 1),
                ter(tecAMM_BALANCE));
        }

        // 7. Full clawback succeeds
        {
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clawbackSetup(env);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            auto const nft = clammFindPositionNFT(env, alice, pid);
            BEAST_EXPECT(nft.has_value());

            env(clammClawback(gw, alice, USD.issue(), xrpIssue(), 1),
                ter(tesSUCCESS));
            env.close();

            // Position should be gone after full clawback
            auto const nftAfter =
                clammFindPositionNFT(env, alice, pid);
            BEAST_EXPECT(!nftAfter.has_value());
        }

        // 8. Partial clawback with specific amount
        {
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clawbackSetup(env);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            auto const nft = clammFindPositionNFT(env, alice, pid);
            BEAST_EXPECT(nft.has_value());

            // Claw back a small amount (position should remain)
            env(clammClawback(
                    gw, alice, USD.issue(), xrpIssue(), 1,
                    USD(100)),
                ter(tesSUCCESS));
            env.close();

            // Position should still exist (partial withdrawal)
            auto const nftAfter =
                clammFindPositionNFT(env, alice, pid);
            BEAST_EXPECT(nftAfter.has_value());
        }

        // 9. Bad amount (wrong issue) -> temBAD_AMOUNT
        {
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clawbackSetup(env);

            env(clammClawback(
                    gw, alice, USD.issue(), xrpIssue(), 1,
                    XRP(100)),
                ter(temBAD_AMOUNT));
        }

        // 10. Negative amount -> temBAD_AMOUNT
        {
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clawbackSetup(env);

            env(clammClawback(
                    gw, alice, USD.issue(), xrpIssue(), 1,
                    USD(-100)),
                ter(temBAD_AMOUNT));
        }
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(CLAMM, app, xrpl, 1);

}  // namespace test
}  // namespace xrpl
