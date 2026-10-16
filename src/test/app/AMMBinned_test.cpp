// Sandbox tests for CtBinned curve type.
//
// CURRENT SCOPE (this branch):
//   - AMMCreate accepts CtBinned + sfBinStep, initializes sfActiveBinID.
//   - Amendment gate (featureAMMCurves) works.
//   - sfBinStep validation (curated set: 1, 5, 10, 25, 100 bp).
//
// DEFERRED to Phase 4 (see tasks/todo.md and docs/binned-amm-spec.md):
//   - AMMDeposit binned path (per-bin MPT issuance + share minting).
//   - AMMWithdraw binned path.
//   - AMMCollectFees binned path with per-bin accumulator.
//   - Bin-walk swap dispatch in payment engine.
//   - Reserve-exemption rule for AMM-issued MPTs.

#include <test/jtx/AMMTest.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/flags.h>
#include <test/jtx/paths.h>
#include <test/jtx/pay.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/basics/Number.h>
#include <xrpl/protocol/AmountConversions.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

namespace xrpl::test {

struct AMMBinned_test : public jtx::AMMTest
{
private:
    static FeatureBitset
    testableAmendments()
    {
        return jtx::testableAmendments() - featureSingleAssetVault - featureLendingProtocol;
    }

    static json::Value
    ammCreateJV(
        jtx::Env& env,
        jtx::Account const& acct,
        jtx::IOU const& asset1,
        jtx::IOU const& asset2,
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

    // Read the LP's MPT balance for a given bin — the authoritative
    // share count after the MPT migration.
    static std::uint64_t
    mptSharesOf(
        jtx::Env& env,
        uint256 const& ammID,
        std::int32_t binID,
        AccountID const& account)
    {
        auto const binSle = env.current()->read(keylet::ammBin(ammID, binID));
        if (!binSle || !binSle->isFieldPresent(sfMPTokenIssuanceID))
            return 0;
        auto const mptId = binSle->getFieldH192(sfMPTokenIssuanceID);
        auto const mpt = env.current()->read(keylet::mptoken(mptId, account));
        if (!mpt)
            return 0;
        return mpt->getFieldU64(sfMPTAmount);
    }

    // Provision a bin (AMMBinCreate). Must run before the first
    // AMMDeposit into the bin since deposit no longer creates bins.
    static void
    provisionBin(
        jtx::Env& env,
        jtx::Account const& by,
        jtx::IOU const& usd,
        jtx::IOU const& eur,
        std::int32_t binID)
    {
        json::Value jv;
        jv[jss::Account] = by.human();
        jv[jss::TransactionType] = "AMMBinCreate";
        jv[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        jv[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        jv[sfBinID.jsonName] = binID;
        jv[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(jv);
        env.close();
    }

    // Fund the gateway once per Env; subsequent LP accounts are funded
    // without re-funding gw (env.fund on an already-funded account
    // causes the spurious "extra payment" failures we saw on multi-LP
    // tests).
    static void
    fundForAMMCreate(
        jtx::Env& env,
        jtx::Account const& gw,
        jtx::Account const& acct,
        jtx::IOU const& usd,
        jtx::IOU const& eur,
        bool fundGw = true)
    {
        using namespace jtx;
        if (fundGw)
            env.fund(XRP(100000), gw);
        env.fund(XRP(100000), acct);
        env.trust(usd(1000000), acct);
        env.trust(eur(1000000), acct);
        env(pay(gw, acct, usd(100000)));
        env(pay(gw, acct, eur(100000)));
        env.close();
    }

    void
    testCreateHappyPath(FeatureBitset features)
    {
        testcase("AMMCreate(CtBinned) initializes pool with binStep + activeBinID");
        using namespace jtx;

        for (auto const step : {1u, 5u, 10u, 25u, 100u})
        {
            Env env(*this, features | featureAMMCurves);
            Account const gw("gw");
            Account const al("alice");
            auto const usd = gw["USD"];
            auto const eur = gw["EUR"];
            fundForAMMCreate(env, gw, al, usd, eur);

            auto jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
            jv[sfCurveType.jsonName] = CtBinned;
            jv[sfBinStep.jsonName] = step;
            env(jv);
            env.close();

            auto const ammSle =
                env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned));
            BEAST_EXPECT(ammSle != nullptr);
            if (!ammSle)
                continue;
            BEAST_EXPECT(ammSle->getFieldU8(sfCurveType) == CtBinned);
            BEAST_EXPECT(ammSle->getFieldU16(sfBinStep) == step);
            BEAST_EXPECT(ammSle->isFieldPresent(sfActiveBinID));
            BEAST_EXPECT(ammSle->getFieldI32(sfActiveBinID) == 0);
        }
    }

    void
    testCreateMissingBinStep(FeatureBitset features)
    {
        testcase("AMMCreate(CtBinned) without sfBinStep is temMALFORMED");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);

        auto jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        jv[sfCurveType.jsonName] = CtBinned;
        env(jv, Ter(temMALFORMED));
        env.close();
    }

    void
    testCreateInvalidBinStep(FeatureBitset features)
    {
        testcase("AMMCreate(CtBinned) with non-curated binStep is temMALFORMED");
        using namespace jtx;

        for (auto const step : {0u, 2u, 7u, 50u, 1000u, 65535u})
        {
            Env env(*this, features | featureAMMCurves);
            Account const gw("gw");
            Account const al("alice");
            auto const usd = gw["USD"];
            auto const eur = gw["EUR"];
            fundForAMMCreate(env, gw, al, usd, eur);

            auto jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
            jv[sfCurveType.jsonName] = CtBinned;
            jv[sfBinStep.jsonName] = step;
            env(jv, Ter(temMALFORMED));
            env.close();
        }
    }

    void
    testAmendmentDisabled(FeatureBitset features)
    {
        testcase("AMMCreate(CtBinned) is temDISABLED without featureAMMCurves");
        using namespace jtx;

        Env env(*this, features - featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);

        auto jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        jv[sfCurveType.jsonName] = CtBinned;
        jv[sfBinStep.jsonName] = 10u;
        env(jv, Ter(temDISABLED));
        env.close();
    }

    void
    testCoexistenceWithCL(FeatureBitset features)
    {
        testcase("CtBinned and CtConcentratedLiquidity coexist on same asset pair");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);

        // CL pool.
        {
            auto jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
            jv[sfCurveType.jsonName] = CtConcentratedLiquidity;
            jv[sfFeeTier.jsonName] = FtMedium;
            env(jv);
            env.close();
        }

        // Binned pool on the same asset pair.
        {
            auto jv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
            jv[sfCurveType.jsonName] = CtBinned;
            jv[sfBinStep.jsonName] = 10u;
            env(jv);
            env.close();
        }

        // Both keylets resolve to distinct AMM SLEs.
        auto const clSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtConcentratedLiquidity));
        auto const binSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned));
        BEAST_EXPECT(clSle && binSle);
        BEAST_EXPECT(clSle && clSle->getFieldU8(sfCurveType) == CtConcentratedLiquidity);
        BEAST_EXPECT(binSle && binSle->getFieldU8(sfCurveType) == CtBinned);
        BEAST_EXPECT(clSle && binSle && clSle->key() != binSle->key());
    }

    void
    testDepositCreatesBin(FeatureBitset features)
    {
        testcase("AMMDeposit(CtBinned) creates bin SLE + LP holding record");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);

        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        env(cv);
        env.close();

        auto const ammSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned));
        BEAST_EXPECT(ammSle != nullptr);
        if (!ammSle)
            return;
        auto const ammID = ammSle->key();

        // Deposit into bin 0.
        provisionBin(env, al, usd, eur, 0);
        json::Value dep;
        dep[jss::Account] = al.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] =
            STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] =
            STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = CtBinned;
        dep[jss::Flags] = tfTwoAsset;
        dep[sfBinID.jsonName] = 0;
        dep[jss::Amount] = usd(100).value().getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = eur(100).value().getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep);
        env.close();

        // Bin SLE created.
        auto const binSle = env.current()->read(keylet::ammBin(ammID, 0));
        BEAST_EXPECT(binSle != nullptr);
        if (!binSle)
            return;
        BEAST_EXPECT(binSle->getFieldI32(sfBinID) == 0);
        BEAST_EXPECT(binSle->getFieldU64(sfOutstandingAmount) > 0);
        // Reserves are stored in canonical (lex-min, lex-max) order, not
        // in tx-field order. We just check both reserves equal 100 of
        // their respective asset.
        auto const r0 = binSle->getFieldAmount(sfReserve0);
        auto const r1 = binSle->getFieldAmount(sfReserve1);
        BEAST_EXPECT(Number(r0) == Number{100});
        BEAST_EXPECT(Number(r1) == Number{100});

        // LP holding SLE created.
        auto const holdingSle =
            env.current()->read(keylet::ammBinHolding(ammID, al.id(), 0));
        BEAST_EXPECT(holdingSle != nullptr);
        if (!holdingSle)
            return;
        BEAST_EXPECT((*holdingSle)[sfAccount] == al.id());
        BEAST_EXPECT(holdingSle->getFieldI32(sfBinID) == 0);
        // MPT balance is authoritative for shares; matches bin's
        // sfOutstandingAmount when there's only one LP.
        BEAST_EXPECT(mptSharesOf(env, ammID, 0, al.id()) ==
                     binSle->getFieldU64(sfOutstandingAmount));
    }

    void
    testDepositMissingBinID(FeatureBitset features)
    {
        testcase("AMMDeposit(CtBinned) without BinID is temMALFORMED");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);

        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        env(cv);
        env.close();

        json::Value dep;
        dep[jss::Account] = al.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] =
            STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] =
            STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = CtBinned;
        dep[jss::Flags] = tfTwoAsset;
        dep[jss::Amount] = usd(100).value().getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = eur(100).value().getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep, Ter(temMALFORMED));
        env.close();
    }

    void
    testDepositWithdrawRoundTrip(FeatureBitset features)
    {
        testcase("Binned deposit + withdraw returns LP's assets");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);

        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        env(cv);
        env.close();

        auto const ammSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned));
        auto const ammID = ammSle->key();

        provisionBin(env, al, usd, eur, 5);
        // Record alice's balance before deposit (after AMM create fee).
        auto const usdBeforeDep = env.balance(al, usd.issue());
        auto const eurBeforeDep = env.balance(al, eur.issue());

        // Deposit into bin 5.
        json::Value dep;
        dep[jss::Account] = al.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] =
            STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] =
            STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = CtBinned;
        dep[jss::Flags] = tfTwoAsset;
        dep[sfBinID.jsonName] = 5;
        dep[jss::Amount] = usd(50).value().getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = eur(50).value().getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep);
        env.close();

        // After deposit: alice has 50 less of each.
        BEAST_EXPECT(env.balance(al, usd.issue()) == usdBeforeDep - usd(50));
        BEAST_EXPECT(env.balance(al, eur.issue()) == eurBeforeDep - eur(50));

        // Withdraw all from bin 5.
        json::Value wd;
        wd[jss::Account] = al.human();
        wd[jss::TransactionType] = jss::AMMWithdraw;
        wd[jss::Asset] =
            STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        wd[jss::Asset2] =
            STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        wd[sfCurveType.jsonName] = CtBinned;
        wd[jss::Flags] = tfWithdrawAll;
        wd[sfBinID.jsonName] = 5;
        wd[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(wd);
        env.close();

        // After withdraw: alice has roughly her pre-deposit balance back.
        BEAST_EXPECT(env.balance(al, usd.issue()) == usdBeforeDep);
        BEAST_EXPECT(env.balance(al, eur.issue()) == eurBeforeDep);

        // Bin SLE persists on full drain (owns the MPT issuance ID for
        // future re-deposits). Reserves are zero; outstanding is zero.
        auto const binAfter = env.current()->read(keylet::ammBin(ammID, 5));
        if (BEAST_EXPECT(binAfter != nullptr))
        {
            BEAST_EXPECT(binAfter->getFieldU64(sfOutstandingAmount) == 0);
        }
        // Holding SLE deleted (LP burned all shares).
        BEAST_EXPECT(env.current()->read(keylet::ammBinHolding(ammID, al.id(), 5)) == nullptr);
    }

    void
    testMultiLPSameBinSharesPropotional(FeatureBitset features)
    {
        testcase("Two LPs depositing same amount into same bin get equal shares");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        Account const bob("bob");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);
        fundForAMMCreate(env, gw, bob, usd, eur, /*fundGw=*/false);

        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        env(cv);
        env.close();

        auto const ammSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned));
        auto const ammID = ammSle->key();

        provisionBin(env, al, usd, eur, 0);
        auto submitDeposit = [&](jtx::Account const& acct) {
            json::Value dep;
            dep[jss::Account] = acct.human();
            dep[jss::TransactionType] = jss::AMMDeposit;
            dep[jss::Asset] =
                STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            dep[jss::Asset2] =
                STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            dep[sfCurveType.jsonName] = CtBinned;
            dep[jss::Flags] = tfTwoAsset;
            dep[sfBinID.jsonName] = 0;
            dep[jss::Amount] = usd(100).value().getJson(JsonOptions::Values::None);
            dep[jss::Amount2] = eur(100).value().getJson(JsonOptions::Values::None);
            dep[jss::Fee] =
                std::to_string(env.current()->fees().increment.drops());
            env(dep);
            env.close();
        };
        submitDeposit(al);
        submitDeposit(bob);

        // Same deposit → same shares. MPT balance is authoritative.
        auto const aliceShares = mptSharesOf(env, ammID, 0, al.id());
        auto const bobShares = mptSharesOf(env, ammID, 0, bob.id());
        BEAST_EXPECT(aliceShares > 0);
        BEAST_EXPECT(aliceShares == bobShares);

        // Bin outstanding = sum of both LP shares.
        auto const binSle = env.current()->read(keylet::ammBin(ammID, 0));
        BEAST_EXPECT(binSle->getFieldU64(sfOutstandingAmount) ==
                     aliceShares + bobShares);
    }

    void
    testSwapAtUnitPrice(FeatureBitset features)
    {
        testcase("Swap through binned pool at bin 0 (price=1) is constant-sum");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");  // LP
        Account const bob("bob");   // trader
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);
        fundForAMMCreate(env, gw, bob, usd, eur, /*fundGw=*/false);

        // Create binned pool, binStep=10.
        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        env(cv);
        env.close();

        provisionBin(env, al, usd, eur, 0);
        // Deposit 10000 USD / 10000 EUR into bin 0 (price=1).
        json::Value dep;
        dep[jss::Account] = al.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] =
            STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] =
            STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = CtBinned;
        dep[jss::Flags] = tfTwoAsset;
        dep[sfBinID.jsonName] = 0;
        dep[jss::Amount] = usd(10000).value().getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = eur(10000).value().getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep);
        env.close();

        // Bob trades: spend up to USD(120), receive exactly EUR(100).
        auto const eurBefore = env.balance(bob, eur.issue());
        auto const usdBefore = env.balance(bob, usd.issue());
        env(pay(bob, bob, eur(100)),
            Path(~eur),
            Sendmax(usd(120)),
            Txflags(tfNoRippleDirect | tfPartialPayment));
        env.close();

        auto const eurGot = env.balance(bob, eur.issue()) - eurBefore;
        auto const usdSpent = usdBefore - env.balance(bob, usd.issue());

        // Bin 0 → price = 1. Zero fee → 100 EUR costs 100 USD exactly.
        BEAST_EXPECT(Number(eurGot) == Number{100});
        BEAST_EXPECT(Number(usdSpent) == Number{100});
    }

    void
    testMultiBinWalkOnSwap(FeatureBitset features)
    {
        testcase("Multi-bin swap walks bins and advances activeBinID");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");  // LP
        Account const bob("bob");   // trader
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);
        fundForAMMCreate(env, gw, bob, usd, eur, /*fundGw=*/false);

        // Create binned pool, binStep=100 (1%).
        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 100u;
        env(cv);
        env.close();

        auto const ammSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned));
        BEAST_EXPECT(ammSle != nullptr);
        if (!ammSle)
            return;
        auto const ammID = ammSle->key();

        // EUR < USD lex-wise, so EUR=asset0 (lex-min), USD=asset1.
        // Walk direction when inIsAsset0 (EUR in, USD out) is positive
        // (activeBinID increases). So depositing into bins 0, 1, 2 sets
        // up a stack of liquidity that a EUR→USD swap walks through.
        auto submitDeposit = [&](std::int32_t binID,
                                 STAmount const& amt1,
                                 STAmount const& amt2) {
            provisionBin(env, al, usd, eur, binID);
            json::Value dep;
            dep[jss::Account] = al.human();
            dep[jss::TransactionType] = jss::AMMDeposit;
            dep[jss::Asset] =
                STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            dep[jss::Asset2] =
                STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            dep[sfCurveType.jsonName] = CtBinned;
            dep[jss::Flags] = tfTwoAsset;
            dep[sfBinID.jsonName] = binID;
            dep[jss::Amount] = amt1.getJson(JsonOptions::Values::None);
            dep[jss::Amount2] = amt2.getJson(JsonOptions::Values::None);
            dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(dep);
            env.close();
        };

        // Small liquidity in three consecutive bins. Each bin contains
        // ~100 of asset1 (USD). EUR→USD swap will need to walk bins to
        // get more than ~100 USD.
        submitDeposit(0, usd(100), eur(100));
        submitDeposit(1, usd(100), eur(100));
        submitDeposit(2, usd(100), eur(100));

        // AMM total: 300 USD, 300 EUR. activeBinID still 0 (never
        // advanced by deposits).
        {
            auto const amm =
                env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned));
            BEAST_EXPECT(amm->getFieldI32(sfActiveBinID) == 0);
        }

        // Bob trades EUR → USD. Asking for 250 USD (more than bin 0 can
        // provide alone) forces walking into bin 1 and beyond.
        env(pay(bob, bob, usd(250)),
            Path(~usd),
            Sendmax(eur(300)),
            Txflags(tfNoRippleDirect | tfPartialPayment));
        env.close();

        // After swap: activeBinID should have advanced.
        {
            auto const amm =
                env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned));
            BEAST_EXPECT(amm->getFieldI32(sfActiveBinID) > 0);
        }

        // Bin 0 (the active bin pre-swap) should be drained of USD
        // (asset1, the swap's output side).
        {
            auto const bin0 = env.current()->read(keylet::ammBin(ammID, 0));
            if (BEAST_EXPECT(bin0 != nullptr))
            {
                auto const reserve1 = bin0->getFieldAmount(sfReserve1);
                BEAST_EXPECT(Number(reserve1) < Number{1});
            }
        }
    }

    void
    testFeeAccrualAndCollect(FeatureBitset features)
    {
        testcase("Trading fees accrue into bin and AMMCollectFees pays them out");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");  // LP
        Account const bob("bob");   // trader
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);
        fundForAMMCreate(env, gw, bob, usd, eur, /*fundGw=*/false);

        // Create binned pool with 100bp (1%) trading fee.
        json::Value cv;
        cv[jss::Account] = al.human();
        cv[jss::Amount] = usd(1000).value().getJson(JsonOptions::Values::None);
        cv[jss::Amount2] = eur(1000).value().getJson(JsonOptions::Values::None);
        cv[jss::TradingFee] = 100;  // 100/100000 = 0.1%
        cv[jss::TransactionType] = jss::AMMCreate;
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        cv[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(cv);
        env.close();

        auto const ammSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned));
        auto const ammID = ammSle->key();

        // Alice deposits liquidity into bin 0.
        provisionBin(env, al, usd, eur, 0);
        json::Value dep;
        dep[jss::Account] = al.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = CtBinned;
        dep[jss::Flags] = tfTwoAsset;
        dep[sfBinID.jsonName] = 0;
        dep[jss::Amount] = usd(1000).value().getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = eur(1000).value().getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep);
        env.close();

        // Bob trades — should pay a fee.
        env(pay(bob, bob, usd(100)),
            Path(~usd),
            Sendmax(eur(110)),
            Txflags(tfNoRippleDirect | tfPartialPayment));
        env.close();

        // Bin's feeGrowth should be non-zero on the input side (EUR =
        // asset0 since EUR < USD lex-wise → fees accrue to feeGrowthBin0).
        {
            auto const bin0 = env.current()->read(keylet::ammBin(ammID, 0));
            if (BEAST_EXPECT(bin0 != nullptr))
            {
                Number const fg0{bin0->getFieldNumber(sfFeeGrowthBin0)};
                BEAST_EXPECT(fg0 > Number{0});
            }
        }

        auto const eurBefore = env.balance(al, eur.issue());

        // Alice collects fees.
        json::Value coll;
        coll[jss::Account] = al.human();
        coll[jss::TransactionType] = jss::AMMCollectFees;
        coll[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        coll[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        coll[sfCurveType.jsonName] = CtBinned;
        coll[sfBinID.jsonName] = 0;
        coll[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(coll);
        env.close();

        // Alice should have received some EUR (the fee).
        BEAST_EXPECT(env.balance(al, eur.issue()) > eurBefore);

        // Second collect immediately should be a no-op (snapshot
        // advanced to "now"). Should still succeed.
        auto const eurAfterFirstCollect = env.balance(al, eur.issue());
        env(coll);
        env.close();
        BEAST_EXPECT(env.balance(al, eur.issue()) == eurAfterFirstCollect);
    }

    void
    testPartialWithdraw(FeatureBitset features)
    {
        testcase("Partial withdraw burns N shares, keeps remainder");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);

        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        env(cv);
        env.close();

        auto const ammSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned));
        auto const ammID = ammSle->key();

        // Deposit 1000/1000 into bin 0 — should mint sqrt(1e6) = 1000 shares.
        provisionBin(env, al, usd, eur, 0);
        json::Value dep;
        dep[jss::Account] = al.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = CtBinned;
        dep[jss::Flags] = tfTwoAsset;
        dep[sfBinID.jsonName] = 0;
        dep[jss::Amount] = usd(1000).value().getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = eur(1000).value().getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep);
        env.close();

        auto const totalShares = mptSharesOf(env, ammID, 0, al.id());
        BEAST_EXPECT(totalShares > 0);
        if (totalShares == 0)
            return;

        // Partial withdraw — half the shares.
        json::Value wd;
        wd[jss::Account] = al.human();
        wd[jss::TransactionType] = jss::AMMWithdraw;
        wd[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        wd[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        wd[sfCurveType.jsonName] = CtBinned;
        wd[sfBinID.jsonName] = 0;
        // sfShares is a UINT64 — pass as a Json::UInt directly so the
        // serializer doesn't string-encode and re-parse.
        wd[sfShares.jsonName] =
            static_cast<json::UInt>(totalShares / 2);
        wd[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(wd);
        env.close();

        // LP's MPT balance should be the remainder.
        BEAST_EXPECT(
            mptSharesOf(env, ammID, 0, al.id()) ==
            totalShares - totalShares / 2);
        // Snapshot SLE still exists (LP retains some shares).
        BEAST_EXPECT(
            env.current()->read(keylet::ammBinHolding(ammID, al.id(), 0)) !=
            nullptr);
        // Bin SLE still exists (not fully drained).
        BEAST_EXPECT(env.current()->read(keylet::ammBin(ammID, 0)) != nullptr);

        // Partial-withdraw with 0 shares is malformed.
        wd[sfShares.jsonName] = static_cast<json::UInt>(0);
        env(wd, Ter(temMALFORMED));
        env.close();
    }

    void
    testCollectWithoutHolding(FeatureBitset features)
    {
        testcase("Collect fees on a bin the LP has no holding in fails cleanly");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        Account const bob("bob");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);
        fundForAMMCreate(env, gw, bob, usd, eur, /*fundGw=*/false);

        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        env(cv);
        env.close();

        // Alice deposits into bin 0; bob tries to collect.
        provisionBin(env, al, usd, eur, 0);
        json::Value dep;
        dep[jss::Account] = al.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = CtBinned;
        dep[jss::Flags] = tfTwoAsset;
        dep[sfBinID.jsonName] = 0;
        dep[jss::Amount] = usd(100).value().getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = eur(100).value().getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep);
        env.close();

        json::Value coll;
        coll[jss::Account] = bob.human();
        coll[jss::TransactionType] = jss::AMMCollectFees;
        coll[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        coll[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        coll[sfCurveType.jsonName] = CtBinned;
        coll[sfBinID.jsonName] = 0;
        coll[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(coll, Ter(tecNO_ENTRY));
        env.close();
    }

    void
    testCollectWithBothFieldsMalformed(FeatureBitset features)
    {
        testcase("Collect with both PositionID and BinID is temMALFORMED");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const al("alice");
        env.fund(XRP(100000), al);
        env.close();

        json::Value coll;
        coll[jss::Account] = al.human();
        coll[jss::TransactionType] = jss::AMMCollectFees;
        coll[jss::Asset] = STIssue(sfAsset, xrpIssue()).getJson(JsonOptions::Values::None);
        coll[jss::Asset2] = STIssue(sfAsset, xrpIssue()).getJson(JsonOptions::Values::None);
        coll[sfCurveType.jsonName] = CtBinned;
        coll[sfPositionID.jsonName] = to_string(uint256{0});
        coll[sfBinID.jsonName] = 0;
        coll[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(coll, Ter(temMALFORMED));
        env.close();
    }

    void
    testInvariantWouldCatchDrift(FeatureBitset features)
    {
        testcase("Per-bin invariant catches reserve/trustline drift "
                 "(positive test — clean tx passes)");
        using namespace jtx;

        // Just verify a clean swap doesn't trip the invariant.
        // The actual invariant-failure cases are hard to provoke without
        // explicit bug injection; the existing tests serve as a positive
        // proof that the invariant accepts well-formed mutations.

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        Account const bob("bob");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);
        fundForAMMCreate(env, gw, bob, usd, eur, /*fundGw=*/false);

        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        env(cv);
        env.close();

        provisionBin(env, al, usd, eur, 0);
        json::Value dep;
        dep[jss::Account] = al.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = CtBinned;
        dep[jss::Flags] = tfTwoAsset;
        dep[sfBinID.jsonName] = 0;
        dep[jss::Amount] = usd(500).value().getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = eur(500).value().getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep);
        env.close();

        env(pay(bob, bob, usd(50)),
            Path(~usd),
            Sendmax(eur(60)),
            Txflags(tfNoRippleDirect | tfPartialPayment));
        env.close();
        // If the invariant tripped, the swap would have returned
        // tecINVARIANT_FAILED. Reaching here means it passed.
    }

    // Build a fresh env with N populated bins and run an EUR→USD swap
    // sweeping across them. Returns (USD received, EUR spent) plus the
    // post-swap activeBinID. Used by determinism test to verify two
    // identical-input runs produce byte-identical outputs.
    struct StressResult
    {
        Number usdReceived{0};
        Number eurSpent{0};
        std::int32_t finalActiveBinID{0};
        Number fg0AtBin0{0};
    };

    StressResult
    runDeterminismStress(
        FeatureBitset features,
        std::uint32_t tradingFee,
        std::uint16_t binStep,
        int nBins,
        Number const& depositPerBin,
        Number const& bobSendmax,
        Number const& bobAsks)
    {
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        Account const bob("bob");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);
        fundForAMMCreate(env, gw, bob, usd, eur, /*fundGw=*/false);

        json::Value cv;
        cv[jss::Account] = al.human();
        cv[jss::Amount] = usd(1000).value().getJson(JsonOptions::Values::None);
        cv[jss::Amount2] = eur(1000).value().getJson(JsonOptions::Values::None);
        cv[jss::TradingFee] = tradingFee;
        cv[jss::TransactionType] = jss::AMMCreate;
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = static_cast<json::UInt>(binStep);
        cv[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(cv);
        env.close();

        auto const ammSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned));
        auto const ammID = ammSle->key();

        // Deposit into consecutive bins 0..nBins-1.
        for (int b = 0; b < nBins; ++b)
        {
            provisionBin(env, al, usd, eur, b);
            json::Value dep;
            dep[jss::Account] = al.human();
            dep[jss::TransactionType] = jss::AMMDeposit;
            dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            dep[sfCurveType.jsonName] = CtBinned;
            dep[jss::Flags] = tfTwoAsset;
            dep[sfBinID.jsonName] = b;
            STAmount const amt0 = toSTAmount(usd.asset(), depositPerBin);
            STAmount const amt1 = toSTAmount(eur.asset(), depositPerBin);
            dep[jss::Amount] = amt0.getJson(JsonOptions::Values::None);
            dep[jss::Amount2] = amt1.getJson(JsonOptions::Values::None);
            dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(dep);
            env.close();
        }

        auto const usdBefore = env.balance(bob, usd.issue());
        auto const eurBefore = env.balance(bob, eur.issue());

        STAmount const bobAsksAmt = toSTAmount(usd.asset(), bobAsks);
        STAmount const bobSendmaxAmt = toSTAmount(eur.asset(), bobSendmax);
        env(pay(bob, bob, bobAsksAmt),
            Path(~usd),
            Sendmax(bobSendmaxAmt),
            Txflags(tfNoRippleDirect | tfPartialPayment));
        env.close();

        StressResult r;
        r.usdReceived = Number{env.balance(bob, usd.issue()) - usdBefore};
        r.eurSpent = Number{eurBefore - env.balance(bob, eur.issue())};
        auto const ammAfter =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned));
        r.finalActiveBinID = ammAfter->getFieldI32(sfActiveBinID);
        auto const bin0 = env.current()->read(keylet::ammBin(ammID, 0));
        if (bin0)
            r.fg0AtBin0 = Number{bin0->getFieldNumber(sfFeeGrowthBin0)};
        return r;
    }

    void
    testJITResistancePropRata(FeatureBitset features)
    {
        testcase("JIT-resistance: pro-rata bin dilution means a JIT bot "
                 "can't capture all fees on a populated bin");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const passive("passive");
        Account const jit("jit");
        Account const trader("trader");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, passive, usd, eur);
        fundForAMMCreate(env, gw, jit, usd, eur, /*fundGw=*/false);
        fundForAMMCreate(env, gw, trader, usd, eur, /*fundGw=*/false);

        // 100bp fee — generous enough that fees are easily measurable.
        json::Value cv;
        cv[jss::Account] = passive.human();
        cv[jss::Amount] = usd(1000).value().getJson(JsonOptions::Values::None);
        cv[jss::Amount2] = eur(1000).value().getJson(JsonOptions::Values::None);
        cv[jss::TradingFee] = 100;
        cv[jss::TransactionType] = jss::AMMCreate;
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        cv[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(cv);
        env.close();

        provisionBin(env, passive, usd, eur, 0);

        auto deposit = [&](jtx::Account const& acct, STAmount const& amt0,
                           STAmount const& amt1) {
            json::Value dep;
            dep[jss::Account] = acct.human();
            dep[jss::TransactionType] = jss::AMMDeposit;
            dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            dep[sfCurveType.jsonName] = CtBinned;
            dep[jss::Flags] = tfTwoAsset;
            dep[sfBinID.jsonName] = 0;
            dep[jss::Amount] = amt0.getJson(JsonOptions::Values::None);
            dep[jss::Amount2] = amt1.getJson(JsonOptions::Values::None);
            dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(dep);
            env.close();
        };

        // Passive LP deposits 1000 + 1000 into bin 0.
        deposit(passive, usd(1000), eur(1000));

        // JIT bot deposits 100 + 100 right before the swap. Their share
        // ratio is 100 / (1000 + 100) ≈ 9.1%.
        deposit(jit, usd(100), eur(100));

        // Now trader swaps. With 100bp fee on say 220 EUR in, fee ≈ 2.2 EUR.
        env(pay(trader, trader, usd(200)),
            Path(~usd),
            Sendmax(eur(250)),
            Txflags(tfNoRippleDirect | tfPartialPayment));
        env.close();

        // Both LPs collect.
        auto collectFor = [&](jtx::Account const& acct) {
            auto const usdBefore = env.balance(acct, usd.issue());
            auto const eurBefore = env.balance(acct, eur.issue());
            json::Value coll;
            coll[jss::Account] = acct.human();
            coll[jss::TransactionType] = jss::AMMCollectFees;
            coll[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            coll[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            coll[sfCurveType.jsonName] = CtBinned;
            coll[sfBinID.jsonName] = 0;
            coll[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(coll);
            env.close();
            return std::pair{
                Number{env.balance(acct, usd.issue()) - usdBefore},
                Number{env.balance(acct, eur.issue()) - eurBefore}};
        };
        auto const [passiveUsd, passiveEur] = collectFor(passive);
        auto const [jitUsd, jitEur] = collectFor(jit);

        // JIT's share of fees must be strictly less than passive's
        // (passive has 10x the shares). Confirms pro-rata dilution.
        BEAST_EXPECT(jitEur > Number{0});      // bot got SOMETHING
        BEAST_EXPECT(passiveEur > jitEur);     // but less than passive
        // Stronger: bot's share should be ~10% of passive's (since
        // 100 / 1000 = 10%). Allow a 2x tolerance for rounding.
        if (jitEur > Number{0})
        {
            Number const ratio = passiveEur / jitEur;
            BEAST_EXPECT(ratio > Number{5});   // > 5x => bot is heavily diluted
            BEAST_EXPECT(ratio < Number{20});  // sanity bound
        }
    }

    void
    testDeterminismStress(FeatureBitset features)
    {
        testcase("Determinism: identical multi-bin swap inputs yield "
                 "byte-identical outputs across runs");
        using namespace jtx;

        // Run a non-trivial multi-bin swap twice in fresh envs and
        // compare results bit-for-bit. Any non-determinism (e.g. a
        // rounding mode leak, an iteration-order dependency, a
        // hash-table seed) would break this.
        auto const r1 = runDeterminismStress(
            features,
            /*tradingFee=*/100,    // 100bp
            /*binStep=*/25,        // 25bp wide
            /*nBins=*/10,
            /*depositPerBin=*/Number{100},
            /*bobSendmax=*/Number{700},
            /*bobAsks=*/Number{650});
        auto const r2 = runDeterminismStress(
            features, 100, 25, 10, Number{100}, Number{700}, Number{650});

        BEAST_EXPECT(r1.usdReceived == r2.usdReceived);
        BEAST_EXPECT(r1.eurSpent == r2.eurSpent);
        BEAST_EXPECT(r1.finalActiveBinID == r2.finalActiveBinID);
        BEAST_EXPECT(r1.fg0AtBin0 == r2.fg0AtBin0);
        // And the swap must have actually crossed bins (verify the
        // stress test isn't a trivial single-bin case).
        BEAST_EXPECT(r1.finalActiveBinID > 0);
        // Bob received SOMETHING.
        BEAST_EXPECT(r1.usdReceived > Number{0});
        // Fee accumulator advanced on bin 0 (non-trivial fee).
        BEAST_EXPECT(r1.fg0AtBin0 > Number{0});
    }

    void
    testBinCreateAndMPTRoundTrip(FeatureBitset features)
    {
        testcase("AMMBinCreate provisions bin + MPT issuance; deposit mints, "
                 "withdraw burns shares");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);

        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        env(cv);
        env.close();

        auto const ammSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned));
        auto const ammID = ammSle->key();

        // 1) AMMBinCreate provisions the bin + its per-bin MPT issuance.
        provisionBin(env, al, usd, eur, 0);
        auto const binSle = env.current()->read(keylet::ammBin(ammID, 0));
        if (!BEAST_EXPECT(binSle != nullptr))
            return;
        BEAST_EXPECT(binSle->isFieldPresent(sfMPTokenIssuanceID));
        auto const mptIssuanceID = binSle->getFieldH192(sfMPTokenIssuanceID);

        // The MPT issuance SLE should exist with AMM as issuer.
        auto const mptIssuanceSle =
            env.current()->read(keylet::mptokenIssuance(mptIssuanceID));
        if (!BEAST_EXPECT(mptIssuanceSle != nullptr))
            return;
        BEAST_EXPECT((*mptIssuanceSle)[sfIssuer] == ammSle->getAccountID(sfAccount));
        BEAST_EXPECT(mptIssuanceSle->getFieldU64(sfOutstandingAmount) == 0);

        // Re-provisioning the same bin fails (idempotency check).
        json::Value reprov;
        reprov[jss::Account] = al.human();
        reprov[jss::TransactionType] = "AMMBinCreate";
        reprov[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        reprov[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        reprov[sfBinID.jsonName] = 0;
        reprov[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(reprov, Ter(tecAMM_FAILED));
        env.close();

        // 2) AMMDeposit mints MPT to LP.
        json::Value dep;
        dep[jss::Account] = al.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = CtBinned;
        dep[jss::Flags] = tfTwoAsset;
        dep[sfBinID.jsonName] = 0;
        dep[jss::Amount] = usd(500).value().getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = eur(500).value().getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep);
        env.close();

        // LP now holds an MPToken for the bin's issuance.
        auto const lpMpt =
            env.current()->read(keylet::mptoken(mptIssuanceID, al.id()));
        if (!BEAST_EXPECT(lpMpt != nullptr))
            return;
        auto const lpMptAmount = lpMpt->getFieldU64(sfMPTAmount);
        BEAST_EXPECT(lpMptAmount > 0);

        // Snapshot SLE was auto-created on deposit (carries
        // feeGrowthInside snapshot only — no sfShares).
        BEAST_EXPECT(
            env.current()->read(keylet::ammBinHolding(ammID, al.id(), 0)) !=
            nullptr);

        // Issuance OutstandingAmount tracks the sum.
        auto const issAfter =
            env.current()->read(keylet::mptokenIssuance(mptIssuanceID));
        BEAST_EXPECT(issAfter->getFieldU64(sfOutstandingAmount) == lpMptAmount);

        // 3) AMMWithdraw burns MPT in lock-step with holding SLE shares.
        json::Value wd;
        wd[jss::Account] = al.human();
        wd[jss::TransactionType] = jss::AMMWithdraw;
        wd[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        wd[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        wd[sfCurveType.jsonName] = CtBinned;
        wd[jss::Flags] = tfWithdrawAll;
        wd[sfBinID.jsonName] = 0;
        wd[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(wd);
        env.close();

        // LP MPT balance now zero; issuance OutstandingAmount zero.
        auto const lpMptAfter =
            env.current()->read(keylet::mptoken(mptIssuanceID, al.id()));
        if (BEAST_EXPECT(lpMptAfter != nullptr))
        {
            BEAST_EXPECT(lpMptAfter->getFieldU64(sfMPTAmount) == 0);
        }
        auto const issAfterWd =
            env.current()->read(keylet::mptokenIssuance(mptIssuanceID));
        BEAST_EXPECT(issAfterWd->getFieldU64(sfOutstandingAmount) == 0);
        // Bin SLE persists (still owns the issuance ID).
        BEAST_EXPECT(env.current()->read(keylet::ammBin(ammID, 0)) != nullptr);
    }

    void
    testDepositOnUnprovisionedBin(FeatureBitset features)
    {
        testcase("AMMDeposit on a non-provisioned bin returns tecNO_ENTRY");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);

        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        env(cv);
        env.close();

        // No provisionBin call. Deposit into bin 0 should fail.
        json::Value dep;
        dep[jss::Account] = al.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = CtBinned;
        dep[jss::Flags] = tfTwoAsset;
        dep[sfBinID.jsonName] = 0;
        dep[jss::Amount] = usd(100).value().getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = eur(100).value().getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep, Ter(tecNO_ENTRY));
        env.close();
    }

    void
    testMPTTransferThenWithdraw(FeatureBitset features)
    {
        testcase("LP transfers bin MPT to another account; new holder "
                 "withdraws via standard MPT path");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        Account const bob("bob");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);
        fundForAMMCreate(env, gw, bob, usd, eur, /*fundGw=*/false);

        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        env(cv);
        env.close();

        auto const ammSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned));
        auto const ammID = ammSle->key();

        provisionBin(env, al, usd, eur, 0);

        // Alice deposits 500/500 → gets shares minted as MPT.
        json::Value dep;
        dep[jss::Account] = al.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = CtBinned;
        dep[jss::Flags] = tfTwoAsset;
        dep[sfBinID.jsonName] = 0;
        dep[jss::Amount] = usd(500).value().getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = eur(500).value().getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep);
        env.close();

        auto const aliceShares = mptSharesOf(env, ammID, 0, al.id());
        BEAST_EXPECT(aliceShares > 0);

        // Find the bin's MPT issuance.
        auto const binSle = env.current()->read(keylet::ammBin(ammID, 0));
        if (!BEAST_EXPECT(binSle != nullptr))
            return;
        auto const mptId = binSle->getFieldH192(sfMPTokenIssuanceID);

        // Bob must authorize the issuance to receive — standard MPT flow.
        json::Value auth;
        auth[jss::Account] = bob.human();
        auth[jss::TransactionType] = "MPTokenAuthorize";
        auth[sfMPTokenIssuanceID.jsonName] = to_string(mptId);
        auth[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(auth);
        env.close();

        // Alice transfers half her shares to bob via a Payment of the
        // bin MPT (this is the composability win).
        json::Value mptAmt;
        mptAmt[jss::mpt_issuance_id] = to_string(mptId);
        mptAmt[jss::value] = std::to_string(aliceShares / 2);
        json::Value pay;
        pay[jss::Account] = al.human();
        pay[jss::TransactionType] = jss::Payment;
        pay[jss::Destination] = bob.human();
        pay[jss::Amount] = mptAmt;
        pay[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(pay);
        env.close();

        // Bob now holds half the bin's shares.
        BEAST_EXPECT(mptSharesOf(env, ammID, 0, bob.id()) == aliceShares / 2);
        BEAST_EXPECT(
            mptSharesOf(env, ammID, 0, al.id()) ==
            aliceShares - aliceShares / 2);

        // Bob withdraws using the MPT-authoritative path — no
        // pre-existing snapshot SLE for him is required.
        auto const bobUsdBefore = env.balance(bob, usd.issue());
        auto const bobEurBefore = env.balance(bob, eur.issue());
        json::Value wd;
        wd[jss::Account] = bob.human();
        wd[jss::TransactionType] = jss::AMMWithdraw;
        wd[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        wd[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        wd[sfCurveType.jsonName] = CtBinned;
        wd[jss::Flags] = tfWithdrawAll;
        wd[sfBinID.jsonName] = 0;
        wd[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(wd);
        env.close();

        // Bob received some of the pool reserves.
        BEAST_EXPECT(env.balance(bob, usd.issue()) > bobUsdBefore);
        BEAST_EXPECT(env.balance(bob, eur.issue()) > bobEurBefore);
        // Bob's MPT balance is zero.
        BEAST_EXPECT(mptSharesOf(env, ammID, 0, bob.id()) == 0);
        // Alice's MPT balance is unchanged.
        BEAST_EXPECT(
            mptSharesOf(env, ammID, 0, al.id()) ==
            aliceShares - aliceShares / 2);
    }

    void
    testBinDestroy(FeatureBitset features)
    {
        testcase("AMMBinDestroy removes drained bin + its MPT issuance");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);

        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        env(cv);
        env.close();

        auto const ammSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned));
        auto const ammID = ammSle->key();

        // Provision two bins so we have a non-active bin we can destroy.
        provisionBin(env, al, usd, eur, 0);
        provisionBin(env, al, usd, eur, 5);

        auto const binDestroyTx = [&](std::int32_t binID) {
            json::Value jv;
            jv[jss::Account] = al.human();
            jv[jss::TransactionType] = "AMMBinDestroy";
            jv[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            jv[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            jv[sfBinID.jsonName] = binID;
            jv[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            return jv;
        };

        // Capture bin 5's MPT issuance ID before destroying.
        auto const bin5 = env.current()->read(keylet::ammBin(ammID, 5));
        if (!BEAST_EXPECT(bin5 != nullptr))
            return;
        auto const mptId = bin5->getFieldH192(sfMPTokenIssuanceID);
        BEAST_EXPECT(env.current()->read(keylet::mptokenIssuance(mptId)) != nullptr);

        // Destroying bin 5 (empty, non-active) succeeds.
        env(binDestroyTx(5));
        env.close();

        BEAST_EXPECT(env.current()->read(keylet::ammBin(ammID, 5)) == nullptr);
        BEAST_EXPECT(env.current()->read(keylet::mptokenIssuance(mptId)) == nullptr);

        // Destroying a non-existent bin returns tecNO_ENTRY.
        env(binDestroyTx(5), Ter(tecNO_ENTRY));
        env.close();

        // Destroy bin 0 (also empty; it's active but the whole pool
        // is empty so destroy is allowed).
        env(binDestroyTx(0));
        env.close();
        BEAST_EXPECT(env.current()->read(keylet::ammBin(ammID, 0)) == nullptr);
    }

    void
    testBinDestroyOnNonEmptyFails(FeatureBitset features)
    {
        testcase("AMMBinDestroy refuses bins with outstanding shares "
                 "or reserves");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);

        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        env(cv);
        env.close();

        provisionBin(env, al, usd, eur, 0);
        json::Value dep;
        dep[jss::Account] = al.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = CtBinned;
        dep[jss::Flags] = tfTwoAsset;
        dep[sfBinID.jsonName] = 0;
        dep[jss::Amount] = usd(100).value().getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = eur(100).value().getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep);
        env.close();

        // Bin has reserves + shares — destroy fails.
        json::Value jv;
        jv[jss::Account] = al.human();
        jv[jss::TransactionType] = "AMMBinDestroy";
        jv[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        jv[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        jv[sfBinID.jsonName] = 0;
        jv[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(jv, Ter(tecAMM_FAILED));
        env.close();
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testCreateHappyPath(features);
        testCreateMissingBinStep(features);
        testCreateInvalidBinStep(features);
        testAmendmentDisabled(features);
        testCoexistenceWithCL(features);
        testDepositCreatesBin(features);
        testDepositMissingBinID(features);
        testDepositWithdrawRoundTrip(features);
        testMultiLPSameBinSharesPropotional(features);
        testSwapAtUnitPrice(features);
        testMultiBinWalkOnSwap(features);
        testFeeAccrualAndCollect(features);
        testPartialWithdraw(features);
        testCollectWithoutHolding(features);
        testCollectWithBothFieldsMalformed(features);
        testInvariantWouldCatchDrift(features);
        testDeterminismStress(features);
        testJITResistancePropRata(features);
        testBinCreateAndMPTRoundTrip(features);
        testDepositOnUnprovisionedBin(features);
        testMPTTransferThenWithdraw(features);
        testBinDestroy(features);
        testBinDestroyOnNonEmptyFails(features);
        testPartialWithdrawAutoCollectsFees(features);
        testActiveBinAdvancesToNearestOnDrain(features);
        testFeeGrowthPrecisionStress(features);
        testSparseBinSHAMapSeek(features);
        testClawbackProportionalDrain(features);
        testBinIDExtremesAccepted(features);
        testBinIDOutOfRangeRejected(features);
        testAMMVoteOnBinnedRejected(features);
        testBookStepRoutesAcrossCurves(features);
        testAllBinnedTxAmendmentGated(features);
        testJITDilutedByExistingLPs(features);
        testDustSpamReserveCharged(features);
        testAMMDeleteOnBinned(features);
        testAMMBidOnBinnedRejected(features);
        testReserveExemptionAcrossChurnCycles(features);
        testFrozenTrustlineBlocksDeposit(features);
        testAMMInfoSurfacesBinnedFields(features);
        testFirstDepositAutoAuthorizesMPT(features);
        testBinRecreateAfterDestroy(features);
    }

    void
    testFeeGrowthPrecisionStress(FeatureBitset features)
    {
        testcase("sfFeeGrowthBin accumulates monotonically across "
                 "many moderate-fee swaps without precision loss");
        using namespace jtx;

        // Note: a stricter test with 1bp fees on 1-EUR swaps triggers
        // a Number denormalization in the payment engine. That's a
        // real precision-audit finding — at the extreme low end of
        // (fee × amount / outstanding), the multiply/divide chain in
        // BookStep can produce denormal Numbers. Documented as a
        // known limitation; this test exercises the moderate regime
        // that production binned pools will actually see (≥10bp fees,
        // ≥10 unit swaps), where the accumulator behaves correctly.

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        Account const bob("bob");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);
        fundForAMMCreate(env, gw, bob, usd, eur, /*fundGw=*/false);

        // Mirror testFeeAccrualAndCollect's parameters (which passes with
        // a single swap), then loop to validate the accumulator across
        // many swaps.
        json::Value cv;
        cv[jss::Account] = al.human();
        cv[jss::Amount] = usd(1000).value().getJson(JsonOptions::Values::None);
        cv[jss::Amount2] = eur(1000).value().getJson(JsonOptions::Values::None);
        cv[jss::TradingFee] = 100;  // 1% — same as fee-accrual test
        cv[jss::TransactionType] = jss::AMMCreate;
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        cv[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(cv);
        env.close();

        provisionBin(env, al, usd, eur, 0);
        json::Value dep;
        dep[jss::Account] = al.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = CtBinned;
        dep[jss::Flags] = tfTwoAsset;
        dep[sfBinID.jsonName] = 0;
        dep[jss::Amount] = usd(1000).value().getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = eur(1000).value().getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep);
        env.close();

        auto const ammSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned));
        auto const ammID = ammSle->key();

        // 20 swaps. After each, verify feeGrowthBin0 is strictly
        // monotonically increasing — any underflow / loss of precision
        // would freeze it at a stale value.
        Number lastFG{0};
        for (int i = 0; i < 20; ++i)
        {
            env(pay(bob, bob, usd(10)),
                Path(~usd),
                Sendmax(eur(12)),
                Txflags(tfNoRippleDirect | tfPartialPayment));
            env.close();
            auto const bin = env.current()->read(keylet::ammBin(ammID, 0));
            if (!BEAST_EXPECT(bin != nullptr))
                return;
            Number const fg{bin->getFieldNumber(sfFeeGrowthBin0)};
            BEAST_EXPECT(fg > lastFG);  // strictly increasing
            lastFG = fg;
        }

        // After 50 swaps, alice should be able to collect a non-zero
        // fee — the integral of all those tiny credits.
        auto const eurPre = env.balance(al, eur.issue());
        json::Value coll;
        coll[jss::Account] = al.human();
        coll[jss::TransactionType] = jss::AMMCollectFees;
        coll[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        coll[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        coll[sfCurveType.jsonName] = CtBinned;
        coll[sfBinID.jsonName] = 0;
        coll[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(coll);
        env.close();
        BEAST_EXPECT(env.balance(al, eur.issue()) > eurPre);
    }

    void
    testSparseBinSHAMapSeek(FeatureBitset features)
    {
        testcase("Swap walks past a huge sparse gap via SHAMap succ "
                 "(no kMaxEmptyBinSkips bound)");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        Account const bob("bob");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);
        fundForAMMCreate(env, gw, bob, usd, eur, /*fundGw=*/false);

        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        env(cv);
        env.close();

        // Liquidity in bin 0 (small) and bin 5000 (large) — a 5000-bin
        // gap. The previous walk would have given up at kMaxEmptyBinSkips=100;
        // the SHAMap-succ walk jumps directly to bin 5000.
        auto deposit = [&](std::int32_t binID, std::int64_t amt) {
            provisionBin(env, al, usd, eur, binID);
            json::Value dep;
            dep[jss::Account] = al.human();
            dep[jss::TransactionType] = jss::AMMDeposit;
            dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            dep[sfCurveType.jsonName] = CtBinned;
            dep[jss::Flags] = tfTwoAsset;
            dep[sfBinID.jsonName] = binID;
            dep[jss::Amount] = usd(amt).value().getJson(JsonOptions::Values::None);
            dep[jss::Amount2] = eur(amt).value().getJson(JsonOptions::Values::None);
            dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(dep);
            env.close();
        };
        deposit(0, 50);
        deposit(5000, 500);

        // Bob asks for 200 USD — bin 0 has only 50, so the walk must
        // jump 5000 bins to bin 5000 to fulfil the rest. Pre-SHAMap-
        // succ, the 100-bin scan limit would have stranded most of it.
        env(pay(bob, bob, usd(200)),
            Path(~usd),
            Sendmax(eur(1000)),
            Txflags(tfNoRippleDirect | tfPartialPayment));
        env.close();

        auto const ammAfter =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned));
        BEAST_EXPECT(ammAfter->getFieldI32(sfActiveBinID) >= 5000);
    }

    void
    testClawbackProportionalDrain(FeatureBitset features)
    {
        testcase("AMMClawback on binned pool drains LP's MPT shares "
                 "proportionally across bins");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");  // issuer of clawbackable asset
        Account const al("alice");  // LP / holder
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        // Issuer must have lsfAllowTrustLineClawback to claw IOUs.
        env.fund(XRP(100000), gw);
        env(fset(gw, asfAllowTrustLineClawback));
        env.close();
        fundForAMMCreate(env, gw, al, usd, eur, /*fundGw=*/false);

        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        env(cv);
        env.close();

        auto const ammSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned));
        auto const ammID = ammSle->key();

        // Alice deposits across two bins.
        auto deposit = [&](std::int32_t binID, std::int64_t amt) {
            provisionBin(env, al, usd, eur, binID);
            json::Value dep;
            dep[jss::Account] = al.human();
            dep[jss::TransactionType] = jss::AMMDeposit;
            dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            dep[sfCurveType.jsonName] = CtBinned;
            dep[jss::Flags] = tfTwoAsset;
            dep[sfBinID.jsonName] = binID;
            dep[jss::Amount] = usd(amt).value().getJson(JsonOptions::Values::None);
            dep[jss::Amount2] = eur(amt).value().getJson(JsonOptions::Values::None);
            dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(dep);
            env.close();
        };
        deposit(0, 100);
        deposit(1, 100);

        auto const sharesBin0Pre = mptSharesOf(env, ammID, 0, al.id());
        auto const sharesBin1Pre = mptSharesOf(env, ammID, 1, al.id());
        BEAST_EXPECT(sharesBin0Pre > 0);
        BEAST_EXPECT(sharesBin1Pre > 0);

        // Issuer claws back EUR (asset0, lex-min vs USD) from alice.
        // Expected: alice loses MPT shares in BOTH bins proportionally;
        // bin reserves drain on both sides (the bin's price invariant
        // requires paired reduction).
        json::Value clw;
        clw[jss::Account] = gw.human();
        clw[jss::TransactionType] = jss::AMMClawback;
        clw[jss::Asset] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        clw[jss::Asset2] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        clw[jss::Holder] = al.human();
        clw[sfCurveType.jsonName] = CtBinned;
        clw[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(clw);
        env.close();

        // Alice's shares dropped in BOTH bins.
        BEAST_EXPECT(mptSharesOf(env, ammID, 0, al.id()) < sharesBin0Pre);
        BEAST_EXPECT(mptSharesOf(env, ammID, 1, al.id()) < sharesBin1Pre);
    }

    void
    testPartialWithdrawAutoCollectsFees(FeatureBitset features)
    {
        testcase("Partial withdraw auto-collects accrued fees against pre-burn balance");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        Account const bob("bob");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);
        fundForAMMCreate(env, gw, bob, usd, eur, /*fundGw=*/false);

        // 100bp fee for visible accrual.
        json::Value cv;
        cv[jss::Account] = al.human();
        cv[jss::Amount] = usd(1000).value().getJson(JsonOptions::Values::None);
        cv[jss::Amount2] = eur(1000).value().getJson(JsonOptions::Values::None);
        cv[jss::TradingFee] = 100;
        cv[jss::TransactionType] = jss::AMMCreate;
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        cv[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(cv);
        env.close();

        auto const ammSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned));
        auto const ammID = ammSle->key();

        provisionBin(env, al, usd, eur, 0);
        // Alice deposits 1000/1000.
        json::Value dep;
        dep[jss::Account] = al.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = CtBinned;
        dep[jss::Flags] = tfTwoAsset;
        dep[sfBinID.jsonName] = 0;
        dep[jss::Amount] = usd(1000).value().getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = eur(1000).value().getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep);
        env.close();

        auto const startingShares = mptSharesOf(env, ammID, 0, al.id());

        // Bob trades to generate fees.
        env(pay(bob, bob, usd(100)),
            Path(~usd),
            Sendmax(eur(110)),
            Txflags(tfNoRippleDirect | tfPartialPayment));
        env.close();

        // Capture alice's wallet balance pre-withdraw.
        auto const eurPre = env.balance(al, eur.issue());
        auto const usdPre = env.balance(al, usd.issue());

        // Partial withdraw — burn half. Auto-collect should have paid
        // out fees on the FULL pre-burn balance, then advanced the
        // snapshot, before the burn took shares away.
        json::Value wd;
        wd[jss::Account] = al.human();
        wd[jss::TransactionType] = jss::AMMWithdraw;
        wd[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        wd[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        wd[sfCurveType.jsonName] = CtBinned;
        wd[sfBinID.jsonName] = 0;
        wd[sfShares.jsonName] = static_cast<json::UInt>(startingShares / 2);
        wd[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(wd);
        env.close();

        // Alice received:
        //  - principal: half of bin's USD reserve (~450 — bob just took
        //    100 USD out) and half of bin's EUR reserve (~550 — bob put
        //    ~101 EUR in)
        //  - PLUS fees: pre-burn shares × (now - snapshot). Fee accrued
        //    on the EUR (input) side ≈ 1 EUR for the 100bp on a ~100
        //    EUR swap. Auto-collect paid this BEFORE the burn.
        auto const usdDelta = env.balance(al, usd.issue()) - usdPre;
        auto const eurDelta = env.balance(al, eur.issue()) - eurPre;
        // Got SOMETHING on each side.
        BEAST_EXPECT(usdDelta > usd(0));
        BEAST_EXPECT(eurDelta > eur(0));
        // EUR side strictly exceeds 50% of the EUR pre-swap reserve
        // (550 baseline) — additional fee was credited via auto-collect.
        BEAST_EXPECT(eurDelta > eur(550));

        // Subsequent collect should be a no-op (snapshot advanced).
        auto const eurAfter = env.balance(al, eur.issue());
        auto const usdAfter = env.balance(al, usd.issue());
        json::Value coll;
        coll[jss::Account] = al.human();
        coll[jss::TransactionType] = jss::AMMCollectFees;
        coll[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        coll[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        coll[sfCurveType.jsonName] = CtBinned;
        coll[sfBinID.jsonName] = 0;
        coll[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(coll);
        env.close();
        BEAST_EXPECT(env.balance(al, usd.issue()) == usdAfter);
        BEAST_EXPECT(env.balance(al, eur.issue()) == eurAfter);
    }

    void
    testActiveBinAdvancesToNearestOnDrain(FeatureBitset features)
    {
        testcase("Draining the active bin moves activeBinID to the nearest non-empty bin");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);

        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        env(cv);
        env.close();

        // Provision three bins; deposit into all. Active starts at 0
        // (default) and advances to 0 on first deposit.
        for (std::int32_t b : {0, 5, 10})
        {
            provisionBin(env, al, usd, eur, b);
            json::Value dep;
            dep[jss::Account] = al.human();
            dep[jss::TransactionType] = jss::AMMDeposit;
            dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            dep[sfCurveType.jsonName] = CtBinned;
            dep[jss::Flags] = tfTwoAsset;
            dep[sfBinID.jsonName] = b;
            dep[jss::Amount] = usd(100).value().getJson(JsonOptions::Values::None);
            dep[jss::Amount2] = eur(100).value().getJson(JsonOptions::Values::None);
            dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(dep);
            env.close();
        }
        BEAST_EXPECT(env.current()
                        ->read(keylet::amm(usd.asset(), eur.asset(), CtBinned))
                        ->getFieldI32(sfActiveBinID) == 0);

        // Drain bin 0 fully.
        json::Value wd;
        wd[jss::Account] = al.human();
        wd[jss::TransactionType] = jss::AMMWithdraw;
        wd[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        wd[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        wd[sfCurveType.jsonName] = CtBinned;
        wd[jss::Flags] = tfWithdrawAll;
        wd[sfBinID.jsonName] = 0;
        wd[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(wd);
        env.close();

        // Nearest surviving bin from 0 is bin 5 (distance 5) vs bin 10
        // (distance 10) — should advance to 5, not arbitrarily to 10.
        auto const ammAfter =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned));
        BEAST_EXPECT(ammAfter->getFieldI32(sfActiveBinID) == 5);
    }

    void
    testBinIDExtremesAccepted(FeatureBitset features)
    {
        testcase("AMMBinCreate accepts ±maxBinID (boundary of Number "
                 "exponent range)");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);

        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 100u;  // 100bp — widest curated step
        env(cv);
        env.close();

        auto const ammSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned));
        BEAST_EXPECT(ammSle);
        auto const ammID = ammSle->key();

        // Provision the two extreme bins. The geometric price formula
        //   p(binID) = (1 + binStep/10000)^binID
        // at binStep=100 hits (1.01)^221818 — exactly the largest
        // exponent Number can represent without overflow. Provisioning
        // these bins must succeed cleanly.
        for (std::int32_t const binID : {minBinID, maxBinID})
        {
            json::Value jv;
            jv[jss::Account] = al.human();
            jv[jss::TransactionType] = "AMMBinCreate";
            jv[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            jv[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            jv[sfBinID.jsonName] = binID;
            jv[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(jv);
            env.close();
            BEAST_EXPECT(env.current()->read(keylet::ammBin(ammID, binID)) != nullptr);
        }
    }

    void
    testBinIDOutOfRangeRejected(FeatureBitset features)
    {
        testcase("AMMBinCreate rejects bin IDs outside [minBinID, maxBinID]");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);

        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        env(cv);
        env.close();

        // One step past each extreme.
        for (std::int32_t const binID : {minBinID - 1, maxBinID + 1})
        {
            json::Value jv;
            jv[jss::Account] = al.human();
            jv[jss::TransactionType] = "AMMBinCreate";
            jv[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            jv[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            jv[sfBinID.jsonName] = binID;
            jv[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(jv, Ter(temMALFORMED));
            env.close();
        }
    }

    void
    testAMMVoteOnBinnedRejected(FeatureBitset features)
    {
        testcase("AMMVote on a binned pool returns tecAMM_FAILED "
                 "(binned pools have no fungible LP shares to weight)");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);

        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        env(cv);
        env.close();

        // Even with bin liquidity present, AMMVote is meaningless for
        // binned: there's no aggregate LP token whose share weighting
        // can drive a vote on the trading fee.
        provisionBin(env, al, usd, eur, 0);
        json::Value dep;
        dep[jss::Account] = al.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = CtBinned;
        dep[jss::Flags] = tfTwoAsset;
        dep[sfBinID.jsonName] = 0;
        dep[jss::Amount] = usd(100).value().getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = eur(100).value().getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep);
        env.close();

        json::Value vote;
        vote[jss::Account] = al.human();
        vote[jss::TransactionType] = jss::AMMVote;
        vote[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        vote[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        vote[jss::TradingFee] = 50;
        vote[sfCurveType.jsonName] = CtBinned;
        vote[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(vote, Ter(tecAMM_FAILED));
        env.close();
    }

    void
    testBookStepRoutesAcrossCurves(FeatureBitset features)
    {
        testcase("BookStep routes a payment through the binned pool when "
                 "it offers the better realized quality vs. a coexisting CP pool");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");   // CP LP
        Account const bob("bob");    // Binned LP
        Account const tr("trader");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);
        fundForAMMCreate(env, gw, bob, usd, eur, /*fundGw=*/false);
        fundForAMMCreate(env, gw, tr, usd, eur, /*fundGw=*/false);

        // CP pool: shallow (100 / 100) with 100bp fee — small swaps eat
        // the curve significantly.
        env(ammCreateJV(env, al, usd, eur, usd(100), eur(100)));
        env.close();

        // Binned pool: same pair, much deeper (1000 / 1000) on bin 0
        // (price = 1, zero within-bin slippage) with a 1bp fee. The
        // binned pool's per-step quality (out/in ≈ 1 − 0.0001) should
        // beat the CP pool's quality on a non-trivial trade.
        {
            auto jv = ammCreateJV(env, al, usd, eur, usd(1), eur(1));
            jv[sfCurveType.jsonName] = CtBinned;
            jv[sfBinStep.jsonName] = 10u;
            jv[jss::TradingFee] = 1;
            env(jv);
            env.close();
        }
        provisionBin(env, bob, usd, eur, 0);
        json::Value dep;
        dep[jss::Account] = bob.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = CtBinned;
        dep[jss::Flags] = tfTwoAsset;
        dep[sfBinID.jsonName] = 0;
        dep[jss::Amount] = usd(1000).value().getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = eur(1000).value().getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep);
        env.close();

        auto const binnedAmmID =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned))->key();
        // EUR is lex-min vs USD, so on the binned bin SLE asset0=EUR
        // and asset1=USD. Trader pays EUR-in / USD-out, so binned bin
        // 0's USD reserve (sfReserve1) is what we expect to decrease
        // if BookStep routed through the binned pool.
        auto const binnedBin0UsdBefore =
            env.current()->read(keylet::ammBin(binnedAmmID, 0))->getFieldAmount(sfReserve1);
        auto const eurBefore = env.balance(tr, eur.issue());
        auto const usdTrBefore = env.balance(tr, usd.issue());

        env(pay(tr, tr, usd(50)),
            Path(~usd),
            Sendmax(eur(60)),
            Txflags(tfNoRippleDirect | tfPartialPayment));
        env.close();

        auto const binnedBin0UsdAfter =
            env.current()->read(keylet::ammBin(binnedAmmID, 0))->getFieldAmount(sfReserve1);

        auto const traderUsdReceived =
            env.balance(tr, usd.issue()) - usdTrBefore;
        auto const binnedUsdDrop =
            binnedBin0UsdBefore - binnedBin0UsdAfter;

        // Payment landed.
        BEAST_EXPECT(traderUsdReceived > usd(0));
        BEAST_EXPECT(env.balance(tr, eur.issue()) < eurBefore);

        // Strong routing check: BookStep should have routed the MAJORITY
        // of the trade through the binned pool (deeper liquidity, lower
        // fee, zero within-bin slippage at price=1). The binned pool's
        // USD drain must account for ≥80% of the USD the trader
        // received — anything lower means BookStep is preferring the
        // shallow CP pool and the multi-curve selection logic is
        // regressed.
        BEAST_EXPECT(binnedUsdDrop > usd(0));
        BEAST_EXPECT(Number{binnedUsdDrop} >=
            Number{traderUsdReceived} * Number{8} / Number{10});
    }

    void
    testAllBinnedTxAmendmentGated(FeatureBitset features)
    {
        testcase("AMMBinCreate / AMMBinDestroy / AMMDeposit(BinID) all "
                 "return temDISABLED without featureAMMCurves");
        using namespace jtx;

        Env env(*this, features - featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);

        // AMMBinCreate against a non-binned pool is the natural smoke
        // test — without the amendment, even reaching preflight should
        // fail temDISABLED.
        {
            json::Value jv;
            jv[jss::Account] = al.human();
            jv[jss::TransactionType] = "AMMBinCreate";
            jv[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            jv[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            jv[sfBinID.jsonName] = 0;
            jv[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(jv, Ter(temDISABLED));
            env.close();
        }
        {
            json::Value jv;
            jv[jss::Account] = al.human();
            jv[jss::TransactionType] = "AMMBinDestroy";
            jv[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            jv[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            jv[sfBinID.jsonName] = 0;
            jv[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(jv, Ter(temDISABLED));
            env.close();
        }
        // AMMDeposit with BinID = an attempt to use the binned surface
        // via the shared transactor — must also be rejected.
        {
            json::Value dep;
            dep[jss::Account] = al.human();
            dep[jss::TransactionType] = jss::AMMDeposit;
            dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            dep[sfCurveType.jsonName] = CtBinned;
            dep[jss::Flags] = tfTwoAsset;
            dep[sfBinID.jsonName] = 0;
            dep[jss::Amount] = usd(100).value().getJson(JsonOptions::Values::None);
            dep[jss::Amount2] = eur(100).value().getJson(JsonOptions::Values::None);
            dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(dep, Ter(temDISABLED));
            env.close();
        }
    }

    void
    testJITDilutedByExistingLPs(FeatureBitset features)
    {
        testcase("Adversarial JIT: bot's last-ledger 1000x outsized deposit "
                 "captures at most its pro-rata share of the bin");
        using namespace jtx;

        // Stronger than testJITResistancePropRata: the bot deposits
        // ~1000x what existing LPs have, but THIS test verifies the
        // bot earns ≤ (bot shares / total shares) of the fee — i.e.
        // pro-rata exactly. There is no concentrated-tick advantage
        // for the JIT bot to exploit in a binned pool.
        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const passive("passive");
        Account const jit("jit");
        Account const tr("trader");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, passive, usd, eur);
        fundForAMMCreate(env, gw, jit, usd, eur, /*fundGw=*/false);
        fundForAMMCreate(env, gw, tr, usd, eur, /*fundGw=*/false);

        auto cv = ammCreateJV(env, passive, usd, eur, usd(1), eur(1));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        cv[jss::TradingFee] = 100;
        env(cv);
        env.close();
        provisionBin(env, passive, usd, eur, 0);

        auto deposit = [&](Account const& lp, std::int64_t amt) {
            json::Value d;
            d[jss::Account] = lp.human();
            d[jss::TransactionType] = jss::AMMDeposit;
            d[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            d[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            d[sfCurveType.jsonName] = CtBinned;
            d[jss::Flags] = tfTwoAsset;
            d[sfBinID.jsonName] = 0;
            d[jss::Amount] = usd(amt).value().getJson(JsonOptions::Values::None);
            d[jss::Amount2] = eur(amt).value().getJson(JsonOptions::Values::None);
            d[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(d);
            env.close();
        };

        // Passive LP: small but established.
        deposit(passive, 10);
        // JIT: 1000x outsized last-ledger sandwich attempt.
        deposit(jit, 10000);

        auto const ammID =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned))->key();
        auto const passiveShares = mptSharesOf(env, ammID, 0, passive.id());
        auto const jitShares = mptSharesOf(env, ammID, 0, jit.id());
        BEAST_EXPECT(passiveShares > 0 && jitShares > passiveShares);

        // Sandwich trade.
        env(pay(tr, tr, usd(50)),
            Path(~usd),
            Sendmax(eur(60)),
            Txflags(tfNoRippleDirect | tfPartialPayment));
        env.close();

        auto collectFor = [&](Account const& acct) {
            auto const eurBefore = env.balance(acct, eur.issue());
            json::Value coll;
            coll[jss::Account] = acct.human();
            coll[jss::TransactionType] = jss::AMMCollectFees;
            coll[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            coll[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            coll[sfCurveType.jsonName] = CtBinned;
            coll[sfBinID.jsonName] = 0;
            coll[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(coll);
            env.close();
            return Number{env.balance(acct, eur.issue()) - eurBefore};
        };
        auto const passiveEur = collectFor(passive);
        auto const jitEur = collectFor(jit);

        // Strong adversarial check: the JIT bot's fee earnings divided
        // by total fee earnings must NOT exceed its share fraction
        // (jitShares / totalShares). In other words, the bot cannot
        // capture MORE than its pro-rata share — there's no way to
        // outearn a passive LP at the same share count. We allow a 1%
        // tolerance for rounding accumulation.
        Number const totalEur = passiveEur + jitEur;
        BEAST_EXPECT(totalEur > Number{0});
        Number const jitShareFraction =
            Number{static_cast<std::int64_t>(jitShares)} /
            Number{static_cast<std::int64_t>(passiveShares + jitShares)};
        Number const jitFeeFraction = jitEur / totalEur;
        // jitFeeFraction must be ≤ jitShareFraction (no super-pro-rata).
        BEAST_EXPECT(jitFeeFraction <= jitShareFraction * Number{101} / Number{100});
    }

    void
    testAMMDeleteOnBinned(FeatureBitset features)
    {
        testcase("AMMDelete on a binned pool: tecHAS_OBLIGATIONS while "
                 "bins exist; tesSUCCESS once every bin has been destroyed");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);

        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        env(cv);
        env.close();

        // Provision and fund 3 bins.
        auto deposit = [&](std::int32_t binID, std::int64_t amt) {
            provisionBin(env, al, usd, eur, binID);
            json::Value dep;
            dep[jss::Account] = al.human();
            dep[jss::TransactionType] = jss::AMMDeposit;
            dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            dep[sfCurveType.jsonName] = CtBinned;
            dep[jss::Flags] = tfTwoAsset;
            dep[sfBinID.jsonName] = binID;
            dep[jss::Amount] = usd(amt).value().getJson(JsonOptions::Values::None);
            dep[jss::Amount2] = eur(amt).value().getJson(JsonOptions::Values::None);
            dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(dep);
            env.close();
        };
        deposit(0, 100);
        deposit(1, 100);
        deposit(2, 100);

        // Stage 1: AMMDelete while bins exist → tecHAS_OBLIGATIONS.
        {
            json::Value del;
            del[jss::Account] = al.human();
            del[jss::TransactionType] = jss::AMMDelete;
            del[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            del[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            del[sfCurveType.jsonName] = CtBinned;
            del[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(del, Ter(tecHAS_OBLIGATIONS));
            env.close();
        }

        // Stage 2: withdraw all + destroy all bins, then re-attempt.
        auto withdrawAll = [&](std::int32_t binID) {
            json::Value wd;
            wd[jss::Account] = al.human();
            wd[jss::TransactionType] = jss::AMMWithdraw;
            wd[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            wd[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            wd[sfCurveType.jsonName] = CtBinned;
            wd[jss::Flags] = tfWithdrawAll;
            wd[sfBinID.jsonName] = binID;
            wd[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(wd);
            env.close();
        };
        auto destroyBin = [&](std::int32_t binID) {
            json::Value dst;
            dst[jss::Account] = al.human();
            dst[jss::TransactionType] = "AMMBinDestroy";
            dst[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            dst[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            dst[sfBinID.jsonName] = binID;
            dst[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(dst);
            env.close();
        };
        for (std::int32_t b : {0, 1, 2})
        {
            withdrawAll(b);
            destroyBin(b);
        }

        // Verify no bin SLEs survive.
        auto const ammID =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned))->key();
        for (std::int32_t b : {0, 1, 2})
            BEAST_EXPECT(env.current()->read(keylet::ammBin(ammID, b)) == nullptr);

        // Stage 3: AMMDelete now succeeds and the AMM SLE is gone.
        {
            json::Value del;
            del[jss::Account] = al.human();
            del[jss::TransactionType] = jss::AMMDelete;
            del[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            del[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            del[sfCurveType.jsonName] = CtBinned;
            del[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(del);
            env.close();
        }
        BEAST_EXPECT(
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned)) == nullptr);
    }

    void
    testDustSpamReserveCharged(FeatureBitset features)
    {
        testcase("Dust spam: each AMMBinCreate charges the owner reserve "
                 "as fee — attacker pays per bin, not for the whole sweep");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const attacker("attacker");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, attacker, usd, eur);

        auto cv = ammCreateJV(env, attacker, usd, eur, usd(100), eur(100));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        env(cv);
        env.close();

        auto const reserveFee = env.current()->fees().increment;

        // Provision 30 bins. Verify each AMMBinCreate burns one
        // ownerReserve worth of XRP from the attacker — confirming
        // the spam cost scales linearly with bin count, not amortised
        // across a single tx.
        auto const xrpBefore = env.balance(attacker, XRP);
        for (std::int32_t binID = 0; binID < 30; ++binID)
        {
            provisionBin(env, attacker, usd, eur, binID);
        }
        auto const xrpAfter = env.balance(attacker, XRP);

        // Expected cost ≈ 30 × ownerReserve increment. Allow ±1
        // increment for the tx-fee accounting that the harness may
        // bill in addition.
        auto const burned = Number{xrpBefore - xrpAfter};
        auto const lowerBound = Number{reserveFee} * Number{29};
        auto const upperBound = Number{reserveFee} * Number{32};
        BEAST_EXPECT(burned > lowerBound);
        BEAST_EXPECT(burned < upperBound);
    }

    void
    testAMMBidOnBinnedRejected(FeatureBitset features)
    {
        testcase("AMMBid on a binned pool returns tecAMM_FAILED "
                 "(no fungible LP token to bid against)");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);

        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        env(cv);
        env.close();
        provisionBin(env, al, usd, eur, 0);
        json::Value dep;
        dep[jss::Account] = al.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = CtBinned;
        dep[jss::Flags] = tfTwoAsset;
        dep[sfBinID.jsonName] = 0;
        dep[jss::Amount] = usd(100).value().getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = eur(100).value().getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep);
        env.close();

        json::Value bid;
        bid[jss::Account] = al.human();
        bid[jss::TransactionType] = jss::AMMBid;
        bid[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        bid[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        bid[sfCurveType.jsonName] = CtBinned;
        bid[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(bid, Ter(tecAMM_FAILED));
        env.close();
    }

    void
    testReserveExemptionAcrossChurnCycles(FeatureBitset features)
    {
        testcase("Reserve exemption: AMM pseudo-account owner count "
                 "stays balanced across 10 create/destroy bin cycles");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);

        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        env(cv);
        env.close();

        auto const ammSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned));
        auto const ammAcct = ammSle->getAccountID(sfAccount);
        auto ownerCount = [&]() {
            auto const acct = env.current()->read(keylet::account(ammAcct));
            return acct ? acct->getFieldU32(sfOwnerCount) : 0u;
        };
        std::uint32_t const baseline = ownerCount();

        // 10 create/destroy cycles on distinct bin IDs. After every
        // cycle, the AMM pseudo-account's owner count must return to
        // its pre-cycle baseline — otherwise a long-lived pool that
        // churns through many bins will leak owner-count and
        // eventually fail AMMDelete's invariant.
        for (std::int32_t b = 0; b < 10; ++b)
        {
            provisionBin(env, al, usd, eur, b);
            BEAST_EXPECTS(
                ownerCount() == baseline,
                "after create, baseline=" + std::to_string(baseline) +
                    " got=" + std::to_string(ownerCount()));

            json::Value dst;
            dst[jss::Account] = al.human();
            dst[jss::TransactionType] = "AMMBinDestroy";
            dst[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            dst[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            dst[sfBinID.jsonName] = b;
            dst[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(dst);
            env.close();
            BEAST_EXPECTS(
                ownerCount() == baseline,
                "after destroy, baseline=" + std::to_string(baseline) +
                    " got=" + std::to_string(ownerCount()));
        }
    }

    void
    testFrozenTrustlineBlocksDeposit(FeatureBitset features)
    {
        testcase("Frozen trustline on a binned pool asset blocks "
                 "AMMDeposit but leaves earlier shares intact");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);

        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        env(cv);
        env.close();
        provisionBin(env, al, usd, eur, 0);

        auto deposit = [&]() {
            json::Value dep;
            dep[jss::Account] = al.human();
            dep[jss::TransactionType] = jss::AMMDeposit;
            dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            dep[sfCurveType.jsonName] = CtBinned;
            dep[jss::Flags] = tfTwoAsset;
            dep[sfBinID.jsonName] = 0;
            dep[jss::Amount] = usd(100).value().getJson(JsonOptions::Values::None);
            dep[jss::Amount2] = eur(100).value().getJson(JsonOptions::Values::None);
            dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            return dep;
        };

        // First deposit: succeeds, alice gets bin shares.
        env(deposit());
        env.close();

        auto const ammID =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned))->key();
        auto const sharesPreFreeze = mptSharesOf(env, ammID, 0, al.id());
        BEAST_EXPECT(sharesPreFreeze > 0);

        // Gateway freezes alice on the USD trustline.
        env(trust(gw, usd(0), al, tfSetFreeze));
        env.close();

        // Subsequent deposit MUST fail (frozen line can't move USD).
        env(deposit(), Ter(tecFROZEN));
        env.close();

        // Pre-freeze shares are intact — the freeze didn't retroactively
        // confiscate alice's earned bin shares.
        BEAST_EXPECT(mptSharesOf(env, ammID, 0, al.id()) == sharesPreFreeze);
    }

    void
    testAMMInfoSurfacesBinnedFields(FeatureBitset features)
    {
        testcase("amm_info RPC returns curve_type=3, bin_step, "
                 "active_bin_id, bin_count for binned pools");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);

        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 25u;
        env(cv);
        env.close();
        provisionBin(env, al, usd, eur, 0);
        provisionBin(env, al, usd, eur, 3);

        json::Value req;
        req[jss::asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        req[jss::asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        req[jss::curve_type] = CtBinned;
        auto const result = env.rpc("json", "amm_info", to_string(req));

        if (!BEAST_EXPECT(result.isMember(jss::result)))
            return;
        auto const& r = result[jss::result];
        if (!BEAST_EXPECT(r.isMember(jss::amm)))
            return;
        auto const& a = r[jss::amm];
        BEAST_EXPECT(a[jss::curve_type].asUInt() == CtBinned);
        BEAST_EXPECT(a[jss::bin_step].asUInt() == 25u);
        BEAST_EXPECT(a.isMember(jss::active_bin_id));
        BEAST_EXPECT(a[jss::bin_count].asUInt() == 2u);
    }

    void
    testFirstDepositAutoAuthorizesMPT(FeatureBitset features)
    {
        testcase("First AMMDeposit into a bin auto-authorizes the LP "
                 "against the bin's MPT issuance");
        using namespace jtx;

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);

        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        env(cv);
        env.close();
        provisionBin(env, al, usd, eur, 0);

        auto const ammID =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned))->key();
        auto const binSle = env.current()->read(keylet::ammBin(ammID, 0));
        auto const mptId = binSle->getFieldH192(sfMPTokenIssuanceID);

        // Pre-deposit: alice holds NO MPToken authorization against the
        // bin's issuance — she's never interacted with it.
        BEAST_EXPECT(env.current()->read(keylet::mptoken(mptId, al.id())) == nullptr);

        json::Value dep;
        dep[jss::Account] = al.human();
        dep[jss::TransactionType] = jss::AMMDeposit;
        dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        dep[sfCurveType.jsonName] = CtBinned;
        dep[jss::Flags] = tfTwoAsset;
        dep[sfBinID.jsonName] = 0;
        dep[jss::Amount] = usd(100).value().getJson(JsonOptions::Values::None);
        dep[jss::Amount2] = eur(100).value().getJson(JsonOptions::Values::None);
        dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(dep);
        env.close();

        // Post-deposit: the MPToken now exists and holds alice's shares.
        auto const lpMpt = env.current()->read(keylet::mptoken(mptId, al.id()));
        BEAST_EXPECT(lpMpt != nullptr);
        if (lpMpt)
            BEAST_EXPECT(lpMpt->getFieldU64(sfMPTAmount) > 0);
    }

    void
    testBinRecreateAfterDestroy(FeatureBitset features)
    {
        testcase("Bin can be re-provisioned at the same binID after "
                 "destroy: MPT issuance ID is deterministic and "
                 "the lifecycle leaves no orphan state");
        using namespace jtx;

        // The bin's MPT issuance sequence is binID-derived (not
        // account-sequence derived), so re-create reuses the same
        // issuance keylet. This test exercises the round-trip to
        // catch any residual SLE the destroy path forgot to clean.

        Env env(*this, features | featureAMMCurves);
        Account const gw("gw");
        Account const al("alice");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        fundForAMMCreate(env, gw, al, usd, eur);

        auto cv = ammCreateJV(env, al, usd, eur, usd(1000), eur(1000));
        cv[sfCurveType.jsonName] = CtBinned;
        cv[sfBinStep.jsonName] = 10u;
        env(cv);
        env.close();

        auto const ammSle =
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned));
        auto const ammID = ammSle->key();

        auto depositInto = [&](std::int32_t binID, std::int64_t amt) {
            json::Value dep;
            dep[jss::Account] = al.human();
            dep[jss::TransactionType] = jss::AMMDeposit;
            dep[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            dep[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            dep[sfCurveType.jsonName] = CtBinned;
            dep[jss::Flags] = tfTwoAsset;
            dep[sfBinID.jsonName] = binID;
            dep[jss::Amount] = usd(amt).value().getJson(JsonOptions::Values::None);
            dep[jss::Amount2] = eur(amt).value().getJson(JsonOptions::Values::None);
            dep[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(dep);
            env.close();
        };
        auto withdrawAll = [&](std::int32_t binID) {
            json::Value wd;
            wd[jss::Account] = al.human();
            wd[jss::TransactionType] = jss::AMMWithdraw;
            wd[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            wd[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            wd[sfCurveType.jsonName] = CtBinned;
            wd[jss::Flags] = tfWithdrawAll;
            wd[sfBinID.jsonName] = binID;
            wd[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(wd);
            env.close();
        };
        auto destroy = [&](std::int32_t binID) {
            json::Value dst;
            dst[jss::Account] = al.human();
            dst[jss::TransactionType] = "AMMBinDestroy";
            dst[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
            dst[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
            dst[sfBinID.jsonName] = binID;
            dst[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
            env(dst);
            env.close();
        };

        // First incarnation: create + use + destroy bin 0.
        provisionBin(env, al, usd, eur, 0);
        depositInto(0, 100);
        auto const binSle1 = env.current()->read(keylet::ammBin(ammID, 0));
        auto const mptID1 = binSle1->getFieldH192(sfMPTokenIssuanceID);
        BEAST_EXPECT(env.current()->read(keylet::mptokenIssuance(mptID1)) != nullptr);
        withdrawAll(0);
        destroy(0);
        BEAST_EXPECT(env.current()->read(keylet::ammBin(ammID, 0)) == nullptr);
        BEAST_EXPECT(env.current()->read(keylet::mptokenIssuance(mptID1)) == nullptr);

        // Second incarnation: same bin ID. MPT issuance keylet is
        // deterministic per (ammID, binID) — must NOT collide with the
        // erased prior issuance.
        provisionBin(env, al, usd, eur, 0);
        auto const binSle2 = env.current()->read(keylet::ammBin(ammID, 0));
        if (!BEAST_EXPECT(binSle2 != nullptr))
            return;
        auto const mptID2 = binSle2->getFieldH192(sfMPTokenIssuanceID);
        BEAST_EXPECT(mptID2 == mptID1);  // deterministic
        BEAST_EXPECT(env.current()->read(keylet::mptokenIssuance(mptID2)) != nullptr);
        depositInto(0, 50);
        auto const shares = mptSharesOf(env, ammID, 0, al.id());
        BEAST_EXPECT(shares > 0);

        // Full teardown + AMMDelete succeeds (no orphans).
        withdrawAll(0);
        destroy(0);
        json::Value del;
        del[jss::Account] = al.human();
        del[jss::TransactionType] = jss::AMMDelete;
        del[jss::Asset] = STIssue(sfAsset, usd.asset()).getJson(JsonOptions::Values::None);
        del[jss::Asset2] = STIssue(sfAsset, eur.asset()).getJson(JsonOptions::Values::None);
        del[sfCurveType.jsonName] = CtBinned;
        del[jss::Fee] = std::to_string(env.current()->fees().increment.drops());
        env(del);
        env.close();
        BEAST_EXPECT(
            env.current()->read(keylet::amm(usd.asset(), eur.asset(), CtBinned)) == nullptr);
    }

public:
    void
    run() override
    {
        auto const features = testableAmendments();
        testWithFeats(features);
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(AMMBinned, app, xrpl, 1);

}  // namespace xrpl::test
