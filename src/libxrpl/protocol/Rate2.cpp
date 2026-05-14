/** @file
 *  Implements transfer-rate arithmetic for IOU and NFT fees.
 *
 *  Defines `kPARITY_RATE` (the 1:1 identity rate) and provides the six
 *  multiply/divide helpers declared in Rate.h, plus the NFT basis-point
 *  conversion in the `nft` sub-namespace.
 */

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/STAmount.h>

#include <cstdint>

namespace xrpl {

/** The 1:1 transfer rate — sender pays exactly what the recipient receives.
 *
 *  Equal to `QUALITY_ONE` (10^9). Every arithmetic function short-circuits
 *  immediately when it sees this value, so payment paths with no issuer fee
 *  never enter the `STAmount` multiply/divide path.
 */
Rate const kPARITY_RATE(QUALITY_ONE);

namespace detail {

/** Bridge a `Rate` value into the `STAmount` type system as a dimensionless decimal.
 *
 *  Constructs an `STAmount` representing `rate.value × 10⁻⁹`, so that the
 *  existing `STAmount` multiply/divide infrastructure can apply a billion-scale
 *  integer rate without bespoke fixed-point arithmetic.  For example, a 1% fee
 *  (`rate.value == 1,010,000,000`) becomes the `STAmount` `1.010000000`.
 *
 *  @param rate  The transfer rate to convert; must be nonzero.
 *  @return An `STAmount` with `noIssue()` currency carrying the decimal value
 *      of `rate`.
 *  @note `noIssue()` signals that the result is dimensionless — it carries no
 *      currency identity and must not be stored directly in a ledger field.
 */
STAmount
asAmount(Rate const& rate)
{
    return {noIssue(), rate.value, -9, false};
}

}  // namespace detail

namespace nft {

/** Convert an NFT transfer fee in basis points to a billion-scale `Rate`.
 *
 *  NFT royalties are stored in the ledger as a `uint16_t` in basis points
 *  (hundredths of a percent, protocol maximum 50,000 = 50%).  The `Rate`
 *  type uses 10^9 as its unit, so the conversion factor is 10,000:
 *  `50,000 bp × 10,000 = 500,000,000`, which is safely below `QUALITY_ONE`
 *  and fits within `uint32_t`.
 *
 *  @param fee  NFT transfer fee in basis points (0–50,000); enforced by
 *      transaction validation before the value reaches the ledger, so no
 *      range assertion is performed here.
 *  @return A `Rate` suitable for passing to `multiply()` or `multiplyRound()`.
 *  @note Do not call this for ordinary IOU transfer rates — those are already
 *      stored in billion-scale form in `sfTransferRate` and should be wrapped
 *      in `Rate` directly.
 */
Rate
transferFeeAsRate(std::uint16_t fee)
{
    return Rate{static_cast<std::uint32_t>(fee) * 10000};
}

}  // namespace nft

/** Scale an amount by a transfer rate, preserving its asset.
 *
 *  Computes `amount × (rate / 10^9)`.  Returns `amount` unchanged when
 *  `rate == kPARITY_RATE` (the common case for accounts with no transfer fee),
 *  avoiding the `STAmount` arithmetic path entirely.
 *
 *  @param amount  The value to scale.
 *  @param rate    The transfer rate to apply; must be nonzero.
 *  @return The scaled `STAmount` denominated in the same asset as `amount`.
 *  @pre `rate.value != 0`; asserted in debug builds.
 */
STAmount
multiply(STAmount const& amount, Rate const& rate)
{
    XRPL_ASSERT(rate.value, "xrpl::nft::multiply : nonzero rate input");

    if (rate == kPARITY_RATE)
        return amount;

    return multiply(amount, detail::asAmount(rate), amount.asset());
}

/** Scale an amount by a transfer rate with controlled rounding, preserving its asset.
 *
 *  Like `multiply()`, but delegates to `mulRound` so the caller can specify
 *  rounding direction.  Used in IOU payment routing where fee calculations
 *  must stay in a single currency.
 *
 *  @param amount   The value to scale.
 *  @param rate     The transfer rate to apply; must be nonzero.
 *  @param roundUp  If `true`, round fractional results toward positive infinity;
 *      otherwise round toward zero.
 *  @return The scaled `STAmount` denominated in the same asset as `amount`.
 *  @pre `rate.value != 0`; asserted in debug builds.
 */
STAmount
multiplyRound(STAmount const& amount, Rate const& rate, bool roundUp)
{
    XRPL_ASSERT(rate.value, "xrpl::nft::multiplyRound : nonzero rate input");

    if (rate == kPARITY_RATE)
        return amount;

    return mulRound(amount, detail::asAmount(rate), amount.asset(), roundUp);
}

/** Scale an amount by a transfer rate with controlled rounding, emitting a specified asset.
 *
 *  Overload for offer-crossing and cross-currency paths where the output
 *  must be denominated in a different asset than the input.
 *
 *  @param amount   The value to scale.
 *  @param rate     The transfer rate to apply; must be nonzero.
 *  @param asset    The asset type of the returned `STAmount`.
 *  @param roundUp  If `true`, round fractional results toward positive infinity;
 *      otherwise round toward zero.
 *  @return The scaled `STAmount` denominated in `asset`.
 *  @pre `rate.value != 0`; asserted in debug builds.
 */
STAmount
multiplyRound(STAmount const& amount, Rate const& rate, Asset const& asset, bool roundUp)
{
    XRPL_ASSERT(rate.value, "xrpl::nft::multiplyRound(Issue) : nonzero rate input");

    if (rate == kPARITY_RATE)
    {
        return amount;
    }

    return mulRound(amount, detail::asAmount(rate), asset, roundUp);
}

/** Scale an amount by the inverse of a transfer rate, preserving its asset.
 *
 *  Computes `amount / (rate / 10^9)` — the inverse of `multiply()`.  Used when
 *  back-calculating the gross send amount needed to deliver a given net amount
 *  after fees.  Returns `amount` unchanged for `kPARITY_RATE`.
 *
 *  @param amount  The value to scale.
 *  @param rate    The transfer rate to invert; must be nonzero.
 *  @return The scaled `STAmount` denominated in the same asset as `amount`.
 *  @pre `rate.value != 0`; asserted in debug builds.
 */
STAmount
divide(STAmount const& amount, Rate const& rate)
{
    XRPL_ASSERT(rate.value, "xrpl::nft::divide : nonzero rate input");

    if (rate == kPARITY_RATE)
        return amount;

    return divide(amount, detail::asAmount(rate), amount.asset());
}

/** Scale an amount by the inverse of a transfer rate with controlled rounding, preserving its asset.
 *
 *  Like `divide()`, but delegates to `divRound` so the caller can specify
 *  rounding direction.  Used in IOU payment routing for single-currency
 *  gross-amount back-calculation.
 *
 *  @param amount   The value to scale.
 *  @param rate     The transfer rate to invert; must be nonzero.
 *  @param roundUp  If `true`, round fractional results toward positive infinity;
 *      otherwise round toward zero.
 *  @return The scaled `STAmount` denominated in the same asset as `amount`.
 *  @pre `rate.value != 0`; asserted in debug builds.
 */
STAmount
divideRound(STAmount const& amount, Rate const& rate, bool roundUp)
{
    XRPL_ASSERT(rate.value, "xrpl::nft::divideRound : nonzero rate input");

    if (rate == kPARITY_RATE)
        return amount;

    return divRound(amount, detail::asAmount(rate), amount.asset(), roundUp);
}

/** Scale an amount by the inverse of a transfer rate with controlled rounding, emitting a specified asset.
 *
 *  Overload for offer-crossing and cross-currency paths where the output
 *  must be denominated in a different asset than the input.
 *
 *  @param amount   The value to scale.
 *  @param rate     The transfer rate to invert; must be nonzero.
 *  @param asset    The asset type of the returned `STAmount`.
 *  @param roundUp  If `true`, round fractional results toward positive infinity;
 *      otherwise round toward zero.
 *  @return The scaled `STAmount` denominated in `asset`.
 *  @pre `rate.value != 0`; asserted in debug builds.
 */
STAmount
divideRound(STAmount const& amount, Rate const& rate, Asset const& asset, bool roundUp)
{
    XRPL_ASSERT(rate.value, "xrpl::nft::divideRound(Issue) : nonzero rate input");

    if (rate == kPARITY_RATE)
        return amount;

    return divRound(amount, detail::asAmount(rate), asset, roundUp);
}

}  // namespace xrpl
