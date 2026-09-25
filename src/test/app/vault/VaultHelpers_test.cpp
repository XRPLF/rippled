#include <test/jtx/Account.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace xrpl {

// True unit test of `clampToAssetsTotalScale`. The function under test only
// reads sfAsset and sfAssetsTotal from the vault SLE and never touches a
// ledger view or Rules, so a bare in-memory ltVAULT SLE is enough; there is
// no jtx::Env and no transaction submitted anywhere in this file.
//
// Number regime: this suite relies on the default thread_local Number
// mantissa range, which src/libxrpl/basics/Number.cpp initializes to
// Large330 (19-digit mantissa, post-fixCleanup3_3_0 cusp-rounding behavior):
//
//   thread_local std::reference_wrapper<MantissaRange const> Number::kRange =
//       MantissaRange::Access::mantissaRange(MantissaRange::MantissaScale::Large330);
//
// Unlike transaction processing, this test never constructs a ledger `Rules`
// object, so `STAmount::operator=(Number const&)` always takes its
// `!getCurrentTransactionRules()` branch and calls `fromNumber`, independent
// of amendment state. testProbeLarge330Regime() below asserts directly on a
// value that only round-trips exactly under Large330, pinning the regime
// rather than merely asserting it by comment.
class VaultHelpers_test : public beast::unit_test::Suite
{
private:
    // A single row of the clampToAssetsTotalScale table. `assetsTotal` and
    // `delta` must already be genuine, on-grid STAmount values for `asset`.
    struct Case
    {
        char const* name = nullptr;
        Number assetsTotal;
        Number delta;
        std::optional<Number> expected;  // nullopt means tecPRECISION_LOSS
    };

    // Builds a bare ltVAULT SLE with only sfAsset and sfAssetsTotal set,
    // mirroring what a transactor does: set the STNumber field, then call
    // associateAsset() so it is quantized to the asset's STAmount grid, the
    // same way VaultDeposit::doApply does for a real vault (see
    // src/libxrpl/tx/transactors/vault/VaultDeposit.cpp).
    static std::shared_ptr<SLE>
    makeVault(Asset const& asset, Number const& assetsTotal)
    {
        auto vault = std::make_shared<SLE>(keylet::vault(uint256(1)));
        vault->setFieldIssue(sfAsset, STIssue{sfAsset, asset});
        vault->at(sfAssetsTotal) = assetsTotal;
        associateAsset(*vault, asset);
        return vault;
    }

    // Runs every case in `cases` against `asset`, once per ambient rounding
    // mode. The function must give the same answer under all four modes,
    // and its answer must match the hand-derived `expected` value.
    template <std::size_t N>
    void
    runCases(Asset const& asset, std::array<Case, N> const& cases)
    {
        std::array<Number::RoundingMode, 4> const modes{
            Number::RoundingMode::ToNearest,
            Number::RoundingMode::Downward,
            Number::RoundingMode::Upward,
            Number::RoundingMode::TowardsZero};

        for (auto const& c : cases)
        {
            testcase(c.name);

            auto const vault = makeVault(asset, c.assetsTotal);
            BEAST_EXPECTS(
                Number(vault->at(sfAssetsTotal)) == c.assetsTotal,
                std::string(c.name) +
                    ": assetsTotal is not a genuine on-grid STAmount value (associateAsset "
                    "changed it)");

            STAmount const delta{asset, c.delta};
            BEAST_EXPECTS(
                Number(delta) == c.delta,
                std::string(c.name) + ": delta is not a genuine on-grid STAmount value");

            std::optional<std::expected<STAmount, TER>> reference;
            for (auto const mode : modes)
            {
                NumberRoundModeGuard const rg(mode);
                auto const result = clampToAssetsTotalScale(vault, delta);

                // The function must be insensitive to the caller's ambient
                // rounding mode: every mode must agree with the first one
                // tried.
                if (!reference)
                {
                    reference = result;
                }
                else
                {
                    BEAST_EXPECTS(
                        result.has_value() == reference->has_value(),
                        std::string(c.name) + ": result depends on ambient rounding mode");
                    if (result.has_value() && reference->has_value())
                    {
                        BEAST_EXPECTS(
                            *result == **reference,
                            std::string(c.name) + ": value depends on ambient rounding mode");
                    }
                    else if (!result.has_value() && !reference->has_value())
                    {
                        BEAST_EXPECTS(
                            result.error() == reference->error(),
                            std::string(c.name) + ": error depends on ambient rounding mode");
                    }
                }

                if (!c.expected)
                {
                    BEAST_EXPECTS(
                        !result.has_value(),
                        std::string(c.name) + ": expected tecPRECISION_LOSS, got success value " +
                            (result.has_value() ? result->getText() : std::string()));
                    if (!result.has_value())
                    {
                        BEAST_EXPECTS(
                            result.error() == tecPRECISION_LOSS,
                            std::string(c.name) + ": expected tecPRECISION_LOSS, got " +
                                transToken(result.error()));
                    }
                    continue;
                }

                STAmount const expected{asset, *c.expected};
                if (!BEAST_EXPECTS(
                        result.has_value(),
                        std::string(c.name) + ": expected success (" + expected.getText() +
                            "), got " + transToken(result.error())))
                {
                    continue;
                }

                BEAST_EXPECTS(
                    *result == expected,
                    std::string(c.name) + ": expected " + expected.getText() + ", got " +
                        result->getText());

                // The result must always be positive...
                BEAST_EXPECT(Number(*result) > Number{0});

                // ...and never larger in magnitude than the requested delta.
                BEAST_EXPECT(abs(Number(*result)) <= abs(c.delta));

                // For IOU rows, re-flooring the result on the posterior grid
                // must be a no-op: the result is already exactly
                // representable at that scale.
                //
                // For debits this holds directly at postScale, because the
                // result IS `roundToScale(magnitude, postScale, Downward)` by
                // construction. For credits the result is
                // `roundedPosterior - assetsTotal`, where roundedPosterior
                // sits exactly on the postScale grid but assetsTotal sits on
                // its own (possibly finer) natural grid; the difference of a
                // multiple of 10^postScale and a multiple of 10^assetsScale
                // is only guaranteed exact at the FINER of the two scales.
                // Row 7 below ("overcredit fix across a scale boundary") is
                // exactly this case: assetsTotal's own scale (-15) is finer
                // than postScale (-14), so checking exactness at postScale
                // alone fails even though the implementation is correct.
                if (!asset.integral())
                {
                    bool const isDebit = c.delta.mantissa() < 0;
                    Number const posterior =
                        isDebit ? c.assetsTotal - Number(*result) : c.assetsTotal + Number(*result);
                    int const postScale = scale(posterior, asset);
                    int const checkScale =
                        isDebit ? postScale : std::min(postScale, scale(c.assetsTotal, asset));
                    STAmount const reFloored =
                        roundToScale(*result, checkScale, Number::RoundingMode::Downward);
                    BEAST_EXPECTS(
                        reFloored == *result,
                        std::string(c.name) + ": result " + result->getText() +
                            " is not exact on the posterior grid (scale " +
                            std::to_string(checkScale) + ")");
                }
            }
        }
    }

    // Pins the Number mantissa regime this suite relies on. Under Large330,
    // a 19-digit mantissa (max 10^19-1) is exact where a legacy 16-digit
    // ("Small", max 10^16-1) regime would have to round it down to 16
    // significant digits, changing both mantissa and exponent.
    void
    testProbeLarge330Regime()
    {
        testcase("probe: default Number regime is Large330 (19-digit mantissa)");

        BEAST_EXPECT(Number::getMantissaScale() == MantissaRange::MantissaScale::Large330);

        // std::numeric_limits<std::int64_t>::max(), 19 significant digits.
        // This is already inside Large330's [10^18, 10^19-1] range, so
        // constructing it is a no-op; under "Small" it would have to lose
        // its low 3 digits.
        Number const probe{9'223'372'036'854'775'807LL, 0};
        BEAST_EXPECT(probe.mantissa() == 9'223'372'036'854'775'807LL);
        BEAST_EXPECT(probe.exponent() == 0);
    }

    // -------------------------------------------------------------------
    // IOU debits (delta negative).
    // -------------------------------------------------------------------
    void
    testIouDebits(Asset const& iou)
    {
        std::array<Case, 5> const cases{
            Case{
                // T = 1000000.000000005, delta = -1e-9.
                // Posterior = 1000000.000000004, still 16 significant
                // digits at exponent -9 (no rounding, no decade change).
                // postScale = -9. magnitude 1e-9 has its own exponent -24
                // (finer than -9), so it must be actually floored: 1e-9 is
                // exactly 1 ULP at scale -9, so flooring is a no-op.
                .name = "IOU debit: on-grid, same decade",
                .assetsTotal = Number{1'000'000'000'000'005LL, -9},
                .delta = Number{-1, -9},
                .expected = Number{1, -9},
            },
            Case{
                // T = 1000000, delta = -7.3e-10.
                // Posterior = 999999.99999999927 exactly (17 significant
                // digits: 15 nines, then "27"). Rounding to 16 digits
                // (ToNearest) rounds the trailing "...92.7" up to
                // "...93", giving mantissa 9999999999999993 at exponent
                // -10 -- postScale = -10, ONE DIGIT FINER than the naive
                // "posterior stays in T's decade at -9" guess, because
                // subtracting anything positive from an exact power-of-ten
                // total necessarily drops into the next lower decade
                // (1000000 has 7 integer digits, 999999.x has 6).
                // At scale -10 the ULP is 1e-10, and floor(7.3) = 7, so
                // the debit is NOT sub-ULP: it floors to 7e-10, not to
                // zero. See discrepancy note in the report.
                .name = "IOU debit: sub-ULP at the naive scale, but not at the true postScale",
                .assetsTotal = Number{1'000'000, 0},
                .delta = Number{-73, -11},
                .expected = Number{7, -10},
            },
            Case{
                // T = 1000000, delta = -5.3e-9.
                // Posterior = 999999.9999999947 exactly -- this needs only
                // 16 significant digits (14 nines, then "47"), so it is
                // exactly representable with NO rounding at exponent -10.
                // postScale = -10 (again one digit finer than T's own -9,
                // for the same power-of-ten-boundary reason as the row
                // above). At that grid 5.3e-9 is exactly 53 ULPs (integer),
                // so it floors to itself, unchanged.
                .name = "IOU debit: exact at the true (finer) postScale",
                .assetsTotal = Number{1'000'000, 0},
                .delta = Number{-53, -10},
                .expected = Number{53, -10},
            },
            Case{
                // T = 1.000000000000000, delta = -7.3e-16.
                // Posterior = 0.99999999999999927 exactly (17 significant
                // digits: 15 nines then "27"). Rounding to 16 digits
                // (ToNearest) gives mantissa 9999999999999993 at exponent
                // -16 -- postScale = -16. At that grid, 7.3e-16 is 7.3
                // ULPs (not integral), so it floors to 7e-16, not to
                // itself. See discrepancy note in the report.
                .name = "IOU debit: decade-crossing debit, floored (not exact) at finer grid",
                .assetsTotal = Number{1, 0},
                .delta = Number{-73, -17},
                .expected = Number{7, -16},
            },
            Case{
                // T = 1000000, delta = -999999.9999999999 (9.999999999999999e5).
                // Posterior = 0.0000000001 = 1e-10 exactly. postScale is
                // the exponent of 1e-10 as a canonical STAmount, i.e. -25 --
                // far finer than the magnitude's own exponent (-10).
                // roundToScale short-circuits ("value.exponent() >= scale")
                // and returns the magnitude unchanged.
                .name = "IOU debit: near-total debit, unchanged (finer postScale than magnitude)",
                .assetsTotal = Number{1'000'000, 0},
                .delta = Number{-9'999'999'999'999'999LL, -10},
                .expected = Number{9'999'999'999'999'999LL, -10},
            },
        };

        runCases(iou, cases);
    }

    // -------------------------------------------------------------------
    // IOU credits (delta positive).
    // -------------------------------------------------------------------
    void
    testIouCredits(Asset const& iou)
    {
        std::array<Case, 6> const cases{
            Case{
                // T = 1000000, delta = +2e-9. Posterior = 1000000.000000002,
                // exactly 16 significant digits at exponent -9
                // (postScale = -9, unchanged from T -- addition never
                // crosses below the 1e6 boundary the way subtraction does).
                // magnitude is already exact at that scale, so it passes
                // through unchanged.
                .name = "IOU credit: on-grid",
                .assetsTotal = Number{1'000'000, 0},
                .delta = Number{2, -9},
                .expected = Number{2, -9},
            },
            Case{
                // T = 9.999999999999999, delta = +5.
                // Exact posterior = 14.999999999999999 (17 significant
                // digits: "14" then 15 nines). postScale is computed under
                // ToNearest at the Number (19-digit) level: normalized
                // mantissa 1499999999999999900 (exponent -17) divided by
                // 1000 (to reach 16-digit IOU precision) gives
                // 1499999999999999.9, which rounds UP to 1500000000000000
                // -- i.e. exactly 15, at exponent -14. postScale = -14.
                // Downward-guarded posterior (exact, no rounding needed
                // since 17 digits < 19): 14.999999999999999. Flooring THAT
                // to 16 digits at scale -14 (Downward) gives
                // 1499999999999999 * 10^-14 = 14.99999999999999 (postScale
                // already matches the STAmount's own exponent, so no
                // further roundToScale is applied).
                // actualDelta = 14.99999999999999 - 9.999999999999999
                //             = 4.999999999999991.
                // This mirrors testBugVaultDepositOvercreditsAcrossScaleBoundary
                // in VaultBugs_test.cpp (same seed/deposit values), which
                // asserts post-fix `credited <= paid` rather than an exact
                // number; this row pins the exact value.
                .name = "IOU credit: overcredit fix across a scale boundary",
                .assetsTotal = Number{9'999'999'999'999'999LL, -15},
                .delta = Number{5, 0},
                .expected = Number{4'999'999'999'999'991LL, -15},
            },
            Case{
                // Finding-1 regression: T = 1000000, delta = +9.999999999999999e-10.
                // The exact sum needs ~25 significant digits (1000000 at
                // position 6, delta's last digit at position -25), far
                // beyond Number's 19-digit mantissa.
                //
                // postScale (computed under ToNearest): the digits of delta
                // that land within the 19-digit window (positions -10..-12,
                // "999") plus an all-nines remainder below position -12
                // round UP under ToNearest, carrying all the way through
                // the intervening zeros: the sum rounds to exactly
                // 1000000.000000001, i.e. postScale = -9.
                //
                // But the credit branch computes the *posterior* under a
                // Downward guard, not ToNearest: positions -10..-12 stay
                // "999" (no carry), giving posterior = 1000000.000000000999
                // exactly. Flooring that (Downward) to scale -9 truncates
                // the "999" entirely, landing back on exactly 1000000 --
                // i.e. the same as T. actualDelta = 0 => tecPRECISION_LOSS.
                // This is the ambient-rounding leak the Downward guard on
                // the credit-side sum exists to close; this row is a
                // regression test that the guard is doing its job.
                .name = "IOU credit: Finding-1 regression, ToNearest sum would overcredit",
                .assetsTotal = Number{1'000'000, 0},
                .delta = Number{9'999'999'999'999'999LL, -25},
                .expected = std::nullopt,
            },
            Case{
                // Same shape as the row above, but delta = +9.995e-10 is a
                // 19-digit half-even tie at the position-(-12) cusp: the
                // remainder below the retained "999" digits is exactly
                // 0.5 ULP, and ToNearest ties-to-even rounds the (odd) "9"
                // up, carrying the same way. Downward-guarded posterior
                // still truncates to "...000999" and floors back to T, so
                // the outcome is identical: tecPRECISION_LOSS.
                .name = "IOU credit: Finding-1 regression, 19-digit half-even tie",
                .assetsTotal = Number{1'000'000, 0},
                .delta = Number{9'995, -13},
                .expected = std::nullopt,
            },
            Case{
                // T = 0, delta = +3.7e-5. Posterior grid is delta's own
                // scale (postScale = -20, the canonical exponent of
                // 3.7e-5), so the magnitude is trivially unchanged.
                .name = "IOU credit: zero-total vault",
                .assetsTotal = Number{0},
                .delta = Number{37, -6},
                .expected = Number{37, -6},
            },
            Case{
                // T = 1000000, delta = +4e-10. Exact sum needs 17
                // significant digits (leading "1" at position 6, trailing
                // "4" at position -10); rounding to 16 digits drops the "4"
                // entirely (0.4 ULP at scale -9 rounds down under both
                // ToNearest and Downward), so postScale = -9 and the
                // Downward-guarded posterior floors straight back to T.
                // actualDelta = 0 => tecPRECISION_LOSS.
                .name = "IOU credit: sub-ULP credit",
                .assetsTotal = Number{1'000'000, 0},
                .delta = Number{4, -10},
                .expected = std::nullopt,
            },
        };

        runCases(iou, cases);
    }

    // -------------------------------------------------------------------
    // Integral assets (XRP, MPT): rounding is a no-op, magnitude is
    // returned unchanged and positive regardless of delta's sign. This is
    // a regression test for a signed-return bug: the function must not
    // hand back a negative delta for a debit.
    // -------------------------------------------------------------------
    void
    testIntegralAssets(Asset const& mpt, Asset const& xrp)
    {
        std::array<Case, 2> const mptCases{
            Case{
                .name = "MPT debit: magnitude is positive, not the signed delta",
                .assetsTotal = Number{1'000'000},
                .delta = Number{-5},
                .expected = Number{5},
            },
            Case{
                .name = "MPT credit: unchanged",
                .assetsTotal = Number{1'000'000},
                .delta = Number{7},
                .expected = Number{7},
            },
        };
        runCases(mpt, mptCases);

        std::array<Case, 2> const xrpCases{
            Case{
                .name = "XRP debit: magnitude is positive, not the signed delta",
                .assetsTotal = Number{100'000},
                .delta = Number{-3},
                .expected = Number{3},
            },
            Case{
                .name = "XRP credit: unchanged",
                .assetsTotal = Number{100'000},
                .delta = Number{10},
                .expected = Number{10},
            },
        };
        runCases(xrp, xrpCases);
    }

    // Builds the minimal vault + share-issuance pair that the share<->asset
    // conversions read: they only touch sfAsset, sfAssetsTotal, sfShareMPTID,
    // sfScale and sfLossUnrealized on the vault, plus sfOutstandingAmount on
    // the issuance. No ledger view and no Rules are involved.
    static std::pair<std::shared_ptr<SLE>, std::shared_ptr<SLE>>
    makeVaultAndIssuance(
        Asset const& asset,
        MPTID const& shareMPT,
        Number const& assetsTotal,
        std::uint64_t sharesTotal)
    {
        auto vault = std::make_shared<SLE>(keylet::vault(uint256(1)));
        vault->setFieldIssue(sfAsset, STIssue{sfAsset, asset});
        vault->at(sfShareMPTID) = shareMPT;
        vault->at(sfScale) = std::uint8_t(0);
        vault->at(sfAssetsTotal) = assetsTotal;
        vault->at(sfLossUnrealized) = Number(0);
        associateAsset(*vault, asset);

        auto issuance = std::make_shared<SLE>(keylet::mptokenIssuance(shareMPT));
        issuance->at(sfOutstandingAmount) = sharesTotal;
        return {vault, issuance};
    }

    // Common Prefix formal-verification finding: VaultDeposit charges the
    // depositor via sharesToAssetsDeposit, which quantized the exchange under
    // round-to-nearest. When the vault holds an integral asset (MPT/XRP) at a
    // non-unit exchange rate, the charge for freshly minted shares can be
    // rounded *down* below their fair value. The depositor then receives
    // shares worth more than the assets they pay in, diluting the existing
    // shareholders.
    //
    // Post-fixCleanup3_5_0 the charge rounds Upward (in the pool's favor), so
    // a depositor always pays at least the fair value of the shares they
    // receive and the assets-per-share price never decreases for existing
    // holders.
    //
    // Note this is only load-bearing for integral assets. For an IOU the
    // fixCleanup3_4_0 clamp in VaultDeposit::doApply re-floors the charge onto
    // the posterior sfAssetsTotal grid, whereas clampToAssetsTotalScale
    // returns integral magnitudes unchanged (see testIntegralAssets above).
    //
    // This exercises sharesToAssetsDeposit directly: the ToNearest overload is
    // the pre-amendment behavior, the Upward overload is the fix. For an
    // integral asset the difference is a full unit, so the undercharge is
    // unambiguous.
    void
    testDepositRoundingFavorsPool(MPTIssue const& assetMPT, MPTID const& shareMPT)
    {
        testcase("Deposit: share-to-asset rounding favors the pool");

        Asset const vaultAsset{assetMPT};

        // Charge for `minted` shares against a vault holding `assetsTotal`
        // integral assets backed by `sharesTotal` shares.
        auto const charge = [&](std::int64_t assetsTotal,
                                std::uint64_t sharesTotal,
                                std::uint64_t minted,
                                Number::RoundingMode mode) -> std::uint64_t {
            auto const [vault, issuance] =
                makeVaultAndIssuance(vaultAsset, shareMPT, Number(assetsTotal), sharesTotal);
            STAmount const shares{MPTIssue{shareMPT}, Number(minted)};
            auto const assets = sharesToAssetsDeposit(vault, issuance, shares, mode);
            BEAST_EXPECT(assets.has_value());
            return assets ? assets->mantissa() : 0;
        };

        // rate 100/3: charging 1 share is worth 33.33 assets.
        //   ToNearest -> 33 (undercharges: 33 * 3 = 99 < 100 -> dilution)
        //   Upward    -> 34 (favors pool: 34 * 3 = 102 >= 100)
        {
            std::uint64_t const nearest = charge(100, 3, 1, Number::RoundingMode::ToNearest);
            std::uint64_t const up = charge(100, 3, 1, Number::RoundingMode::Upward);
            BEAST_EXPECT(nearest == 33);
            BEAST_EXPECT(up == 34);
            // Pre-fix undercharges (dilutes); post-fix never does.
            BEAST_EXPECT(nearest * 3 < 100 * 1);
            BEAST_EXPECT(up * 3 >= 100 * 1);
        }

        // rate 100/3, 2 shares -> 66.67. ToNearest already rounds up here, so
        // both agree and neither dilutes: the fix only ever raises the charge.
        {
            std::uint64_t const nearest = charge(100, 3, 2, Number::RoundingMode::ToNearest);
            std::uint64_t const up = charge(100, 3, 2, Number::RoundingMode::Upward);
            BEAST_EXPECT(nearest == 67);
            BEAST_EXPECT(up == 67);
            BEAST_EXPECT(up * 3 >= 100 * 2);
        }

        // Exact rate: no rounding, the fix is a no-op.
        {
            std::uint64_t const nearest = charge(100, 4, 2, Number::RoundingMode::ToNearest);
            std::uint64_t const up = charge(100, 4, 2, Number::RoundingMode::Upward);
            BEAST_EXPECT(nearest == 50);
            BEAST_EXPECT(up == 50);
        }

        // A large, awkward rate: Upward is always the ceiling of the exact
        // fair value, so the pool is never shortchanged.
        //   fair = 1'000'003 * 7 / 999'983 = 7'000'021 / 999'983 = 7.00014...
        //   ToNearest -> 7 (7 * 999'983 = 6'999'881 < 7'000'021 -> dilution)
        //   Upward    -> 8 (8 * 999'983 = 7'999'864 >= 7'000'021)
        {
            std::int64_t const assetsTotal = 1'000'003;
            std::uint64_t const sharesTotal = 999'983;  // coprime-ish
            std::uint64_t const minted = 7;
            std::uint64_t const up =
                charge(assetsTotal, sharesTotal, minted, Number::RoundingMode::Upward);
            std::uint64_t const nearest =
                charge(assetsTotal, sharesTotal, minted, Number::RoundingMode::ToNearest);
            // up == ceil(assetsTotal * minted / sharesTotal)
            std::uint64_t const num = assetsTotal * minted;
            std::uint64_t const expectedUp = (num + sharesTotal - 1) / sharesTotal;
            BEAST_EXPECT(up == expectedUp);
            BEAST_EXPECT(up * sharesTotal >= num);      // no dilution
            BEAST_EXPECT(nearest * sharesTotal < num);  // pre-fix dilutes
        }
    }

    // Common Prefix formal-verification finding (mirror of the deposit case):
    // VaultWithdraw pays the withdrawing shareholder via
    // sharesToAssetsWithdraw, which quantized the exchange under
    // round-to-nearest. When the vault holds an integral asset (MPT/XRP) at a
    // non-unit exchange rate, the payout for burned shares can be rounded *up*
    // above their fair value. The shareholder then receives assets worth more
    // than the shares they burn, diluting the remaining shareholders.
    //
    // Post-fixCleanup3_5_0 the payout rounds Downward (in the pool's favor), so
    // a withdrawer always receives at most the fair value of the shares they
    // burn and the assets-per-share price never decreases for the holders who
    // remain.
    //
    // As with the deposit case this is only load-bearing for integral assets:
    // for an IOU the fixCleanup3_4_0 clamp in VaultWithdraw::doApply already
    // floors the payout onto the posterior sfAssetsTotal grid.
    //
    // This exercises sharesToAssetsWithdraw directly: the ToNearest overload is
    // the pre-amendment behavior, the Downward overload is the fix. For an
    // integral asset the difference is a full unit, so the overpayment is
    // unambiguous.
    void
    testWithdrawRoundingFavorsPool(MPTIssue const& assetMPT, MPTID const& shareMPT)
    {
        testcase("Withdraw: share-to-asset rounding favors the pool");

        Asset const vaultAsset{assetMPT};

        // Pay out for burning `burned` shares against a vault holding
        // `assetsTotal` integral assets backed by `sharesTotal` shares. The
        // vault carries no unrealized loss, so the waive flag is irrelevant.
        auto const payout = [&](std::int64_t assetsTotal,
                                std::uint64_t sharesTotal,
                                std::uint64_t burned,
                                Number::RoundingMode mode) -> std::uint64_t {
            auto const [vault, issuance] =
                makeVaultAndIssuance(vaultAsset, shareMPT, Number(assetsTotal), sharesTotal);
            STAmount const shares{MPTIssue{shareMPT}, Number(burned)};
            auto const assets =
                sharesToAssetsWithdraw(vault, issuance, shares, WaiveUnrealizedLoss::No, mode);
            BEAST_EXPECT(assets.has_value());
            return assets ? assets->mantissa() : 0;
        };

        // rate 100/3: burning 1 share is worth 33.33 assets.
        //   ToNearest -> 33 (fair: 33 * 3 = 99 <= 100, no dilution here)
        //   Downward  -> 33 (favors pool: 33 * 3 = 99 <= 100)
        {
            std::uint64_t const nearest = payout(100, 3, 1, Number::RoundingMode::ToNearest);
            std::uint64_t const down = payout(100, 3, 1, Number::RoundingMode::Downward);
            BEAST_EXPECT(nearest == 33);
            BEAST_EXPECT(down == 33);
            BEAST_EXPECT(down * 3 <= 100 * 1);  // pool never shortchanged
        }

        // rate 100/3, 2 shares -> 66.67. ToNearest rounds *up* to 67 and
        // overpays (67 * 3 = 201 > 200 -> dilution); Downward floors to 66.
        {
            std::uint64_t const nearest = payout(100, 3, 2, Number::RoundingMode::ToNearest);
            std::uint64_t const down = payout(100, 3, 2, Number::RoundingMode::Downward);
            BEAST_EXPECT(nearest == 67);
            BEAST_EXPECT(down == 66);
            // Pre-fix overpays (dilutes remaining holders); post-fix never does.
            BEAST_EXPECT(nearest * 3 > 100 * 2);
            BEAST_EXPECT(down * 3 <= 100 * 2);
        }

        // Exact rate: no rounding, the fix is a no-op.
        {
            std::uint64_t const nearest = payout(100, 4, 2, Number::RoundingMode::ToNearest);
            std::uint64_t const down = payout(100, 4, 2, Number::RoundingMode::Downward);
            BEAST_EXPECT(nearest == 50);
            BEAST_EXPECT(down == 50);
        }

        // A large, awkward rate whose fair value has a fractional part above
        // 0.5, so ToNearest rounds *up* and overpays. Downward is always the
        // floor of the exact fair value, so the remaining shareholders are
        // never shortchanged.
        //   fair = 1'000'003 * 3 / 7 = 3'000'009 / 7 = 428'572.71...
        //   ToNearest -> 428'573 (428'573 * 7 = 3'000'011 > 3'000'009 -> dilution)
        //   Downward  -> 428'572 (428'572 * 7 = 3'000'004 <= 3'000'009)
        {
            std::int64_t const assetsTotal = 1'000'003;
            std::uint64_t const sharesTotal = 7;  // coprime-ish
            std::uint64_t const burned = 3;
            std::uint64_t const down =
                payout(assetsTotal, sharesTotal, burned, Number::RoundingMode::Downward);
            std::uint64_t const nearest =
                payout(assetsTotal, sharesTotal, burned, Number::RoundingMode::ToNearest);
            // down == floor(assetsTotal * burned / sharesTotal)
            std::uint64_t const num = assetsTotal * burned;
            std::uint64_t const expectedDown = num / sharesTotal;
            BEAST_EXPECT(down == expectedDown);
            BEAST_EXPECT(down * sharesTotal <= num);    // no dilution
            BEAST_EXPECT(nearest * sharesTotal > num);  // pre-fix dilutes
        }
    }

public:
    void
    run() override
    {
        testProbeLarge330Regime();

        test::jtx::Account const issuer{"issuer"};
        Issue const iou{toCurrency("USD"), issuer.id()};
        MPTIssue const mpt{makeMptID(1, issuer.id())};
        Issue const xrp = xrpIssue();

        testIouDebits(iou);
        testIouCredits(iou);
        testIntegralAssets(mpt, xrp);

        MPTID const shareMPT = makeMptID(2, issuer.id());
        testDepositRoundingFavorsPool(mpt, shareMPT);
        testWithdrawRoundingFavorsPool(mpt, shareMPT);
    }
};

BEAST_DEFINE_TESTSUITE(VaultHelpers, app, xrpl);

}  // namespace xrpl
