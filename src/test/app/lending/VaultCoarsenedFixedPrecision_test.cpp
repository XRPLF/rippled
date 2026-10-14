#include <test/app/lending/LoanPayFixedPrecisionBase.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/ter.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>

#include <chrono>
#include <string>

namespace xrpl {

// Vault transactions (withdraw, clawback, deposit, ValidVault) and the
// deletion of a pending loan on a FixedPrecision Vault that LoanPay late
// interest has coarsened past its base grid. The coarsening needs
// FixedPrecision LoanPay, so these live with it.
class VaultCoarsenedFixedPrecision_test : public LoanPayFixedPrecisionBase
{
    void
    testCoarsenedVaultWithdrawClawbackKeepAvailableGridDigit()
    {
        using namespace test::jtx;

        testcase(
            "Lending: VaultWithdraw and VaultClawback of an amount with a digit on AA's grid "
            "but not AT's succeed exactly");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupCoarsenIou(env, true);

        auto const coarsened = coarsenVault(env, owner, depositor, asset, {.overdueDays = 200});
        auto const& vaultKeylet = coarsened.fixture.vaultKeylet;
        expectCoarsened(env, coarsened);

        // Withdrawals need the Redemption phase.
        env.close(coarsened.fixture.redemptionDate + std::chrono::seconds{1});

        struct Row
        {
            Debit kind = Debit::Withdraw;
            char const* label = nullptr;
            // Off-grid at AA's base grid (1e-10).
            Number amount;
        };
        Row const rows[] = {
            {.kind = Debit::Withdraw, .label = "withdraw", .amount = Number{123'0000000007LL, -10}},
            {.kind = Debit::Clawback, .label = "clawback", .amount = Number{111'0000000003LL, -10}},
        };
        for (auto const& row : rows)
        {
            Number const delta =
                debitAvailable(env, coarsened, row.kind, issuer, depositor, asset, row.amount);
            // The share round-trip can trim the delta, but it must keep AA's finer grid
            // precision, not collapse to the AssetsTotal grid (-9).
            BEAST_EXPECT(delta > beast::kZero);
            BEAST_EXPECT(delta <= row.amount);
            BEAST_EXPECT(delta != roundToAsset(asset, delta, -9, Number::RoundingMode::Downward));
            auto const after = snapshotVault(env, vaultKeylet, asset);
            BEAST_EXPECT(after.available <= after.total);

            checkVaultLoanSums(
                env,
                coarsened,
                std::string{"testCoarsenedVaultWithdrawClawbackKeepAvailableGridDigit ("} +
                    row.label + ")");
        }
    }

    void
    testCoarsenedVaultSubUnitWithdrawClawbackSucceeds()
    {
        using namespace test::jtx;

        testcase(
            "Lending: VaultWithdraw and VaultClawback smaller than half a unit of the "
            "AssetsTotal cache's grid still succeed");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupCoarsenIou(env, true);

        auto const coarsened = coarsenVault(env, owner, depositor, asset, {.overdueDays = 200});
        auto const vaultSle0 = env.le(coarsened.fixture.vaultKeylet);
        if (!BEAST_EXPECT(vaultSle0))
            return;
        // The AssetsTotal cache grid (-9) is coarser than the base grid, and AA
        // itself still sits on the base grid (-10).
        BEAST_EXPECT(getVaultScale(vaultSle0) == -9);
        BEAST_EXPECT(getVaultBaseScale(vaultSle0) == -10);

        // Withdrawals need the Redemption phase.
        env.close(coarsened.fixture.redemptionDate + std::chrono::seconds{1});

        struct Row
        {
            Debit kind = Debit::Withdraw;
            char const* label = nullptr;
            // On AA's base grid (-10) but under half a unit of AssetsTotal's grid (5e-10).
            Number amount;
        };
        Row const rows[] = {
            {.kind = Debit::Withdraw, .label = "withdraw", .amount = Number{2, -10}},
            {.kind = Debit::Clawback, .label = "clawback", .amount = Number{3, -10}},
        };
        for (auto const& row : rows)
        {
            // Rounding Downward at -9 gives exactly zero.
            BEAST_EXPECT(
                roundToAsset(asset, row.amount, -9, Number::RoundingMode::Downward) ==
                beast::kZero);

            Number const delta =
                debitAvailable(env, coarsened, row.kind, issuer, depositor, asset, row.amount);
            // The delta may be trimmed by the share round-trip. It must stay strictly
            // positive and sub-(AT-grid); the bug rejected it outright.
            BEAST_EXPECT(delta > beast::kZero);
            BEAST_EXPECT(delta <= row.amount);
            BEAST_EXPECT(
                roundToAsset(asset, delta, -9, Number::RoundingMode::Downward) == beast::kZero);

            checkVaultLoanSums(
                env,
                coarsened,
                std::string{"testCoarsenedVaultSubUnitWithdrawClawbackSucceeds ("} + row.label +
                    ")");
        }
    }

    void
    testCoarsenedVaultDebitCrossesPowerOfTenDownward()
    {
        using namespace test::jtx;

        testcase(
            "Lending: VaultWithdraw and VaultClawback debits that cross a power of ten keep "
            "AssetsAvailable at 16 digits");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupCoarsenIou(env, true);

        auto const coarsened = coarsenVault(env, owner, depositor, asset, {.overdueDays = 200});
        auto const& vaultKeylet = coarsened.fixture.vaultKeylet;
        expectCoarsened(env, coarsened);

        // Withdrawals need the Redemption phase.
        env.close(coarsened.fixture.redemptionDate + std::chrono::seconds{1});

        // A margin, since the share round-trip can trim the delta short of the
        // requested amount.
        Number const margin{100};

        // The clawback runs on the post-withdraw vault, crossing a fresh boundary.
        struct Row
        {
            Debit kind = Debit::Withdraw;
            char const* label = nullptr;
        };
        Row const rows[] = {
            {.kind = Debit::Withdraw, .label = "withdraw"},
            {.kind = Debit::Clawback, .label = "clawback"},
        };
        for (auto const& row : rows)
        {
            Number const availableBefore = snapshotVault(env, vaultKeylet, asset).available;

            // The largest power of ten below AssetsAvailable; crossing it downward drops
            // a digit from the integer part.
            Number const powerOfTen = largestPowerOfTenAtMost(availableBefore);

            // One transaction spans the whole gap; the AssetsAvailable-delta check is
            // exact against the real transfer.
            Number const amount = availableBefore - powerOfTen + margin;
            BEAST_EXPECT(amount > beast::kZero);
            BEAST_EXPECT(amount < availableBefore);

            debitAvailable(env, coarsened, row.kind, issuer, depositor, asset, amount);

            BEAST_EXPECT(expectAvailableAtSixteenDigits(env, vaultKeylet, asset) < powerOfTen);

            checkVaultLoanSums(
                env,
                coarsened,
                std::string{"testCoarsenedVaultDebitCrossesPowerOfTenDownward ("} + row.label +
                    ")");
        }
    }

    void
    testValidVaultLargeSingleDebitAcrossAssetsTotalGridDoesNotFalsePositive()
    {
        using namespace test::jtx;

        testcase(
            "Lending: a single large VaultWithdraw/VaultClawback across the AssetsTotal grid "
            "boundary succeeds (ValidVault false-positive regression)");

        Number const margin{1'000};
        Number const powerOfTen{100'000};

        struct Row
        {
            Debit kind = Debit::Withdraw;
            char const* label = nullptr;
        };
        // Each debit runs on a fresh vault so AssetsTotal starts above its boundary
        // (after a shrink it would be dominated by AssetsDeployed).
        Row const rows[] = {
            {.kind = Debit::Withdraw, .label = "withdraw"},
            {.kind = Debit::Clawback, .label = "clawback"},
        };
        for (auto const& row : rows)
        {
            Env env(*this, features());
            auto const [issuer, owner, depositor, asset] = setupCoarsenIou(env, true);

            // Setup from the original report: AssetsAvailable 723562.1976154236,
            // AssetsDeployed 560000.
            auto const coarsened = coarsenVault(env, owner, depositor, asset, {.overdueDays = 200});
            auto const& vaultKeylet = coarsened.fixture.vaultKeylet;
            expectCoarsened(env, coarsened);

            env.close(coarsened.fixture.redemptionDate + std::chrono::seconds{1});

            auto const before = snapshotVault(env, vaultKeylet, asset);
            // The stored AssetsTotal crosses its own power-of-ten boundary, not
            // necessarily AssetsAvailable's, since AssetsDeployed adds magnitude.
            Number const powerOfTenAT = largestPowerOfTenAtMost(before.total);
            BEAST_EXPECT(before.total >= powerOfTenAT);

            // One debit takes AssetsAvailable below its boundary and carries AssetsTotal
            // across its own boundary in the same transaction.
            debitAvailable(
                env,
                coarsened,
                row.kind,
                issuer,
                depositor,
                asset,
                before.available - powerOfTen + margin);

            BEAST_EXPECT(snapshotVault(env, vaultKeylet, asset).total < powerOfTenAT);
            checkVaultLoanSums(
                env,
                coarsened,
                std::string{
                    "testValidVaultLargeSingleDebitAcrossAssetsTotalGridDoesNotFalsePositive ("} +
                    row.label + ")");
        }
    }

    void
    testDepositRefusedOnDebtCoarsenedVault()
    {
        using namespace test::jtx;

        testcase(
            "Lending: VaultDeposit is refused on a vault coarsened via AssetsDeployed while "
            "AssetsAvailable is still uncoarsened -- always via the Investment-phase gate, "
            "not the precision-loss check");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupCoarsenIou(env);

        auto const coarsened = coarsenVault(env, owner, depositor, asset, {.overdueDays = 200});
        auto const& vaultKeylet = coarsened.fixture.vaultKeylet;

        auto const vaultSle = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSle))
            return;
        BEAST_EXPECT(getVaultScale(vaultSle) == -9);
        BEAST_EXPECT(getVaultBaseScale(vaultSle) == -10);

        // Off-grid at the coarsened live scale (-9) but on AA's base grid (-10).
        Number const depositAmount{2, -10};
        BEAST_EXPECT(
            roundToAsset(asset, depositAmount, -9, Number::RoundingMode::Downward) == beast::kZero);

        // An AssetsDeployed-coarsened vault always has a LoanBroker, so it is closed-ended
        // and in the Investment phase while debt is outstanding. VaultDeposit refuses
        // there before the precision-loss check, so this amount fails with tecEXPIRED,
        // not tecPRECISION_LOSS. See the VaultBalance.* gtest cases for the bare-SLE
        // precision check.
        env(coarsened.fixture.vault.deposit(
                {.depositor = depositor, .id = vaultKeylet.key, .amount = asset(depositAmount)}),
            Ter(tecEXPIRED));
        env.close();

        checkVaultLoanSums(env, coarsened, "testDepositRefusedOnDebtCoarsenedVault");
    }

    void
    testPendingLoanDeleteOnCoarsenedVault()
    {
        using namespace test::jtx;

        testcase(
            "Lending: LoanDelete of a pending loan on a vault whose AssetsAvailable has "
            "coarsened credits the principal at the live grid and restores the vault");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupCoarsenIou(env);

        // A principal with a digit on the base grid (-10) that rounds up on the
        // coarsened grid (-9), so crediting it back to AssetsAvailable cannot be
        // exact. The proposal lands while the vault is still at its base grid,
        // then the late payment on the first loan coarsens AssetsAvailable itself.
        Number const pendingPrincipal{200'0000000006LL, -10};
        auto const coarsened = coarsenVault(
            env,
            owner,
            depositor,
            asset,
            {.extraLoans = {{.principal = pendingPrincipal, .pending = true}}});
        auto const& vaultKeylet = coarsened.fixture.vaultKeylet;
        auto const& pendingKeylet = coarsened.loanKeylets[1];
        expectCoarsened(env, coarsened);

        auto const vaultBefore = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultBefore))
            return;
        int const liveScale = getVaultScale(vaultBefore);
        BEAST_EXPECT(liveScale == -9);
        BEAST_EXPECT(scale(Number(vaultBefore->at(sfAssetsAvailable)), asset) == liveScale);
        BEAST_EXPECT(vaultBefore->at(sfAssetsReserved) == pendingPrincipal);
        if (auto const loan = env.le(pendingKeylet); BEAST_EXPECT(loan))
            BEAST_EXPECT(loan->isFlag(lsfLoanPending));

        auto const before = snapshotVault(env, vaultKeylet, asset);
        auto const ownerCountBefore = env.ownerCount(owner);

        env(loan::del(owner, pendingKeylet.key));
        env.close();
        BEAST_EXPECT(!env.le(pendingKeylet));

        auto const vaultAfter = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultAfter))
            return;
        auto const after = snapshotVault(env, vaultKeylet, asset);

        // AssetsReserved and AssetsDeployed are exact Numbers, so they unwind
        // exactly. AssetsAvailable is a 16-digit STAmount on the coarsened grid,
        // so the principal comes back rounded to that grid, which here means
        // rounded up: the credit exceeds the exact release. That is the case
        // the ValidVault LoanDelete check used to reject.
        BEAST_EXPECT(vaultAfter->at(sfAssetsReserved) == beast::kZero);
        BEAST_EXPECT(after.assetsDeployed == before.assetsDeployed - pendingPrincipal);
        Number const credited = after.available - before.available;
        BEAST_EXPECT(credited > pendingPrincipal);
        BEAST_EXPECT(
            credited ==
            roundToAsset(asset, pendingPrincipal, liveScale, Number::RoundingMode::ToNearest));
        // Nothing is reserved any more, so AssetsAvailable is the whole
        // pseudo-account balance again, exactly as if the loan had never been
        // proposed: the ledger rounded that balance at the same grid when the
        // late interest arrived.
        BEAST_EXPECT(after.available == after.vaultBalance);
        // The stored AssetsTotal is the 16-digit floor of AssetsAvailable plus
        // AssetsDeployed. While reserved, the principal's base-grid digit sat
        // in AssetsDeployed and was floored out of the cache; it returns in
        // AssetsAvailable rounded up, so the cache moves by exactly one live
        // unit. ValidVault tolerates that much and no more.
        Number const ulp{1, liveScale};
        BEAST_EXPECT(after.total == before.total + ulp);
        BEAST_EXPECT(env.ownerCount(owner) == ownerCountBefore - 1);

        checkVaultLoanSums(env, coarsened, "testPendingLoanDeleteOnCoarsenedVault");
    }

    void
    testDefaultOnlyActiveLoanWithPendingLoanOnCoarsenedVault()
    {
        using namespace test::jtx;
        using namespace loan;

        testcase(
            "Lending: defaulting the only active loan on a coarsened vault that still holds a "
            "pending loan leaves AssetsDeployed equal to AssetsReserved and passes ValidVault");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupCoarsenIou(env);

        // Same setup as the LoanDelete case: a pending loan whose principal has
        // a digit on the base grid (-10) that the coarsened grid (-9) cannot
        // hold, proposed before the late payment on the first loan coarsens
        // AssetsAvailable.
        Number const pendingPrincipal{200'0000000006LL, -10};
        auto const coarsened = coarsenVault(
            env,
            owner,
            depositor,
            asset,
            {.extraLoans = {{.principal = pendingPrincipal, .pending = true}}});
        auto const& vaultKeylet = coarsened.fixture.vaultKeylet;
        auto const& activeKeylet = coarsened.loanKeylets[0];
        auto const& pendingKeylet = coarsened.loanKeylets[1];
        expectCoarsened(env, coarsened);

        auto const vaultBefore = env.le(vaultKeylet);
        auto const activeBefore = env.le(activeKeylet);
        if (!BEAST_EXPECT(vaultBefore) || !BEAST_EXPECT(activeBefore))
            return;
        int const liveScale = getVaultScale(vaultBefore);
        BEAST_EXPECT(liveScale == -9);
        BEAST_EXPECT(vaultBefore->at(sfAssetsReserved) == pendingPrincipal);
        Number const activePrincipal = activeBefore->at(sfPrincipalOutstanding);
        BEAST_EXPECT(activePrincipal > beast::kZero);

        // The coarsening late payment left the active loan still overdue, so
        // the broker owner defaults it. With no cover deposited, nothing flows
        // back to the vault: AssetsDeployed just drops by the whole principal.
        env(manage(owner, activeKeylet.key, tfLoanDefault));
        env.close();

        auto const activeAfter = env.le(activeKeylet);
        auto const vaultAfter = env.le(vaultKeylet);
        if (!BEAST_EXPECT(activeAfter) || !BEAST_EXPECT(vaultAfter))
            return;
        BEAST_EXPECT(activeAfter->isFlag(lsfLoanDefault));
        BEAST_EXPECT(activeAfter->at(sfPrincipalOutstanding) == beast::kZero);
        if (auto const loan = env.le(pendingKeylet); BEAST_EXPECT(loan))
            BEAST_EXPECT(loan->isFlag(lsfLoanPending));

        // The pending loan's principal is now the only deployed principal, so
        // AssetsDeployed equals AssetsReserved exactly. The stored AssetsTotal
        // floors their base-grid digit away at the live grid, so it falls
        // short of AssetsAvailable + AssetsReserved. ValidVault must compare
        // the exact fields and not the cache, or this valid default would be
        // rejected with tecINVARIANT_FAILED.
        auto const after = snapshotVault(env, vaultKeylet, asset);
        BEAST_EXPECT(after.assetsDeployed == pendingPrincipal);
        BEAST_EXPECT(vaultAfter->at(sfAssetsReserved) == pendingPrincipal);
        BEAST_EXPECT(
            after.total ==
            roundToAsset(
                asset,
                after.available + pendingPrincipal,
                liveScale,
                Number::RoundingMode::Downward));
        BEAST_EXPECT(after.total < after.available + pendingPrincipal);

        checkVaultLoanSums(
            env, coarsened, "testDefaultOnlyActiveLoanWithPendingLoanOnCoarsenedVault");
    }

public:
    void
    run() override
    {
        testCoarsenedVaultWithdrawClawbackKeepAvailableGridDigit();
        testCoarsenedVaultSubUnitWithdrawClawbackSucceeds();
        testCoarsenedVaultDebitCrossesPowerOfTenDownward();
        testValidVaultLargeSingleDebitAcrossAssetsTotalGridDoesNotFalsePositive();
        testDepositRefusedOnDebtCoarsenedVault();
        testPendingLoanDeleteOnCoarsenedVault();
        testDefaultOnlyActiveLoanWithPendingLoanOnCoarsenedVault();
    }
};

BEAST_DEFINE_TESTSUITE(VaultCoarsenedFixedPrecision, app, xrpl);

}  // namespace xrpl
