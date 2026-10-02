#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/tx/transactors/lending/LoanManage.h>

#include <gtest/gtest.h>
#include <helpers/Account.h>
#include <protocol/VaultTestHelpers.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <random>
#include <sstream>

namespace xrpl {
namespace {

// Minimal bare LoanBroker SLE carrying the fields calculateDefaultCover reads:
// CoverAvailable, DebtTotal, and the two cover rates. Not a valid ledger entry
// on its own -- just enough for the function's own field reads.
std::shared_ptr<SLE>
makeBroker(
    Asset const& asset,
    Number const& coverAvailable,
    Number const& debtTotal,
    TenthBips32 coverRateMinimum,
    TenthBips32 coverRateLiquidation)
{
    auto broker = std::make_shared<SLE>(keylet::loanBroker(uint256(2)));
    broker->at(sfCoverAvailable) = coverAvailable;
    broker->at(sfDebtTotal) = debtTotal;
    broker->at(sfCoverRateMinimum) = coverRateMinimum.value();
    broker->at(sfCoverRateLiquidation) = coverRateLiquidation.value();
    associateAsset(*broker, asset);
    return broker;
}

// Minimal bare Loan SLE carrying the only field calculateDefaultCover reads:
// PrincipalOutstanding.
std::shared_ptr<SLE>
makeLoan(Asset const& asset, Number const& principalOutstanding)
{
    auto loan = std::make_shared<SLE>(keylet::loan(uint256(3)));
    loan->at(sfPrincipalOutstanding) = principalOutstanding;
    associateAsset(*loan, asset);
    return loan;
}

// Replicates calculateDefaultCover's uncapped XLS-66 raw cover computation
// (same Upward rounding), so tests can independently verify the
// CoverAvailable/PrincipalOutstanding cap without duplicating
// calculateDefaultCover itself.
Number
rawCoverFor(
    Number const& debtTotal,
    TenthBips32 coverRateMinimum,
    TenthBips32 coverRateLiquidation,
    Number const& principalOutstanding)
{
    NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
    Number const minimumCover = tenthBipsOfValue(debtTotal, coverRateMinimum);
    return std::min(tenthBipsOfValue(minimumCover, coverRateLiquidation), principalOutstanding);
}

struct CoverCase
{
    Number assetsAvailable;
    Number coverAvailable;
    Number debtTotal;
    TenthBips32 coverRateMinimum;
    TenthBips32 coverRateLiquidation;
    Number principalOutstanding;
};

// Builds the ledger entries for one case, calls calculateDefaultCover, and
// checks every invariant defaultLoanFixedPrecision itself asserts right after
// the call: cover is non-negative, never exceeds the uncapped XLS-66 amount
// or the broker's CoverAvailable, leaves AssetsAvailable + cover exactly
// representable (STAmount round-trips to the same Number) whenever the cover
// can carry AssetsAvailable's digits, and rounds CoverAvailable - cover by at
// most half an ulp.
void
checkCoverCase(Asset const& asset, std::uint8_t scale, CoverCase const& in)
{
    auto const vault = makeVault(
        asset,
        in.assetsAvailable,
        VaultVersion::FixedPrecision,
        scale,
        Number{0},
        in.assetsAvailable);
    auto const broker = makeBroker(
        asset, in.coverAvailable, in.debtTotal, in.coverRateMinimum, in.coverRateLiquidation);
    auto const loan = makeLoan(asset, in.principalOutstanding);

    STAmount const cover = LoanManage::calculateDefaultCover(loan, broker, vault);

    Number const rawCover = rawCoverFor(
        in.debtTotal, in.coverRateMinimum, in.coverRateLiquidation, in.principalOutstanding);

    std::ostringstream context;
    context << "scale=" << int(scale) << " assetsAvailable=" << in.assetsAvailable
            << " coverAvailable=" << in.coverAvailable << " debtTotal=" << in.debtTotal
            << " coverRateMinimum=" << in.coverRateMinimum.value()
            << " coverRateLiquidation=" << in.coverRateLiquidation.value()
            << " principalOutstanding=" << in.principalOutstanding << " rawCover=" << rawCover
            << " cover=" << Number(cover);

    EXPECT_GE(Number(cover), Number{0}) << "cover < 0: " << context.str();
    EXPECT_LE(Number(cover), rawCover) << "cover exceeds uncapped XLS-66 amount: " << context.str();
    EXPECT_LE(Number(cover), in.coverAvailable)
        << "cover exceeds CoverAvailable: " << context.str();

    // AssetsAvailable + cover is exact unless AssetsAvailable has digits finer
    // than the 16-digit cover can carry; then no cover cancels them, and the
    // sum rounds by at most half an ulp of its posterior grid.
    Number const vaultSum = in.assetsAvailable + Number(cover);
    STAmount const vaultRounded{asset, vaultSum};
    bool const coverCanCarryAssetsAvailable =
        roundToAsset(
            asset,
            in.assetsAvailable,
            xrpl::scale(Number(cover), asset),
            Number::RoundingMode::TowardsZero) == in.assetsAvailable;
    if (coverCanCarryAssetsAvailable)
    {
        EXPECT_EQ(Number(vaultRounded), vaultSum)
            << "AssetsAvailable + cover not exactly 16-digit representable: " << context.str();
    }
    else
    {
        Number const vaultRoundingError = abs(Number(vaultRounded) - vaultSum);
        EXPECT_LE(vaultRoundingError, Number(5, xrpl::scale(Number(vaultRounded), asset) - 1))
            << "AssetsAvailable + cover rounding error exceeds half a posterior ulp: "
            << context.str();
    }

    // The LoanBroker absorbs the rounding: CoverAvailable - cover rounds to
    // nearest, so the error stays within half an ulp of its posterior grid.
    Number const brokerSum = in.coverAvailable - Number(cover);
    STAmount const brokerRounded{asset, brokerSum};
    Number const brokerRoundingError = abs(Number(brokerRounded) - brokerSum);
    EXPECT_LE(brokerRoundingError, Number(5, xrpl::scale(Number(brokerRounded), asset) - 1))
        << "CoverAvailable - cover rounding error exceeds half a posterior ulp: " << context.str();
}

}  // namespace

TEST(LoanDefaultCover, hand_picked_cases)
{
    test::Account const issuer{"issuer"};
    Issue const iou{toCurrency("USD"), issuer.id()};
    std::uint8_t const scale = 6;

    TenthBips32 const halfRate = percentageToTenthBips(50);
    TenthBips32 const fullRate = lending::kMaxCoverRate;

    // Open zone: AssetsAvailable and CoverAvailable both well under the Open
    // limit (9 * 10^9 at Scale 6); every grid involved stays at the fine
    // (scale-6) exponent.
    checkCoverCase(
        iou,
        scale,
        {.assetsAvailable = Number{1000},
         .coverAvailable = Number{5000},
         .debtTotal = Number{10000},
         .coverRateMinimum = halfRate,
         .coverRateLiquidation = halfRate,
         .principalOutstanding = Number{2000}});

    // CoverAvailable coarsened above the Open limit (20,000,000,000.00000,
    // 16 significant digits); AssetsAvailable stays small and fine.
    Number const coarseCoverAvailable{2'000'000'000'000'000, -5};
    checkCoverCase(
        iou,
        scale,
        {.assetsAvailable = Number{100},
         .coverAvailable = coarseCoverAvailable,
         .debtTotal = Number{1000},
         .coverRateMinimum = fullRate,
         .coverRateLiquidation = fullRate,
         .principalOutstanding = Number{500}});

    // AssetsAvailable one unit below a power of ten (9,999,999,999.999999,
    // 16 significant digits already); CoverAvailable stays small and fine.
    Number const coarseAssetsAvailable{9'999'999'999'999'999, -6};
    checkCoverCase(
        iou,
        scale,
        {.assetsAvailable = coarseAssetsAvailable,
         .coverAvailable = Number{50},
         .debtTotal = Number{1000},
         .coverRateMinimum = fullRate,
         .coverRateLiquidation = fullRate,
         .principalOutstanding = Number{30}});

    // Both AssetsAvailable and CoverAvailable coarsened.
    checkCoverCase(
        iou,
        scale,
        {.assetsAvailable = coarseAssetsAvailable,
         .coverAvailable = coarseCoverAvailable,
         .debtTotal = Number{1000},
         .coverRateMinimum = fullRate,
         .coverRateLiquidation = fullRate,
         .principalOutstanding = Number{900}});

    // Cover capped by CoverAvailable: the uncapped XLS-66 amount vastly
    // exceeds the broker's available cover.
    checkCoverCase(
        iou,
        scale,
        {.assetsAvailable = Number{1000},
         .coverAvailable = Number{1},
         .debtTotal = Number{1'000'000},
         .coverRateMinimum = fullRate,
         .coverRateLiquidation = fullRate,
         .principalOutstanding = Number{1'000'000}});

    // Cover capped by PrincipalOutstanding: DebtTotal times both rates would
    // exceed the Loan's own outstanding principal.
    checkCoverCase(
        iou,
        scale,
        {.assetsAvailable = Number{1000},
         .coverAvailable = Number{1'000'000},
         .debtTotal = Number{1'000'000},
         .coverRateMinimum = fullRate,
         .coverRateLiquidation = fullRate,
         .principalOutstanding = Number{5}});
}

TEST(LoanDefaultCover, integral_asset_cases)
{
    test::Account const issuer{"issuer"};
    TenthBips32 const fullRate = lending::kMaxCoverRate;

    for (Asset const asset : {Asset{xrpIssue()}, Asset{MPTIssue{makeMptID(1, issuer.id())}}})
    {
        // Integral assets have no fractional grid (Scale is unused), so the
        // same cover arithmetic must still land exactly for whole-unit
        // amounts.
        checkCoverCase(
            asset,
            0,
            {.assetsAvailable = Number{1000},
             .coverAvailable = Number{200},
             .debtTotal = Number{900},
             .coverRateMinimum = fullRate,
             .coverRateLiquidation = fullRate,
             .principalOutstanding = Number{150}});
    }
}

TEST(LoanDefaultCover, randomized_sweep)
{
    test::Account const issuer{"issuer"};
    Issue const iou{toCurrency("USD"), issuer.id()};

    // NOLINTNEXTLINE(bugprone-random-generator-seed): fixed seed for reproducible test
    std::mt19937 rng{20261002u};
    constexpr int kIterations = 5000;

    auto const pickScale = [&rng]() -> std::uint8_t {
        std::uniform_int_distribution<int> dist(0, kVaultMaximumFixedPrecisionIouScale);
        return static_cast<std::uint8_t>(dist(rng));
    };
    auto const pickRate = [&rng]() -> TenthBips32 {
        std::uniform_int_distribution<std::uint32_t> dist(0, lending::kMaxCoverRate.value());
        return TenthBips32{dist(rng)};
    };
    // Mantissa with 1 to 16 digits, biased toward landing exactly on a power
    // of ten or one unit below it (all nines) -- the cusps where coarsening
    // and carries are most likely to surface a bug. Always positive, since
    // these stand in for ledger balances.
    auto const pickMantissa = [&rng]() -> std::int64_t {
        std::uniform_int_distribution<int> digitsDist(1, 16);
        int const digits = digitsDist(rng);
        std::int64_t powerOfTen = 1;
        for (int i = 0; i < digits - 1; ++i)
            powerOfTen *= 10;

        std::uniform_int_distribution<int> shapeDist(0, 9);
        int const shape = shapeDist(rng);
        if (shape == 0)
            return powerOfTen;  // bare power of ten
        if (shape == 1)
            return (powerOfTen * 10) - 1;  // all nines, one digit up

        std::uniform_int_distribution<std::int64_t> mantissaDist(powerOfTen, (powerOfTen * 10) - 1);
        return mantissaDist(rng);
    };
    // A FixedPrecision Vault never holds a balance finer than its own Scale
    // (exponent must be >= -scale); minExponent enforces that so every
    // generated case is a state the real system could actually reach.
    auto const pickExponent = [&rng](int minExponent) -> int {
        std::uniform_int_distribution<int> dist(minExponent, minExponent + 25);
        return dist(rng);
    };
    auto const pickNumber = [&](int minExponent) -> Number {
        return Number{pickMantissa(), pickExponent(minExponent)};
    };

    for (int i = 0; i < kIterations; ++i)
    {
        std::uint8_t const scale = pickScale();
        int const minExponent = -static_cast<int>(scale);
        CoverCase const in{
            .assetsAvailable = pickNumber(minExponent),
            .coverAvailable = pickNumber(minExponent),
            .debtTotal = pickNumber(minExponent),
            .coverRateMinimum = pickRate(),
            .coverRateLiquidation = pickRate(),
            .principalOutstanding = pickNumber(minExponent)};

        checkCoverCase(iou, scale, in);
    }
}

}  // namespace xrpl
