#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Quality.h>

namespace xrpl {

/** Average quality of a payment strand expressed as a linear function of output.
 *
 *  Models the relationship `q(out) = m_ * out + b_`, where `q` is the average
 *  exchange rate (quality) that a strand delivers when it produces `out` units.
 *  This analytical model lets `StrandFlow::limitOut()` compute — without
 *  simulation — the maximum output the strand may produce before AMM price
 *  impact degrades the average quality below a caller-supplied limit.
 *
 *  **Derivation.**  For an AMM step with pool balances `poolGets` (input side)
 *  and `poolPays` (output side) and fee multiplier `cfee = 1 - tfee`, the
 *  constant-product swap formula gives:
 *  @code
 *      in = [(poolGets * poolPays) / (poolGets - out) - poolPays] / cfee
 *  @endcode
 *  Substituting into `q = out / in` and linearising yields:
 *  @code
 *      m = -cfee / poolGets   (always negative for a valid AMM step)
 *      b = poolPays * cfee / poolGets
 *  @endcode
 *
 *  **Multi-hop composition.**  For strands with sequential steps (e.g. a
 *  transfer-fee hop preceding an AMM hop), `combine()` chains two quality
 *  functions analytically.  `StrandFlow::limitOut()` calls `combine()` in a
 *  loop over all steps to accumulate a single QF representing the whole strand.
 *
 *  **Two construction modes** are selected via tag dispatch:
 *  - `AMMTag` — variable quality; slope and intercept derived from pool balances.
 *  - `CLOBLikeTag` — constant quality (`m_ = 0`); used for plain CLOB orders and
 *    for AMM offers in multi-path mode, where each path's AMM allocation is fixed.
 *
 *  @note The linear approximation is exact for *average* quality but not for
 *      instantaneous (marginal) quality, which is quadratic.  Using averages
 *      keeps composition algebraically simple while still providing a
 *      conservative, analytically tractable bound.
 *
 *  @see StrandFlow.h `limitOut()` — primary consumer of this class.
 */
class QualityFunction
{
private:
    /** Slope of the quality–output line (`-cfee / poolGets` for AMM; 0 for CLOB). */
    Number m_;
    /** Intercept of the quality–output line (`poolPays * cfee / poolGets` for AMM;
     *  `1 / quality.rate()` for CLOB). */
    Number b_;
    /** Cached constant quality; seated only when `m_ == 0` (CLOB-like function). */
    std::optional<Quality> quality_;

public:
    /** Tag type that selects the AMM constructor (variable-quality path step). */
    struct AMMTag
    {
    };
    /** Tag type that selects the CLOB-like constructor (constant-quality path step).
     *
     *  Used for both plain CLOB orders and AMM offers operating in multi-path
     *  mode, where the AMM offer size scales proportionally with quality just
     *  like a CLOB, making the effective quality constant from this sub-path's
     *  perspective.
     */
    struct CLOBLikeTag
    {
    };

    /** Construct a constant-quality (CLOB-like) quality function.
     *
     *  Sets `m_ = 0` and `b_ = 1 / quality.rate()`.  `quality_` is seated so
     *  that `isConst()` returns `true` and `StrandFlow::limitOut()` skips the
     *  output cap.
     *
     *  @param quality The fixed exchange rate of this path step.
     *  @throws std::runtime_error if `quality.rate()` is zero, which would
     *      make the intercept infinite.
     */
    QualityFunction(Quality const& quality, CLOBLikeTag);

    /** Construct a variable-quality (AMM) quality function from pool balances.
     *
     *  Derives the slope and intercept from the constant-product swap formula:
     *  @code
     *      m_ = -cfee / amounts.in
     *      b_ = amounts.out * cfee / amounts.in
     *  @endcode
     *  where `cfee = feeMult(tfee)`.  `quality_` is left empty; `isConst()`
     *  returns `false`.
     *
     *  @tparam TIn  Input amount type (e.g. `XRPAmount`, `IOUAmount`).
     *  @tparam TOut Output amount type.
     *  @param amounts Current AMM pool balances: `amounts.in` is the input-side
     *      pool depth, `amounts.out` is the output-side pool depth.
     *  @param tfee    AMM trading fee in the same units as `feeMult()` expects.
     *  @throws std::runtime_error if either pool balance is zero or negative,
     *      which would cause division-by-zero in downstream arithmetic.
     */
    template <typename TIn, typename TOut>
    QualityFunction(TAmounts<TIn, TOut> const& amounts, std::uint32_t tfee, AMMTag);

    /** Chain this quality function with the next path step's quality function.
     *
     *  Applies linear function composition in reciprocal-rate space:
     *  @code
     *      m_ += b_ * qf.m_;
     *      b_ *= qf.b_;
     *  @endcode
     *  If the combined slope becomes nonzero, `quality_` is cleared to reflect
     *  that the resulting function is no longer constant and `outFromAvgQ()`
     *  must be used rather than a simple pass/fail quality check.
     *
     *  @param qf Quality function for the next step to compose in.
     */
    void
    combine(QualityFunction const& qf);

    /** Solve for the maximum output at which average quality meets the given limit.
     *
     *  Inverts `q(out) = m_ * out + b_` by substituting `q = 1 / quality.rate()`:
     *  @code
     *      out = (1 / quality.rate() - b_) / m_
     *  @endcode
     *  The rounding mode is set to `Upward` during the calculation so the
     *  returned bound is conservative: because `m_` is negative, dividing an
     *  upward-rounded numerator by a negative slope yields a result that rounds
     *  down, ensuring the engine never requests marginally more output than the
     *  quality constraint allows.
     *
     *  Returns `std::nullopt` in three cases:
     *  - `m_ == 0`: the function is constant (CLOB-like); quality either passes
     *    or fails uniformly, so no output cap is meaningful.
     *  - `quality.rate() == 0`: guards against division-by-zero when forming
     *    `1 / rate`.
     *  - `out <= 0`: the quality limit cannot be achieved at any positive output;
     *    the strand is effectively dead for this constraint.
     *
     *  @param quality The minimum acceptable average exchange rate (quality limit).
     *  @return The output amount at which the strand's average quality equals
     *      `quality`, or `std::nullopt` if the cap is inapplicable or infeasible.
     */
    std::optional<Number>
    outFromAvgQ(Quality const& quality);

    /** Return `true` if this quality function is constant (CLOB-like).
     *
     *  A constant function has `m_ == 0`: the average quality is the same
     *  regardless of output size.  `StrandFlow::limitOut()` treats a constant
     *  function as a signal to skip the output cap and return `remainingOut`
     *  unchanged.
     */
    [[nodiscard]] bool
    isConst() const
    {
        return quality_.has_value();
    }

    /** Return the cached constant quality, if any.
     *
     *  Seated only when `isConst() == true` (i.e., this is a CLOB-like
     *  function constructed via `CLOBLikeTag`).  Returns `std::nullopt` for
     *  variable-quality (AMM) functions and for any combined function whose
     *  slope became nonzero after `combine()`.
     */
    [[nodiscard]] std::optional<Quality> const&
    quality() const
    {
        return quality_;
    }
};

template <typename TIn, typename TOut>
QualityFunction::QualityFunction(
    TAmounts<TIn, TOut> const& amounts,
    std::uint32_t tfee,
    QualityFunction::AMMTag)
{
    if (amounts.in <= beast::kZERO || amounts.out <= beast::kZERO)
        Throw<std::runtime_error>("QualityFunction amounts are 0.");
    Number const cfee = feeMult(tfee);
    m_ = -cfee / amounts.in;
    b_ = amounts.out * cfee / amounts.in;
}

}  // namespace xrpl
