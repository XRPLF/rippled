#include <test/app/lending/LoanManageFixedPrecisionBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace xrpl {

// LoanManage-focused FixedPrecision tests: default and impair's effect on
// AssetsDeployed and LossUnrealized, First-Loss Capital cover on default, and
// the sole-holder clawback scenarios that need an impaired loan to produce
// their liquid/illiquid split.
class LoanManageFixedPrecision_test : public LoanManageFixedPrecisionBase
{
    void
    testLendingAssetsDeployedDefault()
    {
        using namespace test::jtx;
        using namespace loan;

        testcase("Lending: defaulting a Loan reduces AssetsDeployed by its outstanding principal");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupIou(env);

        auto const fixture = setupLendingVault(env, owner, depositor, asset, Number{1'000});
        auto const loanKeylet = openLoan(env, fixture, Number{300}, 3);

        defaultAfterGrace(env, owner, loanKeylet);

        expectVault(env, fixture.vaultKeylet, {.assetsDeployed = Number{0}});
        checkVaultLoanSums(env, fixture, {loanKeylet}, "default");
    }

    void
    testLendingDefaultWithPartialCover()
    {
        using namespace test::jtx;
        using namespace loan;
        using namespace loan_broker;

        testcase("Lending: default with partial First-Loss Capital cover");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupIou(env, {.ownerFunds = 40});

        // 10% minimum cover on 300 of debt needs a deposit of 30; 50% liquidation
        // moves only 15 on default, leaving CoverAvailable at 15.
        auto const fixture = setupLendingVault(
            env,
            owner,
            depositor,
            asset,
            Number{1'000},
            std::chrono::seconds{1'000'000},
            std::nullopt,
            percentageToTenthBips(10),
            percentageToTenthBips(50));

        env(coverDeposit(owner, fixture.brokerKeylet.key, asset(30).value()));
        env.close();

        auto const loanKeylet = openLoan(env, fixture, Number{300}, 3);

        auto const brokerSleBefore = env.le(fixture.brokerKeylet);
        if (!BEAST_EXPECT(brokerSleBefore))
            return;
        Number const coverBefore = brokerSleBefore->at(sfCoverAvailable);
        BEAST_EXPECT(coverBefore == Number{30});

        auto const loanSle = env.le(loanKeylet);
        if (!BEAST_EXPECT(loanSle))
            return;
        closePastGrace(env, loanKeylet);

        auto const before = snapshotVault(env, fixture.vaultKeylet, asset);
        Number const principalBefore = loanSle->at(sfPrincipalOutstanding);
        BEAST_EXPECT(before.assetsDeployed == principalBefore);
        checkVaultLoanSums(env, fixture, {loanKeylet}, "partial cover default, before default");

        env(manage(owner, loanKeylet.key, tfLoanDefault));
        env.close();

        auto const after = snapshotVault(env, fixture.vaultKeylet, asset);
        auto const brokerSleAfter = env.le(fixture.brokerKeylet);
        if (!BEAST_EXPECT(brokerSleAfter))
            return;

        Number const coverAfter = brokerSleAfter->at(sfCoverAvailable);
        Number const coverMoved = coverBefore - coverAfter;
        BEAST_EXPECT(coverMoved == Number{15});
        BEAST_EXPECT(coverAfter == Number{15});

        // AssetsDeployed drops by the full principal, not just the covered part.
        BEAST_EXPECT(before.assetsDeployed - after.assetsDeployed == principalBefore);
        BEAST_EXPECT(after.assetsDeployed == beast::kZero);
        BEAST_EXPECT(after.available - before.available == coverMoved);
        BEAST_EXPECT(before.total - after.total == principalBefore - coverMoved);
        expectVault(env, fixture.vaultKeylet, {});
        checkVaultLoanSums(env, fixture, {loanKeylet}, "partial cover default, after default");
    }

    void
    testLendingDefaultOfImpairedLoan()
    {
        using namespace test::jtx;
        using namespace loan;

        testcase(
            "Lending: defaulting an impaired Loan reverses LossUnrealized and drops "
            "AssetsDeployed");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupIou(env);

        auto const fixture = setupLendingVault(env, owner, depositor, asset, Number{1'000});
        auto const loanKeylet = openLoan(env, fixture, Number{300}, 3);

        // Impair once late, before the grace period ends.
        impairWhenLate(env, owner, loanKeylet);

        auto vaultSle = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(vaultSle))
            return;
        Number const lossUnrealizedAtImpair = vaultSle->at(sfLossUnrealized);
        BEAST_EXPECT(lossUnrealizedAtImpair == Number{300});
        Number const assetsDeployedBeforeDefault = vaultSle->at(sfAssetsDeployed);
        BEAST_EXPECT(assetsDeployedBeforeDefault == Number{300});
        checkVaultLoanSums(env, fixture, {loanKeylet}, "default of impaired loan, after impair");

        defaultAfterGrace(env, owner, loanKeylet);

        auto const vaultSleAfter = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(vaultSleAfter))
            return;

        // The realized loss cancels the paper loss booked at impair time.
        BEAST_EXPECT(vaultSleAfter->at(sfLossUnrealized) == beast::kZero);
        BEAST_EXPECT(vaultSleAfter->at(sfAssetsDeployed) == beast::kZero);
        BEAST_EXPECT(
            assetsDeployedBeforeDefault - vaultSleAfter->at(sfAssetsDeployed) == Number{300});
        BEAST_EXPECT(vaultSleAfter->at(sfAssetsTotal) == getAssetsTotal(vaultSleAfter));
        checkVaultLoanSums(env, fixture, {loanKeylet}, "default of impaired loan, after default");
    }

    // Impair, unimpair, impair again, then default, checking
    // checkVaultLoanSums after every step. Runs on any asset.
    void
    checkImpairUnimpairDefault(
        test::jtx::Env& env,
        test::jtx::Account const& owner,
        test::jtx::Account const& depositor,
        PrettyAsset const& asset,
        Number const& depositAmount,
        Number const& principal,
        std::string const& label)
    {
        using namespace test::jtx;
        using namespace loan;

        auto const fixture = setupLendingVault(env, owner, depositor, asset, depositAmount);
        auto const loanKeylet = openLoan(env, fixture, principal, 3);

        auto const expectLossAndAssetsDeployed = [&](Number const& loss,
                                                     Number const& assetsDeployed) {
            auto const vaultSle = env.le(fixture.vaultKeylet);
            if (!BEAST_EXPECT(vaultSle))
                return;
            BEAST_EXPECT(Number(vaultSle->at(sfLossUnrealized)) == loss);
            BEAST_EXPECT(Number(vaultSle->at(sfAssetsDeployed)) == assetsDeployed);
        };

        impairWhenLate(env, owner, loanKeylet);
        expectLossAndAssetsDeployed(principal, principal);
        checkVaultLoanSums(env, fixture, {loanKeylet}, label + " after impair");

        // Unimpair reverses the paper loss; AssetsDeployed is unaffected.
        env(manage(owner, loanKeylet.key, tfLoanUnimpair));
        env.close();
        expectLossAndAssetsDeployed(0, principal);
        checkVaultLoanSums(env, fixture, {loanKeylet}, label + " after unimpair");

        env(manage(owner, loanKeylet.key, tfLoanImpair));
        env.close();
        checkVaultLoanSums(env, fixture, {loanKeylet}, label + " after second impair");

        defaultAfterGrace(env, owner, loanKeylet);
        expectLossAndAssetsDeployed(0, 0);
        checkVaultLoanSums(env, fixture, {loanKeylet}, label + " after default");
    }

    void
    testLendingImpairUnimpairDefault()
    {
        using namespace test::jtx;

        {
            testcase("Lending: FP XRP vault -- impair, unimpair, impair, default");

            Account const owner{"owner"};
            Account const depositor{"depositor"};
            PrettyAsset const asset = xrpIssue();

            Env env(*this, features());
            env.fund(XRP(1'000'000), owner, depositor);
            env.close();

            checkImpairUnimpairDefault(
                env, owner, depositor, asset, Number{1'000}, Number{300}, "XRP");
        }

        {
            testcase("Lending: FP MPT vault -- impair, unimpair, impair, default");

            Env env(*this, features());
            auto const [issuer, owner, depositor, asset] =
                setupMpt(env, {.maxAmt = 100'000, .withDepositor = true, .holderFunds = 1'000});

            checkImpairUnimpairDefault(
                env, owner, depositor, asset, Number{1'000}, Number{300}, "MPT");
        }
    }

    void
    testCoarsenedVaultDefaultWithPartialCover()
    {
        using namespace test::jtx;
        using namespace loan;
        using namespace loan_broker;

        testcase("Lending: default with partial First-Loss Capital cover on a coarsened vault");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupIou(
            env,
            {.depositorTrust = 30'000'000,
             .ownerTrust = 1'000'000,
             .depositorFunds = 10'000'000,
             .ownerFunds = 200'000});

        // Minimum cover is checked at every origination against the broker's
        // cumulative DebtTotal, so it must cover the 700,000 coarsening loan too.
        // 150,000 covers 10% of the largest cumulative DebtTotal (750,000).
        Number const defaultLoanPrincipal{50'000};
        auto const coarsened = coarsenVault(
            env,
            owner,
            depositor,
            asset,
            {.extraLoans = {ExtraLoan{
                 .principal = defaultLoanPrincipal, .paymentInterval = 1000 * 24 * 60 * 60}},
             .coverRateMinimum = percentageToTenthBips(10),
             .coverRateLiquidation = percentageToTenthBips(50),
             .coverDeposit = Number{150'000}});
        auto const& vaultKeylet = coarsened.fixture.vaultKeylet;
        auto const& brokerKeylet = coarsened.fixture.brokerKeylet;
        auto const& loanKeylet = coarsened.loanKeylets[1];
        expectCoarsened(env, coarsened);

        auto const brokerSleBefore = env.le(brokerKeylet);
        auto const loanSleBefore = env.le(loanKeylet);
        if (!BEAST_EXPECT(brokerSleBefore) || !BEAST_EXPECT(loanSleBefore))
            return;
        Number const coverBefore = brokerSleBefore->at(sfCoverAvailable);
        BEAST_EXPECT(coverBefore == Number{150'000});
        Number const principalBefore = loanSleBefore->at(sfPrincipalOutstanding);
        BEAST_EXPECT(principalBefore == defaultLoanPrincipal);

        closePastGrace(env, loanKeylet);
        auto const before = snapshotVault(env, vaultKeylet, asset);

        env(manage(owner, loanKeylet.key, tfLoanDefault));
        env.close();

        auto const after = snapshotVault(env, vaultKeylet, asset);
        auto const brokerSleAfter = env.le(brokerKeylet);
        if (!BEAST_EXPECT(brokerSleAfter))
            return;

        Number const coverMoved = coverBefore - Number(brokerSleAfter->at(sfCoverAvailable));
        // coverMoved is 50% of 10% of the broker's cumulative DebtTotal at default,
        // capped at this loan's principal and CoverAvailable. The exact value
        // depends on how much the coarsening loan had repaid.
        BEAST_EXPECT(coverMoved > beast::kZero);
        BEAST_EXPECT(coverMoved < defaultLoanPrincipal);
        BEAST_EXPECT(coverMoved <= coverBefore);

        // AssetsDeployed drops by the full principal; AssetsAvailable rises by the cover
        // moved.
        BEAST_EXPECT(before.assetsDeployed - after.assetsDeployed == principalBefore);
        BEAST_EXPECT(after.available - before.available == coverMoved);

        checkVaultLoanSums(env, coarsened, "testCoarsenedVaultDefaultWithPartialCover");
    }

    void
    testCoarsenedVaultMultipleDefaultsAndImpair()
    {
        using namespace test::jtx;
        using namespace loan;

        testcase(
            "Lending: repeated zero-cover defaults and an impair on a coarsened vault, then "
            "final withdrawal stays refused");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupCoarsenIou(env);

        // defaultA and defaultB are off-grid (a nonzero digit below the coarsened
        // live unit) to check AssetsDeployed stays exact through default.
        Number const defaultAPrincipal{40'0000000003LL, -10};
        Number const defaultBPrincipal{35'0000000007LL, -10};
        Number const remainingPrincipal{30'000};
        auto const coarsened = coarsenVault(
            env,
            owner,
            depositor,
            asset,
            {.loanPrincipal = Number{300'000},
             .extraLoans = {
                 ExtraLoan{.principal = defaultAPrincipal},
                 ExtraLoan{.principal = defaultBPrincipal},
                 ExtraLoan{.principal = remainingPrincipal}}});
        auto const& vaultKeylet = coarsened.fixture.vaultKeylet;
        auto const& coarseningLoanKeylet = coarsened.loanKeylets[0];
        auto const& defaultALoanKeylet = coarsened.loanKeylets[1];
        auto const& defaultBLoanKeylet = coarsened.loanKeylets[2];
        auto const& remainingLoanKeylet = coarsened.loanKeylets[3];

        auto const vaultSle0 = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSle0))
            return;
        BEAST_EXPECT(getVaultScale(vaultSle0) > getVaultBaseScale(vaultSle0));
        checkVaultLoanSums(
            env, coarsened, "testCoarsenedVaultMultipleDefaultsAndImpair (after coarsening)");

        // defaultA: impaired, then defaulted. Both defaults are zero-cover
        // (coverRateMinimum is 0).
        env(manage(owner, defaultALoanKeylet.key, tfLoanImpair));
        env.close();
        env(manage(owner, defaultALoanKeylet.key, tfLoanDefault));
        env.close();
        checkVaultLoanSums(
            env, coarsened, "testCoarsenedVaultMultipleDefaultsAndImpair (after defaultA)");

        env(manage(owner, defaultBLoanKeylet.key, tfLoanDefault));
        env.close();
        checkVaultLoanSums(
            env, coarsened, "testCoarsenedVaultMultipleDefaultsAndImpair (after defaultB)");

        // The coarsening loan is many periods overdue, so it can be defaulted too.
        env(manage(owner, coarseningLoanKeylet.key, tfLoanDefault));
        env.close();
        checkVaultLoanSums(
            env, coarsened, "testCoarsenedVaultMultipleDefaultsAndImpair (after third default)");

        auto const vaultSleAfterDefaults = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSleAfterDefaults))
            return;
        BEAST_EXPECT(
            Number(vaultSleAfterDefaults->at(sfAssetsDeployed)) == Number(remainingPrincipal));

        env(manage(owner, remainingLoanKeylet.key, tfLoanImpair));
        env.close();
        checkVaultLoanSums(
            env, coarsened, "testCoarsenedVaultMultipleDefaultsAndImpair (after impair)");

        auto const vaultSleImpaired = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSleImpaired))
            return;
        BEAST_EXPECT(Number(vaultSleImpaired->at(sfAssetsDeployed)) != beast::kZero);

        // Withdrawals need the Redemption phase.
        env.close(coarsened.fixture.redemptionDate + std::chrono::seconds{1});

        // A withdrawal of the full share value is a final withdrawal; AssetsDeployed != 0
        // makes it report tecHAS_OBLIGATIONS.
        Number const assetsTotal = getAssetsTotal(vaultSleImpaired);
        env(coarsened.fixture.vault.withdraw(
                {.depositor = depositor, .id = vaultKeylet.key, .amount = asset(assetsTotal)}),
            Ter(tecHAS_OBLIGATIONS));
        env.close();

        auto const vaultSleFinal = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSleFinal))
            return;
        BEAST_EXPECT(Number(vaultSleFinal->at(sfAssetsDeployed)) != beast::kZero);
        checkVaultLoanSums(env, coarsened, "testCoarsenedVaultMultipleDefaultsAndImpair (final)");
    }

    struct SmallAssetsDeployedState
    {
        CoarsenedVault coarsened;
        Number assetsDeployed;
        Number available;
    };

    // Reaches the small-AssetsDeployed state: a second loan is overpaid down to a
    // sub-live-unit remainder dust (one base unit at Scale 10, under the 5e-10
    // half live unit), then the coarsening loan is defaulted, leaving dust as the
    // only AssetsDeployed. Asserts the rounding hazard: AssetsAvailable + AssetsDeployed,
    // rounded to 16 digits, equals AssetsAvailable.
    SmallAssetsDeployedState
    setupSmallAssetsDeployedVault(
        test::jtx::Env& env,
        test::jtx::Account const& owner,
        test::jtx::Account const& depositor,
        PrettyAsset const& asset,
        std::string const& label)
    {
        using namespace test::jtx;
        using namespace loan;

        Number const overpayPrincipal{1'000};
        Number const dust{1, -10};
        auto coarsened = coarsenVault(
            env,
            owner,
            depositor,
            asset,
            {.extraLoans = {ExtraLoan{
                 .principal = overpayPrincipal,
                 .flags = tfLoanOverpayment,
                 .paymentTotal = 2,
                 .paymentInterval = 1000 * 24 * 60 * 60}}});
        auto const& vaultKeylet = coarsened.fixture.vaultKeylet;
        auto const& coarseningLoanKeylet = coarsened.loanKeylets[0];
        auto const& dustLoanKeylet = coarsened.loanKeylets[1];
        expectCoarsened(env, coarsened);

        env(
            pay(depositor,
                dustLoanKeylet.key,
                asset(overpayPrincipal - dust).value(),
                tfLoanOverpayment));
        env.close();

        auto const dustLoanSle = env.le(dustLoanKeylet);
        if (BEAST_EXPECT(dustLoanSle))
        {
            BEAST_EXPECT(Number(dustLoanSle->at(sfPrincipalOutstanding)) == dust);
            BEAST_EXPECT(dustLoanSle->at(sfPaymentRemaining) == 1);
        }
        checkVaultLoanSums(env, coarsened, label + " (after overpay)");

        env(manage(owner, coarseningLoanKeylet.key, tfLoanDefault));
        env.close();
        checkVaultLoanSums(env, coarsened, label + " (after default)");

        auto const vaultSle = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSle))
        {
            return {
                .coarsened = std::move(coarsened),
                .assetsDeployed = Number{0},
                .available = Number{0}};
        }
        Number const assetsDeployed = vaultSle->at(sfAssetsDeployed);
        Number const available = vaultSle->at(sfAssetsAvailable);
        BEAST_EXPECT(assetsDeployed == dust);
        BEAST_EXPECT(assetsDeployed != beast::kZero);
        BEAST_EXPECT(getVaultScale(vaultSle) != getVaultBaseScale(vaultSle));

        STAmount const roundedTotal = STAmount{asset.raw(), available + assetsDeployed};
        STAmount const availableAmount = STAmount{asset.raw(), available};
        BEAST_EXPECT(roundedTotal == availableAmount);

        return {
            .coarsened = std::move(coarsened),
            .assetsDeployed = assetsDeployed,
            .available = available};
    }

    // The tecHAS_OBLIGATIONS guard in VaultWithdraw exists for this case: with
    // AssetsDeployed under half a live unit, AssetsAvailable + AssetsDeployed rounds to
    // AssetsAvailable, so the insufficient-funds check would pass and the vault
    // would end with no shares or assets but non-zero AssetsDeployed.
    void
    testCoarsenedVaultSmallAssetsDeployedFinalWithdrawalRejected()
    {
        using namespace test::jtx;
        using namespace loan;

        testcase(
            "Lending: full-value VaultWithdraw stays refused on a coarsened vault even when "
            "AssetsDeployed is smaller than half the live unit");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupCoarsenIou(env);

        auto const state = setupSmallAssetsDeployedVault(
            env,
            owner,
            depositor,
            asset,
            "testCoarsenedVaultSmallAssetsDeployedFinalWithdrawalRejected");
        auto const& coarsened = state.coarsened;
        auto const& vaultKeylet = coarsened.fixture.vaultKeylet;

        env.close(coarsened.fixture.redemptionDate + std::chrono::seconds{1});

        // Name the full share count, not an asset amount: an asset amount equal to
        // AssetsAvailable truncates the derived share count below the full supply and
        // would miss isFinalWithdrawal, masking the guard under test.
        auto const vaultSle = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSle))
            return;
        auto const shareMPTID = vaultSle->at(sfShareMPTID);
        auto const issuanceBefore = env.le(keylet::mptokenIssuance(shareMPTID));
        if (!BEAST_EXPECT(issuanceBefore))
            return;
        STAmount const allShares{
            MPTIssue{shareMPTID}, Number(issuanceBefore->getFieldU64(sfOutstandingAmount))};

        env(coarsened.fixture.vault.withdraw(
                {.depositor = depositor, .id = vaultKeylet.key, .amount = allShares}),
            Ter(tecHAS_OBLIGATIONS));
        env.close();

        auto const after = snapshotVault(env, vaultKeylet, asset);
        BEAST_EXPECT(after.assetsDeployed == state.assetsDeployed);
        BEAST_EXPECT(after.available == state.available);
        checkVaultLoanSums(
            env, coarsened, "testCoarsenedVaultSmallAssetsDeployedFinalWithdrawalRejected (final)");
    }

    // Clawback counterpart of testCoarsenedVaultSmallAssetsDeployedFinalWithdrawalRejected:
    // a full clawback hits the same rounding hazard, and the tecHAS_OBLIGATIONS
    // guard fires.
    void
    testCoarsenedVaultSmallAssetsDeployedFullClawbackRejected()
    {
        using namespace test::jtx;
        using namespace loan;

        testcase(
            "Lending: full-value VaultClawback stays refused on a coarsened vault even when "
            "AssetsDeployed is smaller than half the live unit");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupCoarsenIou(env, true);

        auto const state = setupSmallAssetsDeployedVault(
            env,
            owner,
            depositor,
            asset,
            "testCoarsenedVaultSmallAssetsDeployedFullClawbackRejected");
        auto const& coarsened = state.coarsened;
        auto const& vaultKeylet = coarsened.fixture.vaultKeylet;

        env(coarsened.fixture.vault.clawback(
                {.issuer = issuer, .id = vaultKeylet.key, .holder = depositor}),
            Ter(tecHAS_OBLIGATIONS));
        env.close();

        auto const after = snapshotVault(env, vaultKeylet, asset);
        BEAST_EXPECT(after.assetsDeployed == state.assetsDeployed);
        BEAST_EXPECT(after.available == state.available);
        checkVaultLoanSums(
            env, coarsened, "testCoarsenedVaultSmallAssetsDeployedFullClawbackRejected (final)");
    }

    void
    testCoarsenedVaultDefaultCoverCrossesPowerOfTenUpward()
    {
        using namespace test::jtx;
        using namespace loan;
        using namespace loan_broker;

        testcase(
            "Lending: a default's cover credit that crosses a power of ten keeps "
            "AssetsAvailable at 16 digits");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupIou(
            env,
            {.depositorTrust = 30'000'000,
             .ownerTrust = 1'000'000,
             .depositorFunds = 10'000'000,
             .ownerFunds = 200'000});

        // Replays testCoarsenedVaultDefaultWithPartialCover's setup. The loan must
        // be an ExtraLoan: LoanSet's Open-zone guard refuses origination on a
        // coarsened vault.
        Number const defaultLoanPrincipal{50'000};
        auto const coarsened = coarsenVault(
            env,
            owner,
            depositor,
            asset,
            {.extraLoans = {ExtraLoan{
                 .principal = defaultLoanPrincipal, .paymentInterval = 1000 * 24 * 60 * 60}},
             .coverRateMinimum = percentageToTenthBips(10),
             .coverRateLiquidation = percentageToTenthBips(50),
             .coverDeposit = Number{150'000}});
        // Default overdueDays (800) coarsens AssetsAvailable's own grid, not just
        // getVaultScale(); the crossing below must exercise the 17th-digit sum
        // floor in creditToPosteriorAvailableScale, not just a 15-to-16-digit
        // widening that every rounding mode handles identically.
        auto const& vaultKeylet = coarsened.fixture.vaultKeylet;
        auto const& defaultLoanKeylet = coarsened.loanKeylets[1];
        expectCoarsened(env, coarsened);

        auto const loanSleBefore = env.le(defaultLoanKeylet);
        if (!BEAST_EXPECT(loanSleBefore))
            return;
        Number const principalBefore = loanSleBefore->at(sfPrincipalOutstanding);
        closePastGrace(env, defaultLoanKeylet);

        auto const brokerSle0 = env.le(coarsened.fixture.brokerKeylet);
        if (!BEAST_EXPECT(brokerSle0))
            return;
        Number const cumulativeDebtTotal = brokerSle0->at(sfDebtTotal);
        Number const coverAvailableBefore = brokerSle0->at(sfCoverAvailable);

        // Mirror LoanManage::defaultLoanFixedPrecision's rawCover formula so the
        // test can independently predict the credited amount and detect whether
        // the grid floors below actually trimmed it.
        Number const rawCoverUncapped = [&]() {
            NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
            Number const minimumCover =
                tenthBipsOfValue(cumulativeDebtTotal, percentageToTenthBips(10));
            return std::min(
                tenthBipsOfValue(minimumCover, percentageToTenthBips(50)), principalBefore);
        }();
        Number const rawCoverCapped = std::min(rawCoverUncapped, coverAvailableBefore);

        // rawCoverCapped is the exact pre-grid-rounding cover amount, so the
        // shrink target can place AssetsAvailable right at the crossing instead
        // of guessing with a conservative estimate.
        Number const margin{1'000};
        Number const powerOfTen{100'000};
        Number const shrinkTarget = powerOfTen - rawCoverCapped + margin;
        BEAST_EXPECT(shrinkTarget > beast::kZero);
        BEAST_EXPECT(shrinkTarget < powerOfTen);

        // Withdrawals need the Redemption phase.
        env.close(coarsened.fixture.redemptionDate + std::chrono::seconds{1});

        // Bring AssetsAvailable close to (but above) the boundary in one withdrawal.
        Number const step = snapshotVault(env, vaultKeylet, asset).available - shrinkTarget;
        if (step > beast::kZero)
        {
            env(coarsened.fixture.vault.withdraw(
                {.depositor = depositor, .id = vaultKeylet.key, .amount = asset(step)}));
            env.close();
        }

        auto const before = snapshotVault(env, vaultKeylet, asset);
        BEAST_EXPECT(before.available < powerOfTen);

        // Independently predict the credited cover using the same two-stage
        // grid rounding defaultLoanFixedPrecision applies: round at the
        // broker's posterior cover grid, then floor the sum at AssetsAvailable's
        // own posterior grid. If either stage actually trims a digit, the
        // predicted credit is strictly below rawCoverCapped.
        auto const vaultSleBeforeDefault = env.le(vaultKeylet);
        auto const brokerSleBeforeDefault = env.le(coarsened.fixture.brokerKeylet);
        if (!BEAST_EXPECT(vaultSleBeforeDefault) || !BEAST_EXPECT(brokerSleBeforeDefault))
            return;
        STAmount const brokerRoundedCover = [&] {
            NumberRoundModeGuard const mg(Number::RoundingMode::TowardsZero);
            return debitToPosteriorBrokerCoverScale(
                vaultSleBeforeDefault,
                brokerSleBeforeDefault,
                STAmount{asset, rawCoverCapped},
                Number::RoundingMode::TowardsZero);
        }();
        STAmount const expectedCoverAmount = creditToPosteriorAvailableScale(
            vaultSleBeforeDefault, brokerRoundedCover, Number::RoundingMode::Downward);
        // rawCoverCapped is itself an exact multiple of the grid for these
        // numbers, so the two-stage rounding is not guaranteed to discard a
        // digit here; the point of this test is that the production code and
        // this independent prediction agree exactly, and that the result
        // lands on the posterior grid across the power-of-ten crossing.
        BEAST_EXPECT(Number(expectedCoverAmount) <= rawCoverCapped);

        env(manage(owner, defaultLoanKeylet.key, tfLoanDefault));
        env.close();

        auto const after = snapshotVault(env, vaultKeylet, asset);
        Number const coverMoved = after.available - before.available;

        // AssetsDeployed drops by the full principal; the cover credited back is
        // the grid-floored amount computed above, not the raw rate-based figure,
        // and is large enough to cross powerOfTen upward.
        BEAST_EXPECT(before.assetsDeployed - after.assetsDeployed == principalBefore);
        BEAST_EXPECT(coverMoved == Number(expectedCoverAmount));
        BEAST_EXPECT(coverMoved <= rawCoverCapped);
        BEAST_EXPECT(expectAvailableAtSixteenDigits(env, vaultKeylet, asset) >= powerOfTen);

        checkVaultLoanSums(env, coarsened, "testCoarsenedVaultDefaultCoverCrossesPowerOfTenUpward");
    }

    // Like VaultFixedPrecisionBase::setupMpt, but the issuance also allows
    // clawback (tfMPTCanClawback), needed for VaultClawback against an MPT
    // vault asset. Only the owner and depositor are funded; the depositor
    // holds the balance and is the vault's sole depositor.
    static AssetAccounts
    setupClawbackMpt(test::jtx::Env& env, std::uint64_t maxAmt, std::uint64_t holderFunds)
    {
        using namespace test::jtx;

        AssetAccounts a;
        env.fund(XRP(1'000'000), a.issuer, a.owner, a.depositor);
        env.close();

        MPTTester mpt{env, a.issuer, kMptInitNoFund};
        mpt.create({.maxAmt = maxAmt, .flags = tfMPTCanTransfer | tfMPTCanClawback});
        a.asset = mpt.issuanceID();
        mpt.authorize({.account = a.owner});
        mpt.authorize({.account = a.depositor});
        env(pay(a.issuer, a.depositor, a.asset(holderFunds)));
        env.close();
        return a;
    }

    // A sole holder's shares are priced without the unrealized-loss
    // discount, for VaultWithdraw and VaultClawback alike, so the smallest
    // amount either can take is one full share's value. A request
    // below that truncates to zero shares and is refused with
    // tecPRECISION_LOSS. This is share granularity, by design.
    void
    testSoleHolderClawbackBelowOneShareIsPrecisionLoss()
    {
        using namespace test::jtx;
        using namespace loan;

        testcase("sole-holder VaultClawback below one share's value is tecPRECISION_LOSS");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupClawbackMpt(env, 1'000'000, 900'000);

        // Deposit two MPT into an empty vault, exactly matching the scenario: the
        // sole holder owns exactly 2 integral shares. Ten doubling cycles (100%
        // interest, single payment, repaid in full each time) grow AssetsTotal
        // without ever touching the share count -- the "originate, repay, and
        // delete...loans that grow the vault" step this scenario depends on. The
        // exact multiplier does not need to match any particular target, only the
        // mechanism: a sole holder with an integral share count small enough that
        // pricing without the loss discount truncates a liquid partial request to zero shares.
        auto const fixture = setupLendingVault(
            env, owner, depositor, asset, Number{2}, std::chrono::seconds{400'000'000});
        compoundVaultBySoleHolderLoanCycles(env, fixture, depositor, asset, 10);

        auto const vaultSle0 = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(vaultSle0))
            return;
        BEAST_EXPECT(Number(vaultSle0->at(sfAssetsDeployed)) == beast::kZero);
        std::uint64_t const grownTotal = toU64(vaultSle0, sfAssetsAvailable);
        BEAST_EXPECT(grownTotal >= 512);

        auto const shareMPTID = vaultSle0->at(sfShareMPTID);
        auto const issuance0 = env.le(keylet::mptokenIssuance(shareMPTID));
        if (!BEAST_EXPECT(issuance0))
            return;
        BEAST_EXPECT(issuance0->getFieldU64(sfOutstandingAmount) == 2);

        // Originate a loan for half the grown vault and impair it fully:
        // AssetsAvailable stays liquid at (grownTotal - principal), and
        // LossUnrealized == AssetsDeployed == principal is the illiquid half,
        // matching a liquid/illiquid split (AssetsAvailable=898,
        // LossUnrealized=902, AssetsTotal=1800) sized to the same mechanism.
        std::uint64_t const principal = grownTotal / 2;
        auto const loanKeylet =
            openLoan(env, fixture, Number{static_cast<std::int64_t>(principal)}, 3);
        impairWhenLate(env, owner, loanKeylet);

        auto const vaultSle1 = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(vaultSle1))
            return;
        std::uint64_t const available = toU64(vaultSle1, sfAssetsAvailable);
        BEAST_EXPECT(available == grownTotal - principal);
        BEAST_EXPECT(toU64(vaultSle1, sfAssetsDeployed) == principal);
        BEAST_EXPECT(toU64(vaultSle1, sfLossUnrealized) == principal);
        BEAST_EXPECT(getAssetsTotal(vaultSle1) == Number{static_cast<std::int64_t>(grownTotal)});

        // Half of the liquid balance is less than one share's value without the
        // loss discount (grownTotal / 2), so it converts to zero shares.
        std::uint64_t const requested = available / 2;
        if (!BEAST_EXPECT(requested > 0))
            return;
        BEAST_EXPECT(2 * requested < grownTotal);

        env(fixture.vault.clawback(
                {.issuer = issuer,
                 .id = fixture.vaultKeylet.key,
                 .holder = depositor,
                 .amount = asset(requested).value()}),
            Ter(tecPRECISION_LOSS));
        env.close();

        auto const vaultSleAfter = env.le(fixture.vaultKeylet);
        auto const issuanceAfter = env.le(keylet::mptokenIssuance(shareMPTID));
        if (!BEAST_EXPECT(vaultSleAfter) || !BEAST_EXPECT(issuanceAfter))
            return;
        BEAST_EXPECT(toU64(vaultSleAfter, sfAssetsAvailable) == available);
        BEAST_EXPECT(issuanceAfter->getFieldU64(sfOutstandingAmount) == 2);

        checkVaultLoanSums(
            env, fixture, {loanKeylet}, "testSoleHolderClawbackBelowOneShareIsPrecisionLoss");
    }

    // A clawback worth more than AssetsAvailable is clamped to the
    // whole shares AssetsAvailable can pay for. Here the request is two
    // shares' worth and the vault holds one share's worth in cash.
    void
    testSoleHolderClawbackClampsToWholeShares()
    {
        using namespace test::jtx;
        using namespace loan;

        testcase("sole-holder VaultClawback above AssetsAvailable is clamped to whole shares");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupClawbackMpt(env, 1'000'000, 900'000);

        auto const fixture = setupLendingVault(
            env, owner, depositor, asset, Number{2}, std::chrono::seconds{400'000'000});
        compoundVaultBySoleHolderLoanCycles(env, fixture, depositor, asset, 10);

        auto const vaultSle0 = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(vaultSle0))
            return;
        std::uint64_t const grownTotal = toU64(vaultSle0, sfAssetsAvailable);
        std::uint64_t const principal = grownTotal / 2;
        auto const loanKeylet =
            openLoan(env, fixture, Number{static_cast<std::int64_t>(principal)}, 3);
        impairWhenLate(env, owner, loanKeylet);

        auto const shareMPTID = vaultSle0->at(sfShareMPTID);
        auto const vaultSle1 = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(vaultSle1))
            return;
        std::uint64_t const available = toU64(vaultSle1, sfAssetsAvailable);
        BEAST_EXPECT(available == grownTotal - principal);

        // Two shares' worth, without the loss discount, is the whole AssetsTotal.
        env(fixture.vault.clawback(
                {.issuer = issuer,
                 .id = fixture.vaultKeylet.key,
                 .holder = depositor,
                 .amount = asset(grownTotal).value()}),
            Ter(tesSUCCESS));
        env.close();

        auto const vaultSleAfter = env.le(fixture.vaultKeylet);
        auto const issuanceAfter = env.le(keylet::mptokenIssuance(shareMPTID));
        if (!BEAST_EXPECT(vaultSleAfter) || !BEAST_EXPECT(issuanceAfter))
            return;
        // One share burned, one share's worth recovered.
        BEAST_EXPECT(issuanceAfter->getFieldU64(sfOutstandingAmount) == 1);
        BEAST_EXPECT(toU64(vaultSleAfter, sfAssetsAvailable) == available - (grownTotal / 2));
        BEAST_EXPECT(toU64(vaultSleAfter, sfAssetsDeployed) == principal);

        checkVaultLoanSums(env, fixture, {loanKeylet}, "testSoleHolderClawbackClampsToWholeShares");
    }

    // Control case: the full/burn-all clawback path the sole-holder waiver
    // was designed for still clamps to AssetsAvailable, per
    // testLendingFullClawbackClampsInsteadOfHasObligations, on the same
    // liquid/illiquid split constructed above -- so the partial-request
    // failure above is not a regression of the full-recovery path the
    // waiver was meant to fix.
    void
    testSoleHolderFullClawbackClampsWithLiquidAndIlliquidSplit()
    {
        using namespace test::jtx;
        using namespace loan;

        testcase(
            "sole-holder full VaultClawback on a liquid/illiquid split still "
            "clamps to AssetsAvailable instead of tecHAS_OBLIGATIONS");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupClawbackMpt(env, 1'000'000, 900'000);

        auto const fixture = setupLendingVault(
            env, owner, depositor, asset, Number{2}, std::chrono::seconds{400'000'000});
        compoundVaultBySoleHolderLoanCycles(env, fixture, depositor, asset, 10);

        auto const vaultSle0 = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(vaultSle0))
            return;
        std::uint64_t const grownTotal = toU64(vaultSle0, sfAssetsAvailable);
        std::uint64_t const principal = grownTotal / 2;
        auto const loanKeylet =
            openLoan(env, fixture, Number{static_cast<std::int64_t>(principal)}, 3);
        impairWhenLate(env, owner, loanKeylet);

        auto const shareMPTID = vaultSle0->at(sfShareMPTID);
        auto const issuanceBefore = env.le(keylet::mptokenIssuance(shareMPTID));
        if (!BEAST_EXPECT(issuanceBefore))
            return;
        BEAST_EXPECT(issuanceBefore->getFieldU64(sfOutstandingAmount) == 2);

        env(fixture.vault.clawback(
                {.issuer = issuer, .id = fixture.vaultKeylet.key, .holder = depositor}),
            Ter(tesSUCCESS));
        env.close();

        auto const vaultSleAfter = env.le(fixture.vaultKeylet);
        auto const issuanceAfter = env.le(keylet::mptokenIssuance(shareMPTID));
        if (!BEAST_EXPECT(vaultSleAfter) || !BEAST_EXPECT(issuanceAfter))
            return;
        BEAST_EXPECT(toU64(vaultSleAfter, sfAssetsAvailable) == 0);
        BEAST_EXPECT(toU64(vaultSleAfter, sfAssetsDeployed) == principal);
        // Shares are left behind rather than burning every share while AssetsDeployed
        // is non-zero (VaultClawback::doApply's tecHAS_OBLIGATIONS guard).
        std::uint64_t const sharesAfter = issuanceAfter->getFieldU64(sfOutstandingAmount);
        // A clamped partial clawback can only ever leave the sole holder's
        // single remaining share outstanding.
        BEAST_EXPECT(sharesAfter == 1);

        checkVaultLoanSums(
            env,
            fixture,
            {loanKeylet},
            "testSoleHolderFullClawbackClampsWithLiquidAndIlliquidSplit");
    }

public:
    void
    run() override
    {
        testLendingAssetsDeployedDefault();
        testLendingDefaultWithPartialCover();
        testLendingDefaultOfImpairedLoan();
        testLendingImpairUnimpairDefault();
        testCoarsenedVaultDefaultWithPartialCover();
        testCoarsenedVaultMultipleDefaultsAndImpair();
        testCoarsenedVaultSmallAssetsDeployedFinalWithdrawalRejected();
        testCoarsenedVaultSmallAssetsDeployedFullClawbackRejected();
        testCoarsenedVaultDefaultCoverCrossesPowerOfTenUpward();
        testSoleHolderClawbackBelowOneShareIsPrecisionLoss();
        testSoleHolderClawbackClampsToWholeShares();
        testSoleHolderFullClawbackClampsWithLiquidAndIlliquidSplit();
    }
};

BEAST_DEFINE_TESTSUITE(LoanManageFixedPrecision, app, xrpl);

}  // namespace xrpl
