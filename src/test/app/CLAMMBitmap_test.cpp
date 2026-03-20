#include <test/jtx.h>
#include <test/jtx/CLAMM.h>
#include <test/jtx/Env.h>

#include <xrpl/protocol/CLAMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/transactors/dex/CLAMMHelpers.h>

namespace xrpl {
namespace test {

struct CLAMMBitmap_test : public beast::unit_test::suite
{
    jtx::Account const gw{"gateway"};
    jtx::Account const alice{"alice"};
    jtx::Account const bob{"bob"};
    jtx::Account const carol{"carol"};
    jtx::IOU const USD{gw["USD"]};

    bool
    hasBitmapWord(
        jtx::Env& env,
        uint256 const& poolID,
        std::int16_t wordPos)
    {
        return env.current()->read(
                   keylet::clammTickBitmap(poolID, wordPos)) != nullptr;
    }

    void
    testBitmapCreatedOnDeposit()
    {
        testcase("Bitmap created on deposit");
        using namespace jtx;

        Env env(*this, testable_amendments() | featureCLAMM);
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const poolID = clammPoolID(xrpIssue(), USD.issue(), 2);
        auto const sp = clammDefaultSqrtPrice();

        env(clammCreate(env, alice, xrpIssue(), USD.issue(), 2, sp));
        env.close();

        env(clammDeposit(bob, poolID, -120, 120, XRP(10'000), USD(10'000)));
        env.close();

        // -120/60 = -2, word = -1, bit = 254
        // 120/60 = 2, word = 0, bit = 2
        BEAST_EXPECT(hasBitmapWord(env, poolID, -1));
        BEAST_EXPECT(hasBitmapWord(env, poolID, 0));
    }

    void
    testBitmapClearedOnWithdraw()
    {
        testcase("Bitmap cleared on withdraw");
        using namespace jtx;

        Env env(*this, testable_amendments() | featureCLAMM);
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const poolID = clammPoolID(xrpIssue(), USD.issue(), 2);

        env(clammCreate(env, alice, xrpIssue(), USD.issue(), 2,
            clammDefaultSqrtPrice()));
        env.close();

        env(clammDeposit(bob, poolID, -120, 120, XRP(10'000), USD(10'000)));
        env.close();

        BEAST_EXPECT(hasBitmapWord(env, poolID, -1));
        BEAST_EXPECT(hasBitmapWord(env, poolID, 0));

        auto const nftID = clammFindPositionNFT(env, bob, poolID);
        BEAST_EXPECT(nftID.has_value());

        if (nftID)
        {
            env(clammWithdraw(bob, *nftID));
            env.close();

            BEAST_EXPECT(!hasBitmapWord(env, poolID, -1));
            BEAST_EXPECT(!hasBitmapWord(env, poolID, 0));
        }
    }

    void
    testMultipleTicksSameWord()
    {
        testcase("Multiple ticks in same bitmap word");
        using namespace jtx;

        Env env(*this, testable_amendments() | featureCLAMM);
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const poolID = clammPoolID(xrpIssue(), USD.issue(), 2);

        env(clammCreate(env, alice, xrpIssue(), USD.issue(), 2,
            clammDefaultSqrtPrice()));
        env.close();

        env(clammDeposit(bob, poolID, 0, 120, XRP(5'000), USD(5'000)));
        env.close();

        env(clammDeposit(carol, poolID, 60, 180, XRP(5'000), USD(5'000)));
        env.close();

        BEAST_EXPECT(hasBitmapWord(env, poolID, 0));
    }

    void
    testBitmapLookup()
    {
        testcase("Bitmap lookup finds next tick");
        using namespace jtx;

        Env env(*this, testable_amendments() | featureCLAMM);
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const poolID = clammPoolID(xrpIssue(), USD.issue(), 2);

        env(clammCreate(env, alice, xrpIssue(), USD.issue(), 2,
            clammDefaultSqrtPrice()));
        env.close();

        env(clammDeposit(bob, poolID, -600, 600, XRP(10'000), USD(10'000)));
        env.close();

        auto const nextUp = clamm::findNextInitializedTickBitmap(
            *env.current(), poolID, 0, 60, false);
        BEAST_EXPECT(nextUp.has_value());
        if (nextUp)
            BEAST_EXPECT(nextUp->first == 600);

        auto const nextDown = clamm::findNextInitializedTickBitmap(
            *env.current(), poolID, 0, 60, true);
        BEAST_EXPECT(nextDown.has_value());
        if (nextDown)
            BEAST_EXPECT(nextDown->first == -600);
    }

    void
    testSwapUsesBitmap()
    {
        testcase("Swap uses bitmap for tick lookup");
        using namespace jtx;

        Env env(*this, testable_amendments() | featureCLAMM);
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const poolID = clammPoolID(xrpIssue(), USD.issue(), 2);

        env(clammCreate(env, alice, xrpIssue(), USD.issue(), 2,
            clammDefaultSqrtPrice()));
        env.close();

        env(clammDeposit(bob, poolID, -600, 600, XRP(10'000), USD(10'000)));
        env.close();

        auto const carolUsdBefore = env.balance(carol, USD);

        env(clammSwap(carol, poolID, XRP(100)));
        env.close();

        auto const carolUsdAfter = env.balance(carol, USD);
        BEAST_EXPECT(carolUsdAfter > carolUsdBefore);
    }

    void
    testTickBitmapPositionComputation()
    {
        testcase("tickBitmapPosition computation");

        {
            auto const [word, bit] = clamm::tickBitmapPosition(120, 60);
            BEAST_EXPECT(word == 0);
            BEAST_EXPECT(bit == 2);
        }
        {
            auto const [word, bit] = clamm::tickBitmapPosition(-120, 60);
            BEAST_EXPECT(word == -1);
            BEAST_EXPECT(bit == 254);
        }
        {
            auto const [word, bit] = clamm::tickBitmapPosition(0, 60);
            BEAST_EXPECT(word == 0);
            BEAST_EXPECT(bit == 0);
        }
        {
            auto const [word, bit] = clamm::tickBitmapPosition(887220, 60);
            BEAST_EXPECT(word == 57);
            BEAST_EXPECT(bit == 195);
        }
    }

    void
    testPartialWithdrawPreservesBitmap()
    {
        testcase("Partial withdraw preserves bitmap");
        using namespace jtx;

        Env env(*this, testable_amendments() | featureCLAMM);
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const poolID = clammPoolID(xrpIssue(), USD.issue(), 2);

        env(clammCreate(env, alice, xrpIssue(), USD.issue(), 2,
            clammDefaultSqrtPrice()));
        env.close();

        env(clammDeposit(bob, poolID, -120, 120, XRP(5'000), USD(5'000)));
        env.close();

        env(clammDeposit(carol, poolID, -120, 120, XRP(5'000), USD(5'000)));
        env.close();

        BEAST_EXPECT(hasBitmapWord(env, poolID, -1));
        BEAST_EXPECT(hasBitmapWord(env, poolID, 0));

        auto const nftID = clammFindPositionNFT(env, bob, poolID);
        BEAST_EXPECT(nftID.has_value());
        if (nftID)
        {
            env(clammWithdraw(bob, *nftID));
            env.close();

            // Carol's ticks still exist
            BEAST_EXPECT(hasBitmapWord(env, poolID, -1));
            BEAST_EXPECT(hasBitmapWord(env, poolID, 0));
        }
    }

    void
    testDifferentTickSpacings()
    {
        testcase("Bitmap with different tick spacings");
        using namespace jtx;

        Env env(*this, testable_amendments() | featureCLAMM);
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const poolID = clammPoolID(xrpIssue(), USD.issue(), 0);

        env(clammCreate(env, alice, xrpIssue(), USD.issue(), 0,
            clammDefaultSqrtPrice()));
        env.close();

        env(clammDeposit(bob, poolID, -10, 10, XRP(10'000), USD(10'000)));
        env.close();

        // spacing=1: compressed=-10 => word=-1; compressed=10 => word=0
        BEAST_EXPECT(hasBitmapWord(env, poolID, -1));
        BEAST_EXPECT(hasBitmapWord(env, poolID, 0));
    }

    void
    run() override
    {
        testBitmapCreatedOnDeposit();
        testBitmapClearedOnWithdraw();
        testMultipleTicksSameWord();
        testBitmapLookup();
        testSwapUsesBitmap();
        testTickBitmapPositionComputation();
        testPartialWithdrawPreservesBitmap();
        testDifferentTickSpacings();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(CLAMMBitmap, app, xrpl, 1);

}  // namespace test
}  // namespace xrpl
