#include <test/app/lending/LoanPayFixedPrecisionBase.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/ter.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>

#include <chrono>
#include <cstdint>
#include <string>

namespace xrpl {

// Vault transactions (withdraw, clawback, deposit, ValidVault) on a
// FixedPrecision Vault that LoanPay late interest has coarsened past its base
// grid. The coarsening needs FixedPrecision LoanPay, so these live with it.
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
    testFinalWithdrawalRoundingCanExceedAvailable()
    {
        using namespace test::jtx;
        using namespace loan;

        testcase(
            "final VaultWithdraw of all shares succeeds when AssetsDeployed is zero, "
            "even if the shares-to-assets round-trip would round the payout above "
            "AssetsAvailable");

        // A non-power-of-ten share count together with an AssetsAvailable
        // pushed toward Number's 19-significant-digit "Large" mantissa
        // (active while LendingProtocol amendments are on) makes
        // (AssetsAvailable * shares) exceed 19 digits -- the same mechanism this
        // scenario's own near-INT64_MAX/3-share example exploits, but reached
        // here through ordinary doubling cycles instead of an unreachable
        // initial deposit (the Open-zone ceiling, getVaultOpenLimit,
        // caps any single VaultDeposit at 9e15 for an integral asset). A
        // closed-ended vault's investment window is capped at 30 years
        // (Protocol.h kMaxInvestmentPeriod), so the doubling loop is bounded
        // to under 30 one-year cycles; a larger initial deposit needs fewer
        // doublings to clear the 1e19 product threshold.
        constexpr std::uint64_t initialDeposit = 1'000'000;
        constexpr int cycles = 26;  // 1e6 * 2^26 ~ 6.7e13; * shares(1e6) ~ 6.7e19 > 1e19

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupMpt(
            env,
            {.maxAmt = 9'000'000'000'000'000'000ULL,
             .withDepositor = true,
             .holderFunds = 200'000'000'000'000'000ULL});

        // ~28.5 years, under the 30-year cap.
        std::chrono::seconds const investmentWindow{900'000'000};
        auto const fixture = setupLendingVault(
            env,
            owner,
            depositor,
            asset,
            Number{static_cast<std::int64_t>(initialDeposit)},
            investmentWindow);
        compoundVaultBySoleHolderLoanCycles(env, fixture, depositor, asset, cycles);

        // The loop above advances the clock cycles simulated years; make sure
        // it is also past RedemptionDate, which VaultWithdraw requires.
        env.close(fixture.redemptionDate + std::chrono::seconds{1});

        auto const vaultSle = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(vaultSle))
            return;
        BEAST_EXPECT(Number(vaultSle->at(sfAssetsDeployed)) == beast::kZero);
        std::uint64_t const available = toU64(vaultSle, sfAssetsAvailable);
        // available * initialDeposit > 1e19, checked as available > 1e19 / 1e6
        // to avoid overflowing uint64_t's ~1.84e19 range.
        std::uint64_t const minAvailable = 10'000'000'000'000ULL;  // > 1e13
        BEAST_EXPECT(available > minAvailable);

        auto const shareMPTID = vaultSle->at(sfShareMPTID);
        auto const issuance = env.le(keylet::mptokenIssuance(shareMPTID));
        if (!BEAST_EXPECT(issuance))
            return;
        std::uint64_t const outstanding = issuance->getFieldU64(sfOutstandingAmount);
        BEAST_EXPECT(outstanding == initialDeposit);

        // Vault is already well past RedemptionDate: the loop advanced the
        // clock by 26 simulated years (cycles). Name the full share count (not
        // an asset amount) so this is unambiguously a final withdrawal.
        STAmount const allShares{MPTIssue{shareMPTID}, Number(outstanding)};
        env(fixture.vault.withdraw(
                {.depositor = depositor, .id = fixture.vaultKeylet.key, .amount = allShares}),
            Ter(tesSUCCESS));
        env.close();

        auto const vaultSleAfter = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(vaultSleAfter))
            return;
        BEAST_EXPECT(toU64(vaultSleAfter, sfAssetsAvailable) == 0);

        checkVaultLoanSums(env, fixture, {}, "testFinalWithdrawalRoundingCanExceedAvailable");
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
        testFinalWithdrawalRoundingCanExceedAvailable();
    }
};

BEAST_DEFINE_TESTSUITE(VaultCoarsenedFixedPrecision, app, xrpl);

}  // namespace xrpl
