/** @file
 *  Overflow-safe multiply-then-divide for 64-bit unsigned integers.
 *
 *  Naive evaluation of `(value * mul) / div` in 64-bit arithmetic silently
 *  overflows whenever the intermediate product exceeds `UINT64_MAX` (~1.8×10¹⁹),
 *  producing a wildly incorrect result with no indication of failure. This is
 *  a real hazard in XRPL fee scaling, where `value * mul` routinely exceeds
 *  that bound.
 *
 *  The implementation widens to `boost::multiprecision::uint128_t` before
 *  multiplying, so any product of two `uint64_t` values fits without overflow
 *  (max ≈ 3.4×10³⁸). Division is performed on the full-precision 128-bit value,
 *  preserving exact integer arithmetic. `boost::multiprecision` is used instead
 *  of the GCC-only `__uint128_t` extension to remain portable across MSVC.
 */
#include <xrpl/basics/mulDiv.h>

#include <boost/multiprecision/cpp_int.hpp>  // IWYU pragma: keep

#include <cstdint>
#include <optional>

namespace xrpl {

/** Compute `(value * mul) / div` using a 128-bit intermediate to prevent overflow.
 *
 *  The product `value * mul` is held in a 128-bit accumulator before dividing,
 *  ensuring no precision is lost before the final narrowing back to 64 bits.
 *  If the quotient still exceeds `kMULDIV_MAX` (i.e. `UINT64_MAX`), the result
 *  cannot be represented as a `uint64_t` and `std::nullopt` is returned instead
 *  of truncating silently.
 *
 *  @param value The base value to scale.
 *  @param mul   The numerator of the scaling ratio.
 *  @param div   The denominator of the scaling ratio. Behaviour is undefined
 *      if `div` is zero.
 *  @return The exact quotient as a `uint64_t`, or `std::nullopt` if the result
 *      exceeds `UINT64_MAX`.
 *  @note Callers that need an exception on overflow (e.g. `LoadFeeTrack`) should
 *      check the returned optional and throw themselves; this function
 *      intentionally keeps overflow policy out of the utility.
 *  @see xrpl::kMULDIV_MAX
 */
std::optional<std::uint64_t>
mulDiv(std::uint64_t value, std::uint64_t mul, std::uint64_t div)
{
    boost::multiprecision::uint128_t result;
    result = multiply(result, value, mul);

    result /= div;

    if (result > xrpl::kMULDIV_MAX)
        return std::nullopt;

    return static_cast<std::uint64_t>(result);
}

}  // namespace xrpl
