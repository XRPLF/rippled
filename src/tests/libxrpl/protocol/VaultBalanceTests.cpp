#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <protocol/VaultTestHelpers.h>

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace xrpl {
namespace {

beast::Journal const kNullJournal{beast::Journal::getNullSink()};

// Snapshots every field adjustVaultBalances can touch, for atomicity checks.
struct BalanceSnapshot
{
    Number available;
    Number assetsDeployed;
    Number loss;
    Number yield;
    Number total;

    static BalanceSnapshot
    of(SLE::ConstRef vault)
    {
        return {
            .available = vault->at(sfAssetsAvailable),
            .assetsDeployed = vault->at(sfAssetsDeployed),
            .loss = vault->at(sfLossUnrealized),
            .yield = vault->at(sfYieldUnrealized),
            .total = vault->at(sfAssetsTotal),
        };
    }

    bool
    operator==(BalanceSnapshot const&) const = default;
};

class VaultBalance : public ::testing::Test
{
protected:
    test::Account const issuer_{"issuer"};
    Issue const iou_{toCurrency("USD"), issuer_.id()};

    // FixedPrecision IOU vault; stored is the (derived, hence possibly
    // stale) sfAssetsTotal cache.
    [[nodiscard]] std::shared_ptr<SLE>
    iouVault(
        Number const& assetsDeployed,
        Number const& available,
        Number const& stored = Number{0},
        std::uint8_t scale = 6) const
    {
        return makeVault(
            iou_, stored, VaultVersion::FixedPrecision, scale, assetsDeployed, available);
    }

    // AssetsDeployed 500, available 100.
    [[nodiscard]] std::shared_ptr<SLE>
    standardVault() const
    {
        return iouVault(Number{500}, Number{100});
    }

    [[nodiscard]] STAmount
    iouAmount(Number const& n) const
    {
        return STAmount{iou_, n};
    }

    // Zero cash change: syncs the cached sfAssetsTotal.
    static void
    syncTotal(std::shared_ptr<SLE> const& vault)
    {
        ASSERT_EQ(adjustVaultBalances(vault, {}, kNullJournal), tesSUCCESS);
    }

    // Above coarsening the cached sfAssetsTotal (Downward, 16 digits) is a
    // floor of the exact total: never above it, and within one unit of its
    // own live exponent.
    void
    expectCacheFloorsExactTotal(std::shared_ptr<SLE> const& vault) const
    {
        Number const exact = getAssetsTotal(vault);
        Number const cached = vault->at(sfAssetsTotal);
        EXPECT_LE(cached, exact);
        int const liveExponent = scale(cached, iou_);
        EXPECT_LE(exact - cached, (Number{1, liveExponent}));
    }

    // Adjusts asset's integral vault by +25 and by an overdraft.
    static void
    checkIntegralControl(Asset const& asset)
    {
        auto const vault =
            makeVault(asset, Number{0}, VaultVersion::FixedPrecision, 0, Number{500}, Number{100});

        ASSERT_EQ(
            adjustVaultBalances(vault, {.cash = STAmount{asset, Number{25}}}, kNullJournal),
            tesSUCCESS);
        EXPECT_EQ(vault->at(sfAssetsAvailable), Number{125});
        EXPECT_EQ(vault->at(sfAssetsTotal), getAssetsTotal(vault));

        EXPECT_EQ(
            adjustVaultBalances(vault, {.cash = STAmount{asset, Number{-1'000}}}, kNullJournal),
            tefBAD_LEDGER);
    }
};

// ---------------------------------------------------------------------------
// getAssetsTotal
// ---------------------------------------------------------------------------

TEST_F(VaultBalance, get_assets_total_stored_for_non_fixed_precision)
{
    struct Row
    {
        std::string_view name;
        VaultVersion version = VaultVersion::Legacy;
        std::int64_t stored = 0;
        std::int64_t available = 0;
    };
    std::vector<Row> const rows{
        {.name = "Legacy", .version = VaultVersion::Legacy, .stored = 1'234'567, .available = 999},
        {
            .name = "CashBasis",
            .version = VaultVersion::CashBasis,
            .stored = 9'876'543,
            .available = 100,
        },
    };

    for (auto const& row : rows)
    {
        SCOPED_TRACE(row.name);
        auto const vault =
            makeVault(iou_, Number{row.stored}, row.version, 6, Number{0}, Number{row.available});
        EXPECT_EQ(getAssetsTotal(vault), Number{row.stored});
        EXPECT_FALSE(vault->isFieldPresent(sfAssetsDeployed));
    }
}

TEST_F(VaultBalance, get_assets_total_fixed_precision_derived_sum)
{
    struct Row
    {
        std::string_view name;
        Number assetsDeployed;
        Number available;
        // The synced cache equals the exact total (not merely a floor of it).
        bool cacheExact = false;
    };
    std::vector<Row> const rows{
        {
            .name = "ExactSum",
            .assetsDeployed = Number{250'000},
            .available = Number{1'000'000},
            .cacheExact = true,
        },
        // available is already 16 significant digits; assetsDeployed pushes the exact
        // sum to 17, past what a 16-digit STAmount represents exactly.
        {
            .name = "ExactPastSixteenDigits",
            .assetsDeployed = Number{8, -6},
            .available = Number{9'999'999'999'999'999, -6},
        },
    };

    for (auto const& row : rows)
    {
        SCOPED_TRACE(row.name);
        auto const vault = iouVault(row.assetsDeployed, row.available);
        Number const exact = row.available + row.assetsDeployed;

        // Exact sum: no rounding, no conversion to a 16-digit STAmount.
        EXPECT_EQ(getAssetsTotal(vault), exact);
        EXPECT_GE(getAssetsTotal(vault), vault->at(sfAssetsAvailable));

        syncTotal(vault);
        if (row.cacheExact)
        {
            EXPECT_EQ(vault->at(sfAssetsTotal), getAssetsTotal(vault));
        }
        expectCacheFloorsExactTotal(vault);
    }
}

TEST_F(VaultBalance, get_assets_total_fixed_precision_integral_exact)
{
    auto const vault = makeVault(
        xrpIssue(), Number{0}, VaultVersion::FixedPrecision, 0, Number{250}, Number{1'000});
    EXPECT_EQ(getAssetsTotal(vault), Number{1'250});
}

TEST_F(VaultBalance, get_assets_total_ignores_stale_cache)
{
    Number const available{4'000'000};
    Number const assetsDeployed{1'000'000};
    auto const vault = iouVault(assetsDeployed, available, Number{99});
    EXPECT_EQ(getAssetsTotal(vault), available + assetsDeployed);
    EXPECT_NE(vault->at(sfAssetsTotal), getAssetsTotal(vault));
}

// The exact total, and the synced cache once Downward-rounded, must never
// fall below AssetsAvailable even when assetsDeployed coarsens the scale.
TEST_F(VaultBalance, get_assets_total_never_below_available)
{
    Number const available{9'999'999'999'999'999, -6};
    Number const assetsDeployed{5'000'000'000, -6};
    auto const vault = iouVault(assetsDeployed, available);
    EXPECT_GT(getVaultScale(vault), getVaultBaseScale(vault));
    EXPECT_GE(getAssetsTotal(vault), available);

    syncTotal(vault);
    EXPECT_GE(Number(vault->at(sfAssetsTotal)), available);
}

// available and assetsDeployed are each at most 16 digits, so associateAsset leaves them
// alone. Their sum, 99999999999999995 at exponent -4, has a 17th digit (5)
// that would round up under ToNearest and carry through the run of nines,
// bumping the exponent. Downward truncation drops it without a bump.
TEST_F(VaultBalance, live_scale_downward_keeps_lower_exponent_at_carry_boundary)
{
    auto vault = iouVault(Number{35, -4}, Number{9'999'999'999'999'996LL, -3});

    int const downwardExponent = [&] {
        NumberRoundModeGuard const rg(Number::RoundingMode::Downward);
        return scale(getAssetsTotal(vault), iou_);
    }();
    int const toNearestExponent = [&] {
        NumberRoundModeGuard const rg(Number::RoundingMode::ToNearest);
        return scale(getAssetsTotal(vault), iou_);
    }();
    EXPECT_LT(downwardExponent, toNearestExponent);
    EXPECT_EQ(getVaultScale(vault), downwardExponent);

    syncTotal(vault);
    EXPECT_EQ(scale(vault->at(sfAssetsTotal), iou_), downwardExponent);
    EXPECT_EQ(getVaultScale(vault), scale(vault->at(sfAssetsTotal), iou_));
}

TEST_F(VaultBalance, scale_and_open_include_assets_deployed)
{
    // Available alone stays at base scale; Available + AssetsDeployed coarsens.
    Number const available{1'000'000'000};
    Number const assetsDeployed{9'000'000'000};
    auto const vault = iouVault(assetsDeployed, available, available);

    EXPECT_EQ(getAssetsTotal(vault), available + assetsDeployed);
    EXPECT_GT(getVaultScale(vault), getVaultBaseScale(vault));

    // Open capacity uses the derived total, so assetsDeployed counts against the
    // ceiling: with assetsDeployed filling most of Open, the inflow is rejected.
    auto const nearOpen = iouVault(Number{1, 9}, Number{8, 9});
    EXPECT_EQ(checkOptionalVaultInflow(nearOpen, iouAmount(Number{1})), tecLIMIT_EXCEEDED);
}

// ---------------------------------------------------------------------------
// adjustVaultBalances
// ---------------------------------------------------------------------------

TEST_F(VaultBalance, fixed_precision_adjust_balances)
{
    Number const assetsDeployed{300};
    auto const vault = iouVault(assetsDeployed, Number{100}, Number{99});

    ASSERT_EQ(
        adjustVaultBalances(vault, {.cash = iouAmount(Number{50})}, kNullJournal), tesSUCCESS);
    EXPECT_EQ(vault->at(sfAssetsAvailable), Number{150});
    EXPECT_EQ(vault->at(sfAssetsDeployed), assetsDeployed);
    EXPECT_EQ(vault->at(sfAssetsTotal), getAssetsTotal(vault));
    EXPECT_EQ(getAssetsTotal(vault), Number{450});

    ASSERT_EQ(
        adjustVaultBalances(vault, {.cash = iouAmount(Number{-20})}, kNullJournal), tesSUCCESS);
    EXPECT_EQ(vault->at(sfAssetsAvailable), Number{130});
    EXPECT_EQ(vault->at(sfAssetsDeployed), assetsDeployed);
    EXPECT_EQ(vault->at(sfAssetsTotal), getAssetsTotal(vault));
    EXPECT_EQ(getAssetsTotal(vault), Number{430});

    // Clear both rails together, the way a final VaultWithdraw does.
    ASSERT_EQ(
        adjustVaultBalances(
            vault,
            {.cash = iouAmount(Number{-130}), .deployed = -Number(assetsDeployed)},
            kNullJournal),
        tesSUCCESS);
    EXPECT_EQ(vault->at(sfAssetsAvailable), Number{0});
    EXPECT_EQ(vault->at(sfAssetsDeployed), Number{0});
    EXPECT_EQ(vault->at(sfAssetsTotal), Number{0});
    EXPECT_EQ(getAssetsTotal(vault), Number{0});
}

TEST_F(VaultBalance, fixed_precision_add_above_coarsening_syncs)
{
    Number const assetsDeployed{5, -6};
    auto const vault = iouVault(assetsDeployed, Number{9'999'999'999'999'990, -6});

    ASSERT_EQ(
        adjustVaultBalances(vault, {.cash = iouAmount(Number{10, -6})}, kNullJournal), tesSUCCESS);
    EXPECT_EQ(vault->at(sfAssetsDeployed), assetsDeployed);
    expectCacheFloorsExactTotal(vault);
}

// For each (balance, delta), AssetsAvailable after adjustVaultBalances must
// equal the plain STAmount sum, the arithmetic the ledger uses for the real
// trust-line/MPT transfer.
TEST_F(VaultBalance, trust_line_arithmetic_mirror)
{
    struct Row
    {
        std::string_view name;
        Number balance;
        Number delta;
    };
    std::vector<Row> const rows{
        {.name = "Plain", .balance = Number{1'000'000}, .delta = Number{500'000}},
        {
            .name = "CreditCrossesPowerOfTenUp",
            .balance = Number{9'999'999'999'999'999, -9},
            .delta = Number{2, -9},
        },
        {
            .name = "DebitCrossesPowerOfTenDown",
            .balance = Number{1'000'000'000'000'000, -9},
            .delta = Number{-1, -9},
        },
        {.name = "DebitPartial", .balance = Number{5'000'000}, .delta = Number{-3'000'000}},
        {.name = "DebitToZero", .balance = Number{2'000'000}, .delta = Number{-2'000'000}},
        {
            .name = "CreditFinerThanBalanceGrid",
            .balance = Number{1'873'013'129'122'272LL, -9},
            .delta = Number{7, -10},
        },
    };

    for (auto const& row : rows)
    {
        SCOPED_TRACE(row.name);
        STAmount const balance{iou_, row.balance};
        STAmount const delta{iou_, row.delta};
        STAmount const expected{iou_, Number(balance) + Number(delta)};

        auto const vault = iouVault(Number{0}, row.balance, Number{0}, 9);
        ASSERT_EQ(adjustVaultBalances(vault, {.cash = delta}, kNullJournal), tesSUCCESS);
        EXPECT_EQ(Number(vault->at(sfAssetsAvailable)), Number(expected));
    }
}

// Each row starts from assetsDeployed 500, available 100.
TEST_F(VaultBalance, fields_move)
{
    struct Row
    {
        std::string_view name;
        std::int64_t cash = 0;
        std::int64_t deployed = 0;
        std::int64_t yield = 0;
        std::int64_t loss = 0;
        std::int64_t expectedAvailable = 100;
        std::int64_t expectedAssetsDeployed = 500;
        std::int64_t expectedYield = 0;
        std::int64_t expectedLoss = 0;
    };
    std::vector<Row> const rows{
        {.name = "CashAlone", .cash = 25, .expectedAvailable = 125},
        {.name = "AssetsDeployedAlone", .deployed = -200, .expectedAssetsDeployed = 300},
        {.name = "YieldAlone", .yield = 40, .expectedYield = 40},
        {.name = "LossAlone", .loss = 150, .expectedLoss = 150},
        {
            .name = "AllTogether",
            .cash = 40,
            .deployed = -100,
            .yield = 10,
            .loss = 50,
            .expectedAvailable = 140,
            .expectedAssetsDeployed = 400,
            .expectedYield = 10,
            .expectedLoss = 50,
        },
    };

    for (auto const& row : rows)
    {
        SCOPED_TRACE(row.name);
        auto const vault = standardVault();
        ASSERT_EQ(
            adjustVaultBalances(
                vault,
                {.cash = iouAmount(Number{row.cash}),
                 .deployed = Number{row.deployed},
                 .yield = Number{row.yield},
                 .loss = Number{row.loss}},
                kNullJournal),
            tesSUCCESS);
        EXPECT_EQ(vault->at(sfAssetsAvailable), Number{row.expectedAvailable});
        EXPECT_EQ(vault->at(sfAssetsDeployed), Number{row.expectedAssetsDeployed});
        EXPECT_EQ(vault->at(sfYieldUnrealized), Number{row.expectedYield});
        EXPECT_EQ(vault->at(sfLossUnrealized), Number{row.expectedLoss});
        EXPECT_EQ(vault->at(sfAssetsTotal), getAssetsTotal(vault));
    }
}

// A failing field rejects the whole change as a unit, including any valid
// cash leg. Each row starts from assetsDeployed 500, available 100.
TEST_F(VaultBalance, invalid_change_fails_atomically)
{
    struct Row
    {
        std::string_view name;
        std::int64_t cash = 0;
        std::int64_t deployed = 0;
        std::int64_t loss = 0;
        TER expected;
    };
    std::vector<Row> const rows{
        {.name = "AssetsAvailableNegative", .cash = -200, .expected = tefBAD_LEDGER},
        {.name = "AssetsDeployedNegative", .deployed = -600, .expected = tefBAD_LEDGER},
        // The +50 credit is valid on its own; the deployed change is not.
        {
            .name = "AssetsDeployedNegativeWithValidCash",
            .cash = 50,
            .deployed = -600,
            .expected = tefBAD_LEDGER,
        },
        // The -50 debit is valid on its own (available stays at 50); the
        // loss exceeds AssetsDeployed.
        {
            .name = "LossExceedsAssetsDeployedWithValidCash",
            .cash = -50,
            .loss = 600,
            .expected = tecLIMIT_EXCEEDED,
        },
        {.name = "LossUnrealizedNegative", .loss = -1, .expected = tefBAD_LEDGER},
        {.name = "LossExceedsAssetsDeployed", .loss = 600, .expected = tecLIMIT_EXCEEDED},
    };

    for (auto const& row : rows)
    {
        SCOPED_TRACE(row.name);
        auto const vault = standardVault();
        BalanceSnapshot const before = BalanceSnapshot::of(vault);

        EXPECT_EQ(
            adjustVaultBalances(
                vault,
                {.cash = iouAmount(Number{row.cash}),
                 .deployed = Number{row.deployed},
                 .loss = Number{row.loss}},
                kNullJournal),
            row.expected);
        EXPECT_EQ(BalanceSnapshot::of(vault), before);
    }
}

TEST_F(VaultBalance, loss_at_assets_deployed_boundary_allowed)
{
    auto const vault = standardVault();
    EXPECT_EQ(adjustVaultBalances(vault, {.loss = Number{500}}, kNullJournal), tesSUCCESS);
    EXPECT_EQ(vault->at(sfLossUnrealized), Number{500});
}

TEST_F(VaultBalance, negative_yield_unrealized_rejected)
{
    auto const vault = standardVault();
    vault->at(sfYieldUnrealized) = Number{5};
    auto const before = BalanceSnapshot::of(vault);

    EXPECT_EQ(adjustVaultBalances(vault, {.yield = Number{-20}}, kNullJournal), tefBAD_LEDGER);
    EXPECT_EQ(BalanceSnapshot::of(vault), before);
}

TEST_F(VaultBalance, assets_total_equals_floor16_of_available_plus_assets_deployed)
{
    Number const assetsDeployed{7, -9};
    auto vault = iouVault(assetsDeployed, Number{9'999'999'999'999'999, -9}, Number{0}, 9);

    syncTotal(vault);

    Number const expected = [&] {
        NumberRoundModeGuard const rg(Number::RoundingMode::Downward);
        return Number(STAmount{iou_, Number(vault->at(sfAssetsAvailable)) + assetsDeployed});
    }();
    EXPECT_EQ(Number(vault->at(sfAssetsTotal)), expected);
}

// ---------------------------------------------------------------------------
// Integral assets
// ---------------------------------------------------------------------------

TEST_F(VaultBalance, xrp_control)
{
    checkIntegralControl(xrpIssue());
}

TEST_F(VaultBalance, mpt_control)
{
    checkIntegralControl(MPTIssue{makeMptID(1, issuer_.id())});
}

TEST_F(VaultBalance, credit_to_posterior_scale_ignores_ambient_rounding_mode)
{
    // reference is on a fine grid (scale -10); raw needs a coarser atScale once
    // reference + raw is floored, so flooredSum - reference needs more digits
    // than fit in a 16-digit STAmount. creditToPosteriorScale must build that
    // difference under its own roundingMode (Downward here), not whatever
    // rounding mode happens to be ambient when it is called.
    Number const reference{4'218'667'505'995'833LL, -10};
    STAmount const raw{iou_, std::int64_t{4'004'426'170'440'612LL}, -8};
    int const atScale = -7;

    // The exact (pre-STAmount) Downward credit, computed independently, floors
    // strictly below raw: part of raw is lost rounding reference + raw onto
    // the coarser atScale grid.
    Number const exactDownwardCredit = [&] {
        NumberRoundModeGuard const rg(Number::RoundingMode::Downward);
        Number const flooredSum =
            roundToAsset(iou_, reference + Number(raw), atScale, Number::RoundingMode::Downward);
        return Number(STAmount{iou_, flooredSum - reference});
    }();
    EXPECT_LT(exactDownwardCredit, Number(raw));

    // Calling under an ambient ToNearest mode (the common case: no caller sets
    // an explicit guard around this call) must still produce the Downward
    // result, not a value rounded under the ambient mode.
    NumberRoundModeGuard const ambient(Number::RoundingMode::ToNearest);
    STAmount const credit = detail::creditToPosteriorScale(
        iou_, reference, atScale, raw, Number::RoundingMode::Downward);
    EXPECT_LT(Number(credit), Number(raw));
    EXPECT_EQ(Number(credit), exactDownwardCredit);
}

// creditToPosteriorAvailableScale returns
// floor16(AssetsAvailable + raw) - AssetsAvailable, never finer than -Scale.

TEST_F(VaultBalance, credit_to_posterior_available_scale_integral_passes_through)
{
    auto const vault = makeVault(xrpIssue(), Number{1'000}, VaultVersion::FixedPrecision);
    STAmount const credit =
        creditToPosteriorAvailableScale(vault, Number{250}, Number::RoundingMode::Downward);
    EXPECT_EQ(credit, STAmount(xrpIssue(), 250));
}

TEST_F(VaultBalance, credit_to_posterior_available_scale_floors_to_base_grid)
{
    // Scale 6: the base grid is 1e-6, and AssetsAvailable is small enough
    // that the posterior grid is the base grid.
    auto const vault = iouVault(Number{0}, Number{100});
    STAmount const credit = creditToPosteriorAvailableScale(
        vault, Number{12'345'678, -7}, Number::RoundingMode::Downward);
    EXPECT_EQ(credit, iouAmount(Number{1'234'567, -6}));
}

TEST_F(VaultBalance, credit_to_posterior_available_scale_floors_the_sum_across_power_of_ten)
{
    // 9999999999.999999 + 0.000011 crosses 10^10, where the 16-digit grid is
    // 1e-5. Flooring the delta alone would give 0.00001; flooring the sum
    // gives 10000000000.00001, so the credit is exactly 0.000011.
    auto const vault = iouVault(Number{0}, Number{9'999'999'999'999'999, -6});
    STAmount const credit =
        creditToPosteriorAvailableScale(vault, Number{11, -6}, Number::RoundingMode::Downward);
    EXPECT_EQ(credit, iouAmount(Number{11, -6}));
    EXPECT_EQ(
        STAmount(iou_, Number{9'999'999'999'999'999, -6} + Number(credit)),
        iouAmount(Number{1'000'000'000'000'001, -5}));
}

TEST_F(VaultBalance, credit_to_posterior_available_scale_uses_the_exact_raw_amount)
{
    // raw has 17 significant digits. Exact: 0.000009 + 12345678901.234561 =
    // 12345678901.23457 on the 1e-5 grid, so the credit is
    // 12345678901.234561, stored as 12345678901.23456. Rounding raw to 16
    // digits first (12345678901.23456) would floor the sum to
    // 12345678901.23456 and give a credit one unit lower, 12345678901.23455.
    auto const vault = iouVault(Number{0}, Number{9, -6});
    STAmount const credit = creditToPosteriorAvailableScale(
        vault, Number{12'345'678'901'234'561, -6}, Number::RoundingMode::Downward);
    EXPECT_EQ(credit, iouAmount(Number{1'234'567'890'123'456, -5}));
}

}  // namespace
}  // namespace xrpl
