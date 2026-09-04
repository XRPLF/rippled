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
#include <expected>
#include <memory>
#include <optional>
#include <string>

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
        auto vault = std::make_shared<SLE>(keylet::vault(UInt256(1)));
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
    }
};

BEAST_DEFINE_TESTSUITE(VaultHelpers, app, xrpl);

}  // namespace xrpl
