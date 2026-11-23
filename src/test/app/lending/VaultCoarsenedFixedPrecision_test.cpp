#include <test/app/lending/LoanPayFixedPrecisionBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/sig.h>
#include <test/jtx/ter.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>

namespace xrpl {

// Vault transactions (withdraw, clawback, deposit, ValidVault) on a
// FixedPrecision Vault that LoanPay late interest has coarsened past its base
// grid. The coarsening needs FixedPrecision LoanPay, so these live with it.
class VaultCoarsenedFixedPrecision_test : public LoanPayFixedPrecisionBase
{
    // An MPT vault grown by late interest holds AssetsAvailable = AssetsTotal at 19 digits for
    // S = D shares and has no loan left. AT * S / S in 19-digit Numbers is AT +- 1, so a full
    // withdrawal or clawback used to ask for one unit more or less than the vault holds.
    //
    // 1. An MPT issuance, optionally with clawback, held by the depositor and the borrower.
    // 2. A closed-ended vault takes D from its only shareholder. Loan A lends all of D at
    //    InterestRate = 0 and LateInterestRate = 100% in 150 installments 60 seconds apart.
    // 3. Every instalment is paid two years late, which grows the vault.
    static constexpr std::uint32_t kLatePaymentTotal = 150;
    static constexpr std::uint32_t kLatePaymentInterval = 60;
    static constexpr std::uint32_t kLateGracePeriod = 60;
    static constexpr NetClock::duration kTwoYears{2 * 365 * 24 * 60 * 60};

    struct LateInterestMptVault
    {
        test::jtx::Account issuer;
        test::jtx::Account depositor;
        test::jtx::PrettyAsset asset;
        LendingFixture fixture;
    };

    static TER
    submitAndClose(test::jtx::Env& env, json::Value const& tx)
    {
        env(tx, test::jtx::Ter(std::ignore));
        TER const result = env.ter();
        env.close();
        return result;
    }

    std::optional<LateInterestMptVault>
    setupLateInterestMptVault(test::jtx::Env& env, Number const& deposit, bool clawback)
    {
        using namespace test::jtx;

        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Account const depositor{"depositor"};
        Account const borrower{"borrower"};
        env.fund(XRP(1'000'000), issuer, owner, depositor, borrower);
        env.close();

        MPTTester mpt{env, issuer, kMptInitNoFund};
        mpt.create({.flags = tfMPTCanTransfer | (clawback ? tfMPTCanClawback : 0u)});
        PrettyAsset const asset = mpt.issuanceID();
        mpt.authorize({.account = owner});
        mpt.authorize({.account = depositor});
        mpt.authorize({.account = borrower});
        env(pay(issuer, depositor, asset(deposit)));
        env(pay(issuer, borrower, asset(Number{2'000'000'000'000'000'000LL})));
        env.close();

        auto const fixture = setupLendingVault(
            env, owner, depositor, asset, deposit, std::chrono::seconds{24 * 60 * 60});

        // Loan A for all of D, with no interest and LateInterestRate = 100%.
        auto const brokerSle = env.le(fixture.brokerKeylet);
        if (!BEAST_EXPECT(brokerSle))
            return std::nullopt;
        Keylet const loanKeylet = keylet::loan(
            fixture.brokerKeylet.key, SeqProxy::rawSequence(brokerSle->at(sfLoanSequence)));
        env(loan::set(borrower, fixture.brokerKeylet.key, deposit),
            loan::kInterestRate(TenthBips32(0)),
            loan::kLateInterestRate(percentageToTenthBips(100)),
            loan::kGracePeriod(kLateGracePeriod),
            loan::kPaymentInterval(kLatePaymentInterval),
            loan::kPaymentTotal(kLatePaymentTotal),
            Sig(sfCounterpartySignature, fixture.owner),
            Fee(env.current()->fees().base * 2));
        env.close();

        auto const loanSle = env.le(loanKeylet);
        if (!BEAST_EXPECT(loanSle))
            return std::nullopt;
        NetClock::time_point const startDate{NetClock::duration{loanSle->at(sfStartDate)}};
        env.close(startDate + kTwoYears);
        for (std::uint32_t instalment = 1; instalment <= kLatePaymentTotal; ++instalment)
        {
            TER const payResult = submitAndClose(
                env,
                loan::pay(
                    borrower,
                    loanKeylet.key,
                    asset(Number{30'000'000'000'000'000LL}).value(),
                    tfLoanLatePayment));
            if (!BEAST_EXPECT(payResult == tesSUCCESS))
                return std::nullopt;
        }

        // The vault is in redemption once the investment window has passed.
        env.close(fixture.redemptionDate + std::chrono::seconds{1});
        return LateInterestMptVault{
            .issuer = issuer,
            .depositor = depositor,
            .asset = asset,
            .fixture = fixture,
        };
    }

    // The vault, its shares and the depositor's shares, all gone.
    void
    expectVaultEmptied(test::jtx::Env& env, LateInterestMptVault const& late, char const* what)
    {
        auto const vaultSle = env.le(late.fixture.vaultKeylet);
        if (!BEAST_EXPECT(vaultSle))
            return;
        auto const issuance = env.le(keylet::mptokenIssuance(vaultSle->at(sfShareMPTID)));
        if (!BEAST_EXPECT(issuance))
            return;
        BEAST_EXPECTS(
            Number{vaultSle->at(sfAssetsTotal)} == beast::kZero &&
                Number{vaultSle->at(sfAssetsAvailable)} == beast::kZero &&
                issuance->at(sfOutstandingAmount) == 0,
            std::string{what} + " should leave no assets and no shares");
    }

    void
    testFullWithdrawAfterLateInterest()
    {
        testcase("Lending: the sole holder of an MPT vault grown by late interest withdraws all");

        using namespace test::jtx;
        Env env{*this, features()};
        auto const late = setupLateInterestMptVault(env, Number{8'999'999'999'999'942LL}, false);
        if (!late)
            return;

        auto const vaultSle = env.le(late->fixture.vaultKeylet);
        if (!BEAST_EXPECT(vaultSle))
            return;
        MPTIssue const share{vaultSle->at(sfShareMPTID)};
        auto const issuance = env.le(keylet::mptokenIssuance(share.getMptID()));
        if (!BEAST_EXPECT(issuance))
            return;

        TER const result = submitAndClose(
            env,
            late->fixture.vault.withdraw({
                .depositor = late->depositor,
                .id = late->fixture.vaultKeylet.key,
                .amount = STAmount{share, issuance->at(sfOutstandingAmount)},
            }));
        BEAST_EXPECTS(
            result == tesSUCCESS,
            "withdrawing every share should empty the vault, but it returned " +
                transToken(result));
        expectVaultEmptied(env, *late, "a full withdrawal");
    }

    void
    testFullClawbackAfterLateInterest()
    {
        testcase("Lending: a full clawback of an MPT vault grown by late interest");

        using namespace test::jtx;
        // The second deposit is the one whose AT * S / S is AT - 1 instead of AT + 1.
        for (std::int64_t const deposit : {8'999'999'999'999'942LL, 8'999'999'999'999'916LL})
        {
            Env env{*this, features()};
            auto const late = setupLateInterestMptVault(env, Number{deposit}, true);
            if (!late)
                return;

            TER const result = submitAndClose(
                env,
                late->fixture.vault.clawback({
                    .issuer = late->issuer,
                    .id = late->fixture.vaultKeylet.key,
                    .holder = late->depositor,
                    .amount = late->asset(Number{0}).value(),
                }));
            BEAST_EXPECTS(
                result == tesSUCCESS,
                "clawing back every share of a vault of " + std::to_string(deposit) +
                    " should empty the vault, but it returned " + transToken(result));
            expectVaultEmptied(env, *late, "a full clawback");
        }
    }

    // Pins the payout of a share withdrawal at a fractional share price. There is no pre-fix run:
    // a FixedPrecision vault cannot be created without fixCleanup3_6_0.
    void
    testWithdrawSharesRoundsDownToBaseScale()
    {
        testcase(
            "Lending: a share withdrawal from an IOU vault grown by interest pays out on the "
            "base grid, rounded down");

        using namespace test::jtx;
        Env env{*this, features()};
        auto const [issuer, owner, depositor, asset] =
            setupIou(env, {.depositorTrust = 1'100, .ownerTrust = 1'000});

        // Scale 6: 1,000 USD mints 1,000,000,000 shares and the base grid is 10^-6.
        auto const fixture = setupLendingVault(
            env, owner, depositor, asset, Number{1'000}, std::chrono::seconds{1'000'000}, 6);
        auto const loanKeylet =
            openLoan(env, fixture, Number{1'000}, 1, percentageToTenthBips(10), 0, 60 * 60);
        env(loan::pay(depositor, loanKeylet.key, asset(1'001).value()));
        env.close();
        env.close(fixture.redemptionDate + std::chrono::seconds{1});

        auto const vaultBefore = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(vaultBefore))
            return;
        MPTIssue const share{vaultBefore->at(sfShareMPTID)};
        // Share balances are read through the vault's pseudo-account, the share issuer.
        env.memoize(Account{"vault", vaultBefore->at(sfAccount)});
        auto const issuance = env.le(keylet::mptokenIssuance(share.getMptID()));
        if (!BEAST_EXPECT(issuance))
            return;
        // One hour of 10% interest on 1,000 grew the vault to 1,000.011416 for 10^9 shares.
        Number const totalBefore = vaultBefore->at(sfAssetsTotal);
        Number const grownTotal{1'000'011'416, -6};
        BEAST_EXPECT(totalBefore == grownTotal);
        BEAST_EXPECT(issuance->at(sfOutstandingAmount) == 1'000'000'000);
        Number const balanceBefore = env.balance(depositor, asset).number();

        // 50,000 shares are worth 1,000.011416 * 50,000 / 10^9 = 0.0500005708. On the 10^-6 grid
        // that rounds down to 0.050000; to nearest it would be 0.050001.
        std::uint64_t const sharesRedeemed = 50'000;
        env(fixture.vault.withdraw({
            .depositor = depositor,
            .id = fixture.vaultKeylet.key,
            .amount = STAmount{share, sharesRedeemed},
        }));
        env.close();

        auto const vaultAfter = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(vaultAfter))
            return;
        Number const paid = env.balance(depositor, asset).number() - balanceBefore;
        Number const debited = totalBefore - Number{vaultAfter->at(sfAssetsTotal)};
        Number const payout{5, -2};
        BEAST_EXPECTS(paid == payout, "paid " + to_string(paid));
        BEAST_EXPECTS(debited == payout, "AssetsTotal fell by " + to_string(debited));
        BEAST_EXPECT(env.balance(depositor, share).number() == Number{1'000'000'000 - 50'000});
    }

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

public:
    void
    run() override
    {
        testCoarsenedVaultWithdrawClawbackKeepAvailableGridDigit();
        testCoarsenedVaultSubUnitWithdrawClawbackSucceeds();
        testCoarsenedVaultDebitCrossesPowerOfTenDownward();
        testValidVaultLargeSingleDebitAcrossAssetsTotalGridDoesNotFalsePositive();
        testDepositRefusedOnDebtCoarsenedVault();
        testFullWithdrawAfterLateInterest();
        testFullClawbackAfterLateInterest();
        testWithdrawSharesRoundsDownToBaseScale();
    }
};

BEAST_DEFINE_TESTSUITE(VaultCoarsenedFixedPrecision, app, xrpl);

}  // namespace xrpl
