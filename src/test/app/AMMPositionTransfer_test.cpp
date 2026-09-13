#include <test/jtx/AMMTest.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/flags.h>
#include <test/jtx/pay.h>
#include <test/jtx/trust.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

namespace xrpl::test {

struct AMMPositionTransfer_test : public jtx::AMMTest
{
private:
    static FeatureBitset
    testableAmendments()
    {
        return jtx::testableAmendments() - featureSingleAssetVault - featureLendingProtocol;
    }

    static void
    fundAccount(
        jtx::Env& env,
        jtx::Account const& gw,
        jtx::Account const& acct,
        jtx::IOU const& usd,
        jtx::IOU const& eur)
    {
        using namespace jtx;
        env.fund(XRP(100000), acct);
        env.trust(usd(1000000), acct);
        env.trust(eur(1000000), acct);
        env(pay(gw, acct, usd(100000)));
        env(pay(gw, acct, eur(100000)));
        env.close();
    }

    static json::Value
    ammCreateJV(
        jtx::Env& env,
        jtx::Account const& acct,
        jtx::IOU const& usd,
        jtx::IOU const& eur,
        STAmount const& amt1,
        STAmount const& amt2)
    {
        json::Value jv;
        jv[jss::Account] = acct.human();
        jv[jss::Amount] = amt1.getJson(JsonOptions::Values::None);
        jv[jss::Amount2] = amt2.getJson(JsonOptions::Values::None);
        jv[jss::TradingFee] = 0;
        jv[jss::TransactionType] = jss::AMMCreate;
        jv[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        return jv;
    }

    // Helper: create a CL pool, deposit one position from `lp`, return
    // (ammID, positionKeylet).
    struct PoolAndPosition
    {
        uint256 ammID;
        Keylet posKeylet;
    };

    static PoolAndPosition
    setupPool(
        jtx::Env& env,
        jtx::Account const& gw,
        jtx::Account const& lp,
        jtx::IOU const& usd,
        jtx::IOU const& eur)
    {
        using namespace jtx;

        env.fund(XRP(100000), gw);
        env.close();
        fundAccount(env, gw, lp, usd, eur);

        // Create CL pool.
        auto cv = ammCreateJV(env, lp, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = CtConcentratedLiquidity;
        cv[sfFeeTier.jsonName] = FtMedium;
        env(cv);
        env.close();

        auto const ammSle = env.current()->read(
            keylet::amm(usd.asset(), eur.asset(), CtConcentratedLiquidity));
        auto const ammID = ammSle->key();

        // Deposit a CL position spanning current tick.
        auto const seqDeposit = env.seq(lp);
        json::Value dep;
        dep[jss::Account] = lp.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] =
            STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] =
            STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = CtConcentratedLiquidity;
        dep[jss::Flags] = tfTwoAsset;
        dep[sfTickLower.jsonName] = -60;
        dep[sfTickUpper.jsonName] = 60;
        dep[jss::Amount] = usd(10).value().getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = eur(10).value().getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep);
        env.close();

        return {ammID, keylet::ammPosition(ammID, lp.id(), seqDeposit)};
    }

    static json::Value
    transferJV(
        jtx::Env& env,
        jtx::Account const& src,
        jtx::Account const& dst,
        uint256 const& positionID)
    {
        json::Value jv;
        jv[jss::Account] = src.human();
        jv[jss::TransactionType] = "AMMPositionTransfer";
        jv[jss::Destination] = dst.human();
        jv[sfPositionID.jsonName] = to_string(positionID);
        jv[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        return jv;
    }

    void
    testHappyPath(FeatureBitset features)
    {
        testcase("happy path: transfer moves ownership");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gateway");
        Account const alice("alice");
        Account const bob("bob");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        auto const pp = setupPool(env, gw, alice, usd, eur);

        // Fund bob with trustlines so future collects/withdrawals work
        // — the transfer itself doesn't require them, but post-transfer
        // value flows do.
        fundAccount(env, gw, bob, usd, eur);

        // Before transfer: alice owns.
        {
            auto const sle = env.current()->read(pp.posKeylet);
            BEAST_EXPECT(sle && (*sle)[sfAccount] == alice.id());
        }

        // Transfer.
        env(transferJV(env, alice, bob, pp.posKeylet.key));
        env.close();

        // After transfer: bob owns.
        {
            auto const sle = env.current()->read(pp.posKeylet);
            BEAST_EXPECT(sle && (*sle)[sfAccount] == bob.id());
        }

        // Bob can now collect fees on this position.
        json::Value coll;
        coll[jss::Account] = bob.human();
        coll[jss::TransactionType] = jss::AMMCollectFees;
        coll[jss::Asset] =
            STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        coll[jss::Asset2] =
            STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        coll[sfCurveType.jsonName] = CtConcentratedLiquidity;
        coll[sfPositionID.jsonName] = to_string(pp.posKeylet.key);
        coll[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(coll);
        env.close();
    }

    void
    testSourceCannotOperateAfterTransfer(FeatureBitset features)
    {
        testcase("source cannot collect or withdraw post-transfer");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gateway");
        Account const alice("alice");
        Account const bob("bob");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        auto const pp = setupPool(env, gw, alice, usd, eur);
        fundAccount(env, gw, bob, usd, eur);

        env(transferJV(env, alice, bob, pp.posKeylet.key));
        env.close();

        // Alice tries to collect on a position she no longer owns.
        json::Value coll;
        coll[jss::Account] = alice.human();
        coll[jss::TransactionType] = jss::AMMCollectFees;
        coll[jss::Asset] =
            STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        coll[jss::Asset2] =
            STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        coll[sfCurveType.jsonName] = CtConcentratedLiquidity;
        coll[sfPositionID.jsonName] = to_string(pp.posKeylet.key);
        coll[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(coll, Ter(tecNO_PERMISSION));
        env.close();
    }

    void
    testNonOwnerCannotTransfer(FeatureBitset features)
    {
        testcase("non-owner transfer attempt is rejected");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gateway");
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        auto const pp = setupPool(env, gw, alice, usd, eur);
        fundAccount(env, gw, bob, usd, eur);
        fundAccount(env, gw, carol, usd, eur);

        // Bob tries to transfer alice's position to carol.
        env(transferJV(env, bob, carol, pp.posKeylet.key),
            Ter(tecNO_PERMISSION));
        env.close();
    }

    void
    testMissingPosition(FeatureBitset features)
    {
        testcase("missing position returns tecNO_ENTRY");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gateway");
        Account const alice("alice");
        Account const bob("bob");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        env.fund(XRP(100000), gw, alice, bob);
        env.close();

        // Random non-existent position ID.
        uint256 fakeID;
        fakeID.data()[31] = 0xAB;
        env(transferJV(env, alice, bob, fakeID), Ter(tecNO_ENTRY));
        env.close();
    }

    void
    testNoDestination(FeatureBitset features)
    {
        testcase("destination account missing returns tecNO_DST");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gateway");
        Account const alice("alice");
        Account const bob("bob");  // not funded — does not exist on ledger
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        auto const pp = setupPool(env, gw, alice, usd, eur);

        env(transferJV(env, alice, bob, pp.posKeylet.key), Ter(tecNO_DST));
        env.close();
    }

    void
    testDepositAuthBlocks(FeatureBitset features)
    {
        testcase("destination with DepositAuth blocks unauthorized transfer");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gateway");
        Account const alice("alice");
        Account const bob("bob");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        auto const pp = setupPool(env, gw, alice, usd, eur);
        fundAccount(env, gw, bob, usd, eur);
        env(fset(bob, asfDepositAuth));
        env.close();

        env(transferJV(env, alice, bob, pp.posKeylet.key),
            Ter(tecNO_PERMISSION));
        env.close();
    }

    void
    testRedundant(FeatureBitset features)
    {
        testcase("source == destination is temREDUNDANT");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gateway");
        Account const alice("alice");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        auto const pp = setupPool(env, gw, alice, usd, eur);

        env(transferJV(env, alice, alice, pp.posKeylet.key),
            Ter(temREDUNDANT));
        env.close();
    }

    void
    testAmendmentDisabled()
    {
        testcase("amendment disabled: AMMPositionTransfer is temDISABLED");
        using namespace jtx;

        // Build a feature set WITHOUT featureAMMCurves.
        Env env(*this, testableAmendments() - featureAMMCurves);
        Account const alice("alice");
        Account const bob("bob");
        env.fund(XRP(100000), alice, bob);
        env.close();

        // Use a bogus position keylet — preflight should reject before
        // anything else matters.
        uint256 fakeID;
        env(transferJV(env, alice, bob, fakeID), Ter(temDISABLED));
        env.close();
    }

    void
    testMalformed(FeatureBitset features)
    {
        testcase("malformed transactions are rejected");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const alice("alice");
        Account const bob("bob");
        env.fund(XRP(100000), alice, bob);
        env.close();

        // Missing PositionID.
        {
            json::Value jv;
            jv[jss::Account] = alice.human();
            jv[jss::TransactionType] = "AMMPositionTransfer";
            jv[jss::Destination] = bob.human();
            jv[jss::Fee] =
                std::to_string(env.current()->fees().increment.drops());
            env(jv, Ter(temMALFORMED));
            env.close();
        }

        // Missing Destination.
        {
            json::Value jv;
            jv[jss::Account] = alice.human();
            jv[jss::TransactionType] = "AMMPositionTransfer";
            jv[sfPositionID.jsonName] = to_string(uint256{0});
            jv[jss::Fee] =
                std::to_string(env.current()->fees().increment.drops());
            env(jv, Ter(temMALFORMED));
            env.close();
        }
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testHappyPath(features);
        testSourceCannotOperateAfterTransfer(features);
        testNonOwnerCannotTransfer(features);
        testMissingPosition(features);
        testNoDestination(features);
        testDepositAuthBlocks(features);
        testRedundant(features);
        testMalformed(features);
        testAmendmentDisabled();
    }

public:
    void
    run() override
    {
        auto const features = testableAmendments();
        testWithFeats(features);
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(AMMPositionTransfer, app, xrpl, 1);

}  // namespace xrpl::test
