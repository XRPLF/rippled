/** @file
 *  Out-of-line arithmetic and comparison operators for MPTAmount.
 *
 *  MPTAmount wraps a plain `int64_t` balance for Multi-Purpose Tokens.
 *  The operators here are intentionally branch-free: no overflow detection
 *  is performed.  Callers are expected to validate bounds through the ledger
 *  constraint machinery before mutating balances. The safe multiplication
 *  path (`mulRatio`, defined inline in MPTAmount.h) uses 128-bit
 *  intermediates and checks for overflow before converting.
 */
#include <xrpl/protocol/MPTAmount.h>

namespace xrpl {

/** Add `other` to this amount in place.
 *
 *  No overflow detection is performed; callers must ensure the result
 *  remains within `int64_t` range.
 *
 *  @param other The amount to add.
 *  @return Reference to this object after the addition.
 */
MPTAmount&
MPTAmount::operator+=(MPTAmount const& other)
{
    value_ += other.value();
    return *this;
}

/** Subtract `other` from this amount in place.
 *
 *  No overflow detection is performed; callers must ensure the result
 *  remains within `int64_t` range.
 *
 *  @param other The amount to subtract.
 *  @return Reference to this object after the subtraction.
 */
MPTAmount&
MPTAmount::operator-=(MPTAmount const& other)
{
    value_ -= other.value();
    return *this;
}

/** Return the arithmetic negation of this amount.
 *
 *  Used in transaction arithmetic where a credit and a debit are expressed
 *  as equal-magnitude amounts of opposite sign before being applied to the
 *  ledger.  Callers must ensure the magnitude is representable in `int64_t`
 *  (negating `INT64_MIN` is undefined behavior).
 *
 *  @return A new MPTAmount whose value is `-value_`.
 */
MPTAmount
MPTAmount::operator-() const
{
    return MPTAmount{-value_};
}

/** Test equality with another MPTAmount.
 *
 *  Together with `operator<`, this satisfies the requirements of
 *  `boost::totally_ordered`, from which `!=`, `>`, `<=`, and `>=` are
 *  synthesized.
 *
 *  @param other The amount to compare against.
 *  @return `true` if both amounts represent the same integer value.
 */
bool
MPTAmount::operator==(MPTAmount const& other) const
{
    return value_ == other.value_;
}

/** Test equality with a raw `int64_t` value.
 *
 *  Allows comparisons such as `amt == 0` without constructing a temporary
 *  MPTAmount.  `boost::equality_comparable<MPTAmount, std::int64_t>`
 *  synthesizes the mixed-type `!=` from this overload.
 *
 *  @param other The raw integer value to compare against.
 *  @return `true` if `value_` equals `other`.
 */
bool
MPTAmount::operator==(value_type other) const
{
    return value_ == other;
}

/** Return `true` if this amount is less than `other`.
 *
 *  The single total-order primitive from which `boost::totally_ordered`
 *  synthesizes `>`, `<=`, and `>=`.  Signed comparison gives correct
 *  semantics for negative balances: a negative MPT amount is less than zero.
 *
 *  @param other The amount to compare against.
 *  @return `true` if `value_` is strictly less than `other.value_`.
 */
bool
MPTAmount::operator<(MPTAmount const& other) const
{
    return value_ < other.value_;
}

/** Return the smallest positive MPT amount (one indivisible unit).
 *
 *  Equivalent to one drop of XRP for MPT balances.  This named factory
 *  exists so generic code that operates across `XRPAmount`, `IOUAmount`,
 *  and `MPTAmount` can call a uniform interface — for `IOUAmount` the
 *  smallest representable positive value requires more complex construction.
 *
 *  @return `MPTAmount{1}`.
 */
MPTAmount
MPTAmount::minPositiveAmount()
{
    return MPTAmount{1};
}

}  // namespace xrpl
