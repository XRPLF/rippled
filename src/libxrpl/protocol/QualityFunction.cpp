/** @file
 *  Implements `QualityFunction`: path average quality as a linear function
 *  of output amount, used by the single-path payment optimizer to find the
 *  maximum output that satisfies a caller-supplied quality limit without
 *  iterative approximation.
 */

#include <xrpl/protocol/QualityFunction.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/Quality.h>

#include <optional>
#include <stdexcept>

namespace xrpl {

/** Construct a constant-quality (CLOB-like) quality function.
 *
 *  For a CLOB offer, the exchange rate is independent of fill size, so the
 *  slope `m_` is zero and the intercept `b_` stores `1 / quality.rate()`.
 *  The same representation is used for AMM offers in multi-path mode, where
 *  the AMM's per-path allocation is fixed, making its quality effectively
 *  constant from this sub-path's perspective.
 *
 *  The intercept is stored as a reciprocal rate because `combine()` and
 *  `outFromAvgQ()` both operate in reciprocal-rate space.
 *
 *  @param quality The fixed quality (exchange rate) of the CLOB or
 *      multi-path AMM step.
 *  @throws std::runtime_error if `quality.rate()` is zero or negative,
 *      which would make `b_` infinite.
 */
QualityFunction::QualityFunction(Quality const& quality, QualityFunction::CLOBLikeTag)
    : m_(0), b_(0), quality_(quality)
{
    if (quality.rate() <= beast::kZERO)
        Throw<std::runtime_error>("QualityFunction quality rate is 0.");
    b_ = 1 / quality.rate();
}

/** Chain this quality function with the next hop's quality function.
 *
 *  Applies linear function composition in reciprocal-rate space:
 *  @code
 *      m_ += b_ * qf.m_;
 *      b_ *= qf.b_;
 *  @endcode
 *  This correctly accumulates the combined slope and intercept for a
 *  multi-hop strand such as XRP → USD → EUR.
 *
 *  The `quality_` cache is cleared to `std::nullopt` when the result
 *  has a nonzero slope (i.e., when any AMM step is present), signalling
 *  to `StrandFlow` that `outFromAvgQ()` is required rather than a simple
 *  constant-quality pass/fail check.
 *
 *  @param qf The quality function for the next step to compose in.
 */
void
QualityFunction::combine(QualityFunction const& qf)
{
    m_ += b_ * qf.m_;
    b_ *= qf.b_;
    if (m_ != 0)
        quality_ = std::nullopt;
}

/** Solve for the output amount whose average quality equals the given limit.
 *
 *  Inverts `q(out) = m_ * out + b_` by setting `q = 1 / quality.rate()` and
 *  solving: `out = (1/rate - b_) / m_`.  The result is the maximum output
 *  the path strand should produce before AMM price impact degrades the
 *  average quality below the caller's limit.
 *
 *  The rounding mode is set to `Upward` during the calculation so that the
 *  returned bound is conservative: the actual quality achieved will meet or
 *  exceed the limit.  `SaveNumberRoundMode` restores the prior mode on exit,
 *  preventing the upward-rounding from leaking into unrelated arithmetic.
 *
 *  Returns `std::nullopt` in three cases:
 *  - `m_ == 0`: the function is constant (CLOB-like); quality either passes
 *    or fails uniformly, so no output cap is meaningful.
 *  - `quality.rate() == zero`: guards against division by zero when forming
 *    `1 / rate`.
 *  - `out <= 0`: the quality limit is already unachievable at any positive
 *    output; the strand is effectively dead for this constraint.
 *
 *  @param quality The minimum acceptable average exchange rate (quality limit).
 *  @return The output amount at which average quality equals `quality`, or
 *      `std::nullopt` if the function is constant or the limit is infeasible.
 */
std::optional<Number>
QualityFunction::outFromAvgQ(Quality const& quality)
{
    if (m_ != 0 && quality.rate() != beast::kZERO)
    {
        SaveNumberRoundMode const rm(Number::setround(Number::RoundingMode::Upward));
        auto const out = (1 / quality.rate() - b_) / m_;
        if (out <= 0)
            return std::nullopt;
        return out;
    }
    return std::nullopt;
}

}  // namespace xrpl
