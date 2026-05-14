/** @file
 *  Defines the `Rate` struct and its arithmetic free functions for applying
 *  XRPL transfer fees to `STAmount` values.
 *
 *  Transfer rates are billion-scale fractions: `1,000,000,000` is parity
 *  (no fee).  `kPARITY_RATE` is the sentinel for the fee-free common case;
 *  all six arithmetic functions short-circuit on it without entering the
 *  `STAmount` multiply/divide path.
 */

#pragma once

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/STAmount.h>

#include <boost/operators.hpp>

#include <cstdint>
#include <ostream>

namespace xrpl {

/** Protocol-level transfer rate, expressed as a fraction of one billion.
 *
 *  A value of `1,000,000,000` means 1:1 — no fee.  A value of
 *  `1,010,000,000` means the sender must deliver 1.01 units for every 1 unit
 *  the recipient receives (a 1% fee).  This scale matches `QUALITY_ONE` in
 *  `Quality.h`, tying transfer fees directly to the ledger's price
 *  representation.
 *
 *  `boost::totally_ordered<Rate>` generates `!=`, `>`, `<=`, and `>=` from
 *  the manually provided `==` and `<`, keeping the struct concise while
 *  remaining fully ordered.
 *
 *  @note The default constructor is deleted: a `Rate` with an unspecified
 *      value is meaningless, and zero would violate the nonzero precondition
 *      asserted by every arithmetic function in this header.  The constructor
 *      is `explicit` to prevent accidental implicit conversion from the
 *      large integers that rate values resemble.
 */
struct Rate : private boost::totally_ordered<Rate>
{
    /** The raw billion-scale rate value as stored in `sfTransferRate`. */
    std::uint32_t value;

    Rate() = delete;

    explicit Rate(std::uint32_t rate) : value(rate)
    {
    }
};

/** Returns `true` if both rates have the same billion-scale value. */
inline bool
operator==(Rate const& lhs, Rate const& rhs) noexcept
{
    return lhs.value == rhs.value;
}

/** Returns `true` if `lhs` is a strictly smaller rate than `rhs`. */
inline bool
operator<(Rate const& lhs, Rate const& rhs) noexcept
{
    return lhs.value < rhs.value;
}

/** Writes the raw billion-scale rate value to `os`. */
inline std::ostream&
operator<<(std::ostream& os, Rate const& rate)
{
    os << rate.value;
    return os;
}

/** Scale an amount by a transfer rate, preserving its asset.
 *
 *  Computes `amount × (rate / 10^9)`.  Returns `amount` unchanged when
 *  `rate == kPARITY_RATE`, avoiding the `STAmount` arithmetic path for the
 *  common fee-free case.
 *
 *  @param amount  The value to scale.
 *  @param rate    The transfer rate to apply; must be nonzero.
 *  @return The scaled `STAmount` denominated in the same asset as `amount`.
 *  @pre `rate.value != 0`; asserted in debug builds.
 */
STAmount
multiply(STAmount const& amount, Rate const& rate);

/** Scale an amount by a transfer rate with controlled rounding, preserving its asset.
 *
 *  Like `multiply()`, but the caller controls rounding direction.  Used in
 *  IOU payment routing where fee calculations stay in a single currency.
 *
 *  @param amount   The value to scale.
 *  @param rate     The transfer rate to apply; must be nonzero.
 *  @param roundUp  If `true`, round fractional results toward positive
 *      infinity; otherwise round toward zero.
 *  @return The scaled `STAmount` denominated in the same asset as `amount`.
 *  @pre `rate.value != 0`; asserted in debug builds.
 */
STAmount
multiplyRound(STAmount const& amount, Rate const& rate, bool roundUp);

/** Scale an amount by a transfer rate with controlled rounding, emitting a specified asset.
 *
 *  Overload for offer-crossing and cross-currency paths where the output
 *  must be denominated in a different asset than the input.
 *
 *  @param amount   The value to scale.
 *  @param rate     The transfer rate to apply; must be nonzero.
 *  @param asset    The asset type of the returned `STAmount`.
 *  @param roundUp  If `true`, round fractional results toward positive
 *      infinity; otherwise round toward zero.
 *  @return The scaled `STAmount` denominated in `asset`.
 *  @pre `rate.value != 0`; asserted in debug builds.
 */
STAmount
multiplyRound(STAmount const& amount, Rate const& rate, Asset const& asset, bool roundUp);

/** Scale an amount by the inverse of a transfer rate, preserving its asset.
 *
 *  Computes `amount / (rate / 10^9)` — the inverse of `multiply()`.  Used
 *  when back-calculating the gross send amount needed to deliver a given net
 *  amount after fees.  Returns `amount` unchanged for `kPARITY_RATE`.
 *
 *  @param amount  The value to scale.
 *  @param rate    The transfer rate to invert; must be nonzero.
 *  @return The scaled `STAmount` denominated in the same asset as `amount`.
 *  @pre `rate.value != 0`; asserted in debug builds.
 */
STAmount
divide(STAmount const& amount, Rate const& rate);

/** Scale an amount by the inverse of a transfer rate with controlled rounding, preserving its asset.
 *
 *  Like `divide()`, but the caller controls rounding direction.  Used in
 *  IOU payment routing for single-currency gross-amount back-calculation.
 *
 *  @param amount   The value to scale.
 *  @param rate     The transfer rate to invert; must be nonzero.
 *  @param roundUp  If `true`, round fractional results toward positive
 *      infinity; otherwise round toward zero.
 *  @return The scaled `STAmount` denominated in the same asset as `amount`.
 *  @pre `rate.value != 0`; asserted in debug builds.
 */
STAmount
divideRound(STAmount const& amount, Rate const& rate, bool roundUp);

/** Scale an amount by the inverse of a transfer rate with controlled rounding, emitting a specified asset.
 *
 *  Overload for offer-crossing and cross-currency paths where the output
 *  must be denominated in a different asset than the input.
 *
 *  @param amount   The value to scale.
 *  @param rate     The transfer rate to invert; must be nonzero.
 *  @param asset    The asset type of the returned `STAmount`.
 *  @param roundUp  If `true`, round fractional results toward positive
 *      infinity; otherwise round toward zero.
 *  @return The scaled `STAmount` denominated in `asset`.
 *  @pre `rate.value != 0`; asserted in debug builds.
 */
STAmount
divideRound(STAmount const& amount, Rate const& rate, Asset const& asset, bool roundUp);

namespace nft {

/** Convert an NFT transfer fee in basis points to a billion-scale `Rate`.
 *
 *  NFT royalties are stored as a `uint16_t` in basis points (0–50,000
 *  representing 0%–50%).  Because `Rate` uses `10^9` as its unit, the
 *  conversion multiplies by `10,000`: a maximum fee of `50,000 bp` becomes
 *  `500,000,000`, safely below `QUALITY_ONE` and within `uint32_t` range.
 *
 *  @param fee  NFT transfer fee in basis points (0–50,000); validated by
 *      transaction processing before reaching this function.
 *  @return A `Rate` suitable for passing to `multiply()` or `multiplyRound()`.
 *  @note Do not call this for ordinary IOU transfer rates — those are already
 *      billion-scale and should be wrapped in `Rate` directly.
 */
Rate
transferFeeAsRate(std::uint16_t fee);

}  // namespace nft

/** The 1:1 transfer rate — sender pays exactly what the recipient receives.
 *
 *  Equal to `QUALITY_ONE` (`1,000,000,000`).  Every arithmetic function in
 *  this header returns `amount` unchanged when it detects this value, so
 *  payment paths through accounts with no transfer fee never enter the
 *  `STAmount` multiply/divide path.  `transferRate()` returns this sentinel
 *  when an account's `sfTransferRate` field is absent.
 *
 *  @see transferRate() in AccountRootHelpers.h
 */
extern Rate const kPARITY_RATE;

}  // namespace xrpl
