#include <test/jtx/AMMTest.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/deposit.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/paths.h>
#include <test/jtx/pay.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <string>

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
        env.close();
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
        cv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        cv[sfFeeTier.jsonName] = kFtMedium;
        env(cv);
        env.close();

        auto const ammSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity));
        auto const ammID = ammSle->key();

        // Deposit a CL position spanning current tick.
        auto const seqDeposit = env.seq(lp);
        json::Value dep;
        dep[jss::Account] = lp.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        dep[jss::Flags] = tfTwoAsset;
        dep[sfTickLower.jsonName] = -60;
        dep[sfTickUpper.jsonName] = 60;
        dep[jss::Amount] = usd(10).value().getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = eur(10).value().getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep);
        env.close();

        return {.ammID = ammID, .posKeylet = keylet::ammPosition(ammID, lp.id(), seqDeposit)};
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

        // Transfer, once bob has preauthorized alice.
        env(deposit::auth(bob, alice));
        env.close();
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
        coll[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        coll[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        coll[sfCurveType.jsonName] = kCtConcentratedLiquidity;
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
        env(deposit::auth(bob, alice));
        env.close();

        env(transferJV(env, alice, bob, pp.posKeylet.key));
        env.close();

        // Alice tries to collect on a position she no longer owns.
        json::Value coll;
        coll[jss::Account] = alice.human();
        coll[jss::TransactionType] = jss::AMMCollectFees;
        coll[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        coll[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        coll[sfCurveType.jsonName] = kCtConcentratedLiquidity;
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
        env(transferJV(env, bob, carol, pp.posKeylet.key), Ter(tecNO_PERMISSION));
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

        env(transferJV(env, alice, bob, pp.posKeylet.key), Ter(tecNO_PERMISSION));
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

        env(transferJV(env, alice, alice, pp.posKeylet.key), Ter(temREDUNDANT));
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
        uint256 const fakeID;
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
            jv[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(jv, Ter(temMALFORMED));
            env.close();
        }

        // Missing Destination.
        {
            json::Value jv;
            jv[jss::Account] = alice.human();
            jv[jss::TransactionType] = "AMMPositionTransfer";
            jv[sfPositionID.jsonName] = to_string(uint256{0});
            jv[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(jv, Ter(temMALFORMED));
            env.close();
        }
    }

    void
    testFlagsPreauthAndReserve(FeatureBitset features)
    {
        testcase("flags, a preauthorized DepositAuth destination, and the destination reserve");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gateway");
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        auto const pp = setupPool(env, gw, alice, usd, eur);

        // Only universal flags.
        {
            auto jv = transferJV(env, alice, bob, pp.posKeylet.key);
            jv[jss::Flags] = tfTwoAsset;
            env(jv, Ter(temINVALID_FLAG));
        }

        // A destination short of the reserve for one more object cannot take the position.
        env.fund(env.current()->fees().accountReserve(1, 1) + drops(10000), carol);
        env.close();
        env(deposit::auth(carol, alice));
        env.close();
        env(transferJV(env, alice, carol, pp.posKeylet.key), Ter(tecINSUFFICIENT_RESERVE));

        // DepositAuth with the source preauthorized lets the transfer through.
        fundAccount(env, gw, bob, usd, eur);
        env(fset(bob, asfDepositAuth));
        env(deposit::auth(bob, alice));
        env.close();
        env(transferJV(env, alice, bob, pp.posKeylet.key), Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT((*env.le(pp.posKeylet))[sfAccount] == bob.id());
    }

    void
    testDestinationConsent(FeatureBitset features)
    {
        testcase("destination that has not preauthorized the source is refused");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gateway");
        Account const alice("alice");
        Account const bob("bob");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        auto const pp = setupPool(env, gw, alice, usd, eur);
        env.fund(XRP(1000), bob);
        env.close();

        auto const ownersBefore = env.le(bob)->getFieldU32(sfOwnerCount);
        env(transferJV(env, alice, bob, pp.posKeylet.key), Ter(tecNO_PERMISSION));
        env.close();
        BEAST_EXPECT(env.le(bob)->getFieldU32(sfOwnerCount) == ownersBefore);
        BEAST_EXPECT((*env.le(pp.posKeylet))[sfAccount] == alice.id());
    }

    void
    testFrozenSource(FeatureBitset features)
    {
        testcase("a source frozen for a pool asset cannot transfer its position");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gateway");
        Account const alice("alice");
        Account const bob("bob");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        auto const pp = setupPool(env, gw, alice, usd, eur);
        fundAccount(env, gw, bob, usd, eur);
        env(deposit::auth(bob, alice));
        env.close();

        // The issuer freezes alice's line for one pool asset.
        env(trust(gw, alice["EUR"](0), tfSetFreeze));
        env.close();
        env(transferJV(env, alice, bob, pp.posKeylet.key), Ter(tecFROZEN));
        env.close();
        BEAST_EXPECT((*env.le(pp.posKeylet))[sfAccount] == alice.id());

        // A global freeze of the issuer blocks it too.
        env(trust(gw, alice["EUR"](0), tfClearFreeze));
        env(fset(gw, asfGlobalFreeze));
        env.close();
        env(transferJV(env, alice, bob, pp.posKeylet.key), Ter(tecFROZEN));
        env.close();
        env(fclear(gw, asfGlobalFreeze));
        env.close();
        env(transferJV(env, alice, bob, pp.posKeylet.key), Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT((*env.le(pp.posKeylet))[sfAccount] == bob.id());
    }

    // `acct` deletes its empty line for `iou`, so the issuer's authorization of it is gone.
    void
    deleteLine(jtx::Env& env, jtx::Account const& acct, jtx::IOU const& iou)
    {
        env(jtx::trust(acct, iou(0)), jtx::Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(!env.le(keylet::trustLine(acct.id(), iou.issue())));
    }

    // `acct` deletes its line for `iou` and opens a new one the issuer has not authorized.
    void
    recreateLine(jtx::Env& env, jtx::Account const& acct, jtx::IOU const& iou)
    {
        deleteLine(env, acct, iou);
        env(jtx::trust(acct, iou(1000000)), jtx::Ter(tesSUCCESS));
        env.close();
    }

    // An AMMWithdraw of the whole CL position `positionID`.
    static json::Value
    withdrawJV(
        jtx::Env& env,
        jtx::Account const& lp,
        jtx::IOU const& usd,
        jtx::IOU const& eur,
        uint256 const& positionID)
    {
        json::Value wd;
        wd[jss::Account] = lp.human();
        wd[jss::TransactionType] = jss::AMMWithdraw;
        wd[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        wd[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        wd[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        wd[jss::Flags] = tfWithdrawAll;
        wd[sfPositionID.jsonName] = to_string(positionID);
        wd[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        return wd;
    }

    void
    testWithdrawNeedsAuthAndNoFreeze(FeatureBitset features)
    {
        testcase("CL withdraw: the withdrawer must be authorized and not frozen");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gateway");
        Account const alice("alice");
        Account const carol("carol");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        env.fund(XRP(100000), gw, alice, carol);
        env.close();
        env(fset(gw, asfRequireAuth));
        env.close();
        env(trust(alice, usd(1000000)));
        env(trust(alice, eur(1000000)));
        env(trust(gw, alice["USD"](0), tfSetfAuth));
        env(trust(gw, alice["EUR"](0), tfSetfAuth));
        env.close();
        env(pay(gw, alice, usd(100000)));
        env(pay(gw, alice, eur(100000)));
        env(trust(carol, usd(1000000)));
        env(trust(carol, eur(1000000)));
        env(trust(gw, carol["USD"](0), tfSetfAuth));
        env(trust(gw, carol["EUR"](0), tfSetfAuth));
        env.close();

        auto cv = ammCreateJV(env, alice, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        cv[sfFeeTier.jsonName] = kFtMedium;
        env(cv);
        env.close();
        auto const ammID =
            env.le(keylet::amm(usd.asset(), eur.asset(), kCtConcentratedLiquidity))->key();
        auto const depositPosition = [&](std::uint32_t seq) {
            json::Value dep;
            dep[jss::Account] = alice.human();
            dep[jss::TransactionType] = jss::AMMDeposit;
            dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            dep[sfCurveType.jsonName] = kCtConcentratedLiquidity;
            dep[jss::Flags] = tfTwoAsset;
            dep[sfTickLower.jsonName] = -60;
            dep[sfTickUpper.jsonName] = 60;
            dep[jss::Amount] = usd(10).value().getJson(JsonOptions::Values::None);
            dep[jss::Amount2] = eur(10).value().getJson(JsonOptions::Values::None);
            dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(dep);
            env.close();
            return keylet::ammPosition(ammID, alice.id(), seq);
        };
        auto const toCarol = depositPosition(env.seq(alice));
        auto const kept = depositPosition(env.seq(alice));

        // A position whose owner recreated its line without the issuer's authorization cannot be
        // withdrawn.
        env(deposit::auth(carol, alice));
        env.close();
        env(transferJV(env, alice, carol, toCarol.key), Ter(tesSUCCESS));
        env.close();
        recreateLine(env, carol, usd);
        env(withdrawJV(env, carol, usd, eur, toCarol.key), Ter(tecNO_AUTH));
        env.close();
        BEAST_EXPECT(env.balance(carol, usd.issue()) == usd(0));
        BEAST_EXPECT(env.balance(carol, eur.issue()) == eur(0));

        // A withdrawer deep-frozen for a pool asset cannot receive it.
        env(trust(gw, alice["EUR"](0), tfSetFreeze | tfSetDeepFreeze));
        env.close();
        env(withdrawJV(env, alice, usd, eur, kept.key), Ter(tecFROZEN));
        env.close();
        env(trust(gw, alice["EUR"](0), tfClearFreeze | tfClearDeepFreeze));
        env.close();
        env(trust(gw, alice["USD"](0), tfSetFreeze | tfSetDeepFreeze));
        env.close();
        env(withdrawJV(env, alice, usd, eur, kept.key), Ter(tecFROZEN));
        env.close();
        env(trust(gw, alice["USD"](0), tfClearFreeze | tfClearDeepFreeze));
        env.close();
        env(withdrawJV(env, alice, usd, eur, kept.key), Ter(tesSUCCESS));
        env.close();
    }

    // A CL deposit of [-60, 60] by `lp` naming the two pool assets; returns its keylet.
    static Keylet
    depositRange(
        jtx::Env& env,
        jtx::Account const& lp,
        Asset const& a,
        Asset const& b,
        STAmount const& amount,
        STAmount const& amount2)
    {
        auto const seq = env.seq(lp);
        json::Value dep;
        dep[jss::Account] = lp.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] = STIssue(sfAsset, a).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] = STIssue(sfAsset, b).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        dep[jss::Flags] = tfTwoAsset;
        dep[sfTickLower.jsonName] = -60;
        dep[sfTickUpper.jsonName] = 60;
        dep[jss::Amount] = amount.getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = amount2.getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep, jtx::Ter(tesSUCCESS));
        env.close();
        return keylet::ammPosition(
            env.le(keylet::amm(a, b, kCtConcentratedLiquidity))->key(), lp.id(), seq);
    }

    static json::Value
    collectJV(
        jtx::Env& env,
        jtx::Account const& owner,
        Asset const& a,
        Asset const& b,
        uint256 const& positionID)
    {
        json::Value coll;
        coll[jss::Account] = owner.human();
        coll[jss::TransactionType] = jss::AMMCollectFees;
        coll[jss::Asset] = STIssue(sfAsset, a).getJson(JsonOptions::Values::None);
        coll[jss::Asset2] = STIssue(sfAsset, b).getJson(JsonOptions::Values::None);
        coll[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        coll[sfPositionID.jsonName] = to_string(positionID);
        coll[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        return coll;
    }

    // A partial payment of `out` through the pool's book `book`, spending at most `max`.
    static void
    trade(
        jtx::Env& env,
        jtx::Account const& trader,
        STAmount const& out,
        jtx::BookSpec const& book,
        STAmount const& max)
    {
        using namespace jtx;
        env(pay(trader, trader, out),
            Path(book),
            Sendmax(max),
            Txflags(tfNoRippleDirect | tfPartialPayment),
            Ter(tesSUCCESS));
        env.close();
    }

    void
    testCollectNeedsAuthAndNoFreeze(FeatureBitset features)
    {
        testcase("CL collect fees: the owner must be authorized and not deep-frozen");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gateway");
        Account const alice("alice");
        Account const carol("carol");
        Account const trader("trader");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        env.fund(XRP(100000), gw, alice, carol, trader);
        env.close();
        env(fset(gw, asfRequireAuth));
        env.close();
        for (auto const& acct : {alice, trader})
        {
            env(trust(acct, usd(1000000)));
            env(trust(acct, eur(1000000)));
            env(trust(gw, acct["USD"](0), tfSetfAuth));
            env(trust(gw, acct["EUR"](0), tfSetfAuth));
            env.close();
            env(pay(gw, acct, usd(100000)));
            env(pay(gw, acct, eur(100000)));
            env.close();
        }
        env(trust(carol, usd(1000000)));
        env(trust(carol, eur(1000000)));
        env(trust(gw, carol["USD"](0), tfSetfAuth));
        env(trust(gw, carol["EUR"](0), tfSetfAuth));
        env.close();

        auto cv = ammCreateJV(env, alice, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        cv[sfFeeTier.jsonName] = kFtMedium;
        env(cv, Ter(tesSUCCESS));
        env.close();
        auto const toCarol = depositRange(env, alice, usd.asset(), eur.asset(), usd(100), eur(100));
        auto const kept = depositRange(env, alice, usd.asset(), eur.asset(), usd(100), eur(100));
        // Trades both ways, so every in-range position earns fees in both assets.
        trade(env, trader, eur(5).value(), ~eur, usd(10).value());
        trade(env, trader, usd(5).value(), ~usd, eur(10).value());

        // An owner that deleted its authorized line collects nothing, and nothing once it opens
        // an unauthorized one.
        env(deposit::auth(carol, alice));
        env.close();
        env(transferJV(env, alice, carol, toCarol.key), Ter(tesSUCCESS));
        env.close();
        deleteLine(env, carol, usd);
        env(collectJV(env, carol, usd.asset(), eur.asset(), toCarol.key), Ter(tecNO_LINE));
        env.close();
        env(trust(carol, usd(1000000)), Ter(tesSUCCESS));
        env.close();
        env(collectJV(env, carol, usd.asset(), eur.asset(), toCarol.key), Ter(tecNO_AUTH));
        env.close();
        BEAST_EXPECT(env.balance(carol, usd.issue()) == usd(0));
        BEAST_EXPECT(env.balance(carol, eur.issue()) == eur(0));

        // An owner deep-frozen for a pool asset collects nothing until the freeze clears.
        env(trust(gw, alice["EUR"](0), tfSetFreeze | tfSetDeepFreeze));
        env.close();
        env(collectJV(env, alice, usd.asset(), eur.asset(), kept.key), Ter(tecFROZEN));
        env.close();
        env(trust(gw, alice["EUR"](0), tfClearFreeze | tfClearDeepFreeze));
        env.close();
        env(collectJV(env, alice, usd.asset(), eur.asset(), kept.key), Ter(tesSUCCESS));
        env.close();
    }

    void
    testCollectMPTNeedsAuthAndNoLock(FeatureBitset features)
    {
        testcase("CL collect fees on an MPT pool: the owner must be authorized and not locked");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gateway");
        Account const alice("alice");
        Account const carol("carol");
        Account const trader("trader");
        auto const usd = gw["USD"];
        env.fund(XRP(100000), gw, alice, carol, trader);
        env.close();
        for (auto const& acct : {alice, trader})
        {
            env.trust(usd(1000000), acct);
            env.close();
            env(pay(gw, acct, usd(100000)));
            env.close();
        }
        MPTTester btc({
            .env = env,
            .issuer = gw,
            .holders = {alice, trader},
            .pay = 100000,
            .flags = kMptDexFlags | tfMPTRequireAuth | tfMPTCanLock,
            .authHolder = true,
        });
        Asset const mpt = btc;
        btc.authorize({.account = carol});
        btc.authorize({.account = gw, .holder = carol});

        auto cv = ammCreateJV(env, alice, usd, usd, STAmount{mpt, 1000}, usd(1000));
        cv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        cv[sfFeeTier.jsonName] = kFtMedium;
        env(cv, Ter(tesSUCCESS));
        env.close();
        auto const toCarol =
            depositRange(env, alice, mpt, usd.asset(), STAmount{mpt, 100}, usd(100));
        auto const kept = depositRange(env, alice, mpt, usd.asset(), STAmount{mpt, 100}, usd(100));
        trade(env, trader, usd(5).value(), ~usd, STAmount{mpt, 10});
        trade(env, trader, STAmount{mpt, 5}, ~btc, usd(10).value());

        env(deposit::auth(carol, alice));
        env.close();
        env(transferJV(env, alice, carol, toCarol.key), Ter(tesSUCCESS));
        env.close();
        // An owner whose authorization the issuer withdrew collects nothing.
        btc.authorize({.account = gw, .holder = carol, .flags = tfMPTUnauthorize});
        env(collectJV(env, carol, mpt, usd.asset(), toCarol.key), Ter(tecNO_AUTH));
        env.close();

        // An owner whose MPToken is locked collects nothing until the lock clears.
        btc.set({.account = gw, .holder = alice, .flags = tfMPTLock});
        env(collectJV(env, alice, mpt, usd.asset(), kept.key), Ter(tecLOCKED));
        env.close();
        btc.set({.account = gw, .holder = alice, .flags = tfMPTUnlock});
        env(collectJV(env, alice, mpt, usd.asset(), kept.key), Ter(tesSUCCESS));
        env.close();
    }

    // Expects a transfer of `pos` from `src` to `dst` refused with `ter`, the position and the
    // destination's owner count unchanged.
    void
    expectTransferRefused(
        jtx::Env& env,
        jtx::Account const& src,
        jtx::Account const& dst,
        Keylet const& pos,
        TER ter)
    {
        auto const ownersBefore = env.le(dst)->getFieldU32(sfOwnerCount);
        env(transferJV(env, src, dst, pos.key), jtx::Ter(ter));
        env.close();
        BEAST_EXPECT((*env.le(pos))[sfAccount] == src.id());
        BEAST_EXPECT(env.le(dst)->getFieldU32(sfOwnerCount) == ownersBefore);
    }

    void
    testTransferNeedsDestinationAuthAndNoFreeze(FeatureBitset features)
    {
        testcase("transfer: the destination must be authorized and not deep-frozen");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gateway");
        Account const alice("alice");
        Account const bob("bob");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        env.fund(XRP(100000), gw, alice, bob);
        env.close();
        env(fset(gw, asfRequireAuth));
        env.close();
        env(trust(alice, usd(1000000)));
        env(trust(alice, eur(1000000)));
        env(trust(gw, alice["USD"](0), tfSetfAuth));
        env(trust(gw, alice["EUR"](0), tfSetfAuth));
        env.close();
        env(pay(gw, alice, usd(100000)));
        env(pay(gw, alice, eur(100000)));
        env(trust(bob, usd(1000000)));
        env(trust(bob, eur(1000000)));
        env(deposit::auth(bob, alice));
        env.close();

        auto cv = ammCreateJV(env, alice, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        cv[sfFeeTier.jsonName] = kFtMedium;
        env(cv, Ter(tesSUCCESS));
        env.close();
        auto const pos = depositRange(env, alice, usd.asset(), eur.asset(), usd(100), eur(100));

        // Neither of bob's lines is authorized, then only the first.
        expectTransferRefused(env, alice, bob, pos, tecNO_AUTH);
        env(trust(gw, bob["USD"](0), tfSetfAuth));
        env.close();
        expectTransferRefused(env, alice, bob, pos, tecNO_AUTH);
        env(trust(gw, bob["EUR"](0), tfSetfAuth));
        env.close();

        // Bob deep-frozen for the second asset.
        env(trust(gw, bob["EUR"](0), tfSetFreeze | tfSetDeepFreeze));
        env.close();
        expectTransferRefused(env, alice, bob, pos, tecFROZEN);
        env(trust(gw, bob["EUR"](0), tfClearFreeze | tfClearDeepFreeze));
        env.close();

        env(transferJV(env, alice, bob, pos.key), Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT((*env.le(pos))[sfAccount] == bob.id());
    }

    void
    testTransferMPTNeedsDestinationAuthAndNoLock(FeatureBitset features)
    {
        testcase("transfer on an MPT pool: the destination must be authorized and not locked");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gateway");
        Account const alice("alice");
        Account const bob("bob");
        auto const usd = gw["USD"];
        env.fund(XRP(100000), gw, alice, bob);
        env.close();
        env.trust(usd(1000000), alice, bob);
        env.close();
        env(pay(gw, alice, usd(100000)));
        env(deposit::auth(bob, alice));
        env.close();
        MPTTester btc({
            .env = env,
            .issuer = gw,
            .holders = {alice},
            .pay = 100000,
            .flags = kMptDexFlags | tfMPTRequireAuth | tfMPTCanLock,
            .authHolder = true,
        });
        Asset const mpt = btc;

        auto cv = ammCreateJV(env, alice, usd, usd, STAmount{mpt, 1000}, usd(1000));
        cv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        cv[sfFeeTier.jsonName] = kFtMedium;
        env(cv, Ter(tesSUCCESS));
        env.close();
        auto const pos = depositRange(env, alice, mpt, usd.asset(), STAmount{mpt, 100}, usd(100));

        // Bob holds the MPT without the issuer's authorization.
        btc.authorize({.account = bob});
        expectTransferRefused(env, alice, bob, pos, tecNO_AUTH);
        btc.authorize({.account = gw, .holder = bob});

        // Bob's MPToken locked.
        btc.set({.account = gw, .holder = bob, .flags = tfMPTLock});
        expectTransferRefused(env, alice, bob, pos, tecLOCKED);
        btc.set({.account = gw, .holder = bob, .flags = tfMPTUnlock});

        env(transferJV(env, alice, bob, pos.key), Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT((*env.le(pos))[sfAccount] == bob.id());
    }

    void
    testCollectMPTWithoutMPToken(FeatureBitset features)
    {
        testcase("CL collect fees on an MPT pool: an owner with no MPToken gets one");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gateway");
        Account const alice("alice");
        Account const bob("bob");
        Account const trader("trader");
        auto const usd = gw["USD"];
        env.fund(XRP(100000), gw, alice, trader);
        // Bob covers the reserve of his USD line, preauthorization and position, not of an
        // MPToken too.
        env.fund(env.current()->fees().accountReserve(3, 1) + drops(1000000), bob);
        env.close();
        env.trust(usd(1000000), alice, bob, trader);
        env.close();
        env(pay(gw, alice, usd(100000)));
        env(pay(gw, trader, usd(100000)));
        env(deposit::auth(bob, alice));
        env.close();
        MPTTester const btc({
            .env = env,
            .issuer = gw,
            .holders = {alice, trader},
            .pay = 100000,
            .flags = kMptDexFlags,
        });
        Asset const mpt = btc;
        auto const bobToken = keylet::mptoken(btc.issuanceID(), bob.id());

        auto cv = ammCreateJV(env, alice, usd, usd, STAmount{mpt, 1000}, usd(1000));
        cv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        cv[sfFeeTier.jsonName] = kFtMedium;
        env(cv, Ter(tesSUCCESS));
        env.close();
        auto const pos =
            depositRange(env, alice, mpt, usd.asset(), STAmount{mpt, 50000}, usd(50000));
        env(transferJV(env, alice, bob, pos.key), Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(!env.le(bobToken));
        // The tier's 0.3% fee on trades this size is whole units of the MPT.
        trade(env, trader, usd(10000).value(), ~usd, STAmount{mpt, 20000});
        trade(env, trader, STAmount{mpt, 10000}, ~btc, usd(20000).value());

        // The fee in the MPT needs an MPToken, which bob cannot reserve for yet.
        env(collectJV(env, bob, mpt, usd.asset(), pos.key), Ter(tecINSUFFICIENT_RESERVE));
        env.close();
        BEAST_EXPECT(!env.le(bobToken));

        // With the reserve, the collect creates the MPToken and pays the fee into it.
        env(pay(gw, bob, XRP(1000)));
        env.close();
        auto const owners = env.le(bob)->getFieldU32(sfOwnerCount);
        env(collectJV(env, bob, mpt, usd.asset(), pos.key), Ter(tesSUCCESS));
        env.close();
        auto const token = env.le(bobToken);
        BEAST_EXPECT(token && token->getFieldU64(sfMPTAmount) > 0);
        BEAST_EXPECT(env.le(bob)->getFieldU32(sfOwnerCount) == owners + 1);

        // A second position without an MPToken behind it is withdrawn the same way.
        auto const carolPos =
            depositRange(env, alice, mpt, usd.asset(), STAmount{mpt, 100}, usd(100));
        Account const carol("carol");
        env.fund(XRP(10000), carol);
        env.close();
        env.trust(usd(1000000), carol);
        env(deposit::auth(carol, alice));
        env.close();
        env(transferJV(env, alice, carol, carolPos.key), Ter(tesSUCCESS));
        env.close();
        json::Value wd;
        wd[jss::Account] = carol.human();
        wd[jss::TransactionType] = jss::AMMWithdraw;
        wd[jss::Asset] = STIssue(sfAsset, mpt).getJson(JsonOptions::Values::None);
        wd[jss::Asset2] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        wd[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        wd[jss::Flags] = tfWithdrawAll;
        wd[sfPositionID.jsonName] = to_string(carolPos.key);
        wd[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(wd, Ter(tesSUCCESS));
        env.close();
        auto const carolToken = env.le(keylet::mptoken(btc.issuanceID(), carol.id()));
        BEAST_EXPECT(carolToken && carolToken->getFieldU64(sfMPTAmount) > 0);
    }

    void
    testCollectMPTToIssuerAndFirstObjects(FeatureBitset features)
    {
        testcase("CL collect fees on an MPT pool: the issuer, and an owner's first objects");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gateway");
        Account const alice("alice");
        Account const dave("dave");
        Account const trader("trader");
        auto const usd = gw["USD"];
        env.fund(XRP(100000), gw, alice, trader);
        env.fund(XRP(10000), dave);
        env.close();
        MPTTester const btc({
            .env = env,
            .issuer = gw,
            .holders = {alice, trader},
            .pay = 100000,
            .flags = kMptDexFlags,
        });
        Asset const mpt = btc;
        Asset const xrp = xrpIssue();

        auto cv = ammCreateJV(env, alice, usd, usd, STAmount{mpt, 1000}, XRP(1000).value());
        cv[sfCurveType.jsonName] = kCtConcentratedLiquidity;
        cv[sfFeeTier.jsonName] = kFtMedium;
        env(cv, Ter(tesSUCCESS));
        env.close();
        auto const toIssuer = depositRange(env, alice, mpt, xrp, STAmount{mpt, 20000}, XRP(20000));
        auto const toDave = depositRange(env, alice, mpt, xrp, STAmount{mpt, 20000}, XRP(20000));
        env(deposit::auth(gw, alice));
        env(deposit::auth(dave, alice));
        env.close();
        env(transferJV(env, alice, gw, toIssuer.key), Ter(tesSUCCESS));
        env(transferJV(env, alice, dave, toDave.key), Ter(tesSUCCESS));
        env.close();
        // Dave's preauthorization goes, leaving the position his one object; he keeps only
        // that object's reserve plus 1 XRP, short of the reserve for two.
        env(deposit::unauth(dave, alice));
        env.close();
        BEAST_EXPECT(env.le(dave)->getFieldU32(sfOwnerCount) == 1);
        XRPAmount const keep = env.current()->fees().accountReserve(1, 1) + XRPAmount{1000000};
        env(pay(dave, gw, STAmount{env.le(dave)->getFieldAmount(sfBalance).xrp() - keep}));
        env.close();
        trade(env, trader, XRP(5000).value(), ~XRP, STAmount{mpt, 10000});
        trade(env, trader, STAmount{mpt, 5000}, ~btc, XRP(10000).value());

        // The issuer is paid its own MPT without an MPToken.
        env(collectJV(env, gw, mpt, xrp, toIssuer.key), Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(!env.le(keylet::mptoken(btc.issuanceID(), gw.id())));

        // An account's first two objects need no reserve, so dave's MPToken is free.
        env(collectJV(env, dave, mpt, xrp, toDave.key), Ter(tesSUCCESS));
        env.close();
        auto const token = env.le(keylet::mptoken(btc.issuanceID(), dave.id()));
        BEAST_EXPECT(token && token->getFieldU64(sfMPTAmount) > 0);
        // Nothing stays owed on an XRP pool's positions: the fields hold no XRP zero.
        for (auto const& pos : {toIssuer, toDave})
        {
            BEAST_EXPECT(
                !env.le(pos)->isFieldPresent(sfTokensOwed0) &&
                !env.le(pos)->isFieldPresent(sfTokensOwed1));
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
        testFlagsPreauthAndReserve(features);
        testDestinationConsent(features);
        testFrozenSource(features);
        testWithdrawNeedsAuthAndNoFreeze(features);
        testCollectNeedsAuthAndNoFreeze(features);
        testCollectMPTNeedsAuthAndNoLock(features);
        testTransferNeedsDestinationAuthAndNoFreeze(features);
        testTransferMPTNeedsDestinationAuthAndNoLock(features);
        testCollectMPTWithoutMPToken(features);
        testCollectMPTToIssuerAndFirstObjects(features);
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
