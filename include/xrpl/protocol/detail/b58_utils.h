#pragma once

#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/detail/token_errors.h>

#include <boost/outcome.hpp>
#include <boost/outcome/result.hpp>

#include <span>
#include <system_error>
#include <tuple>

namespace xrpl {

/** Idiomatic return type for the Base58 codec.
 *
 *  Pairs a success value of type `T` with a `std::error_code` drawn from
 *  `TokenCodecErrc`. Using `boost::outcome` rather than exceptions keeps the
 *  codec allocation-free and gives callers fine-grained error inspection
 *  without try/catch overhead.
 *
 *  @tparam T The success value type.
 */
template <class T>
using Result = boost::outcome_v2::result<T, std::error_code>;

#ifndef _MSC_VER

/** @file
 *  Arithmetic primitives for XRPL's fast Base58 codec.
 *
 *  The fast path achieves 10–15× the throughput of the reference
 *  Bitcoin-derived algorithm by using base 58¹⁰ as an intermediate
 *  representation. Because 58¹⁰ = 430,804,206,899,405,824 fits in a
 *  `uint64_t`, the codec operates on far fewer, much larger "digits",
 *  dramatically reducing inner-loop iterations.
 *
 *  All functions in this namespace rely on GCC/Clang's `unsigned __int128`
 *  extension. The entire namespace is therefore excluded on MSVC (`#ifndef
 *  _MSC_VER`); `tokens.cpp` falls back to the reference implementation on
 *  Windows.
 */
namespace b58_fast::detail {

/** Compute quotient and remainder in a single hardware divide instruction.
 *
 *  A combined `divRem` call allows the compiler to emit one `divq`
 *  instruction rather than two, avoiding the cost of a redundant division.
 *
 *  @param a The dividend.
 *  @param b The divisor.
 *  @return A tuple `{a / b, a % b}`.
 */
[[nodiscard]] inline std::tuple<std::uint64_t, std::uint64_t>
divRem(std::uint64_t a, std::uint64_t b)
{
    return {a / b, a % b};
}

/** Multiply two 64-bit values and add a carry term without overflow.
 *
 *  Uses `unsigned __int128` as a 128-bit intermediate so that GCC/Clang can
 *  fuse the operation into a single `mulq` instruction. This is the
 *  per-limb step of hardware long-multiplication, used by
 *  `inplaceBigintMul` to scale a multi-precision integer by a scalar.
 *
 *  @param a     First multiplicand.
 *  @param b     Second multiplicand.
 *  @param carry Carry value accumulated from a previous limb (may be zero).
 *  @return A tuple `{low64, high64}` where `low64` is the new coefficient
 *      (the low 64 bits of the 128-bit product) and `high64` is the carry
 *      to propagate to the next limb.
 */
[[nodiscard]] inline std::tuple<std::uint64_t, std::uint64_t>
carryingMul(std::uint64_t a, std::uint64_t b, std::uint64_t carry)
{
    unsigned __int128 const x = a;
    unsigned __int128 const y = b;
    unsigned __int128 const c = (x * y) + carry;
    return {c & 0xffff'ffff'ffff'ffff, c >> 64};
}

/** Add two 64-bit values and detect carry into the 65th bit.
 *
 *  Uses `unsigned __int128` as a 128-bit intermediate so that GCC/Clang
 *  generates a single `addq`/`adcq` pair. Used by `inplaceBigintAdd` to
 *  ripple carry through the limbs of a multi-precision integer.
 *
 *  @param a First addend.
 *  @param b Second addend.
 *  @return A tuple `{sum & 0xFFFF…F, carry}` where `carry` is 0 or 1.
 */
[[nodiscard]] inline std::tuple<std::uint64_t, std::uint64_t>
carryingAdd(std::uint64_t a, std::uint64_t b)
{
    unsigned __int128 const x = a;
    unsigned __int128 const y = b;
    unsigned __int128 const c = x + y;
    return {c & 0xffff'ffff'ffff'ffff, c >> 64};
}

/** Add a `uint64_t` scalar to a multi-precision big integer in place.
 *
 *  The big integer is stored as a `span` of `uint64_t` limbs in
 *  **little-endian limb order**: `a[0]` holds the 2⁰ coefficient,
 *  `a[n]` holds the 2⁶⁴ⁿ coefficient. The scalar is added to `a[0]` and
 *  any resulting carry is rippled forward through subsequent limbs.
 *
 *  Early return is the common case: once carry becomes zero the function
 *  stops walking the span, so short carries (spanning one or two limbs) are
 *  essentially free.
 *
 *  @param a The big integer to modify, with limbs in ascending power order.
 *      Must contain at least 2 elements (one data limb + one carry limb).
 *  @param b The scalar value to add.
 *  @return `TokenCodecErrc::Success` on success.
 *      `TokenCodecErrc::InputTooSmall` if `a.size() <= 1`.
 *      `TokenCodecErrc::OverflowAdd` if carry propagates past the end of
 *      `a`; this represents a programming invariant violation — valid
 *      Base58 inputs are never large enough to trigger it.
 *  @note `[[nodiscard]]` — the caller must inspect the return value.
 */
[[nodiscard]] inline TokenCodecErrc
inplaceBigintAdd(std::span<std::uint64_t> a, std::uint64_t b)
{
    if (a.size() <= 1)
    {
        return TokenCodecErrc::InputTooSmall;
    }

    std::uint64_t carry = 0;
    std::tie(a[0], carry) = carryingAdd(a[0], b);

    for (auto& v : a.subspan(1))
    {
        if (carry == 0u)
        {
            return TokenCodecErrc::Success;
        }
        std::tie(v, carry) = carryingAdd(v, 1);
    }
    if (carry != 0u)
    {
        return TokenCodecErrc::OverflowAdd;
    }
    return TokenCodecErrc::Success;
}

/** Multiply a multi-precision big integer by a `uint64_t` scalar in place.
 *
 *  Limbs are stored in **little-endian limb order** (same convention as
 *  `inplaceBigintAdd`). The function multiplies every limb except the last
 *  by `b`, propagating carry forward. The final carry is written into
 *  `a[last_index]`, which must be zero on entry — it acts as a reserved
 *  overflow slot.
 *
 *  `tokens.cpp` always passes a span one element larger than the live
 *  portion so that this overflow slot is always available.
 *
 *  @param a The big integer to scale in place. `a[a.size()-1]` must be zero
 *      on entry; it receives the overflow carry on exit.
 *  @param b The scalar multiplier.
 *  @return `TokenCodecErrc::Success` on success.
 *      `TokenCodecErrc::InputTooSmall` if `a` is empty.
 *      `TokenCodecErrc::InputTooLarge` if `a[last]` is non-zero on entry,
 *      indicating the value would overflow the allocated limb array.
 *  @note `[[nodiscard]]` — the caller must inspect the return value.
 */
[[nodiscard]] inline TokenCodecErrc
inplaceBigintMul(std::span<std::uint64_t> a, std::uint64_t b)
{
    if (a.empty())
    {
        return TokenCodecErrc::InputTooSmall;
    }

    auto const lastIndex = a.size() - 1;
    if (a[lastIndex] != 0)
    {
        return TokenCodecErrc::InputTooLarge;
    }

    std::uint64_t carry = 0;
    for (auto& coeff : a.subspan(0, lastIndex))
    {
        std::tie(coeff, carry) = carryingMul(coeff, b, carry);
    }
    a[lastIndex] = carry;
    return TokenCodecErrc::Success;
}

/** Divide a multi-precision big integer in place by a scalar, returning the remainder.
 *
 *  Performs long division from the most significant limb down, maintaining a
 *  running `prevRem`. At each step it forms a 128-bit dividend
 *  `(prevRem << 64) | numerator[i]` and divides it by `divisor` using an
 *  inner lambda that keeps the `unsigned __int128` logic localized.
 *  `XRPL_ASSERT` guards verify that neither quotient nor remainder
 *  overflows 64 bits (guaranteed by the invariant that `prevRem < divisor`
 *  at every step).
 *
 *  Limbs are stored in **little-endian limb order**: `numerator[0]` is the
 *  2⁰ coefficient, `numerator[n]` is the 2⁶⁴ⁿ coefficient. On return,
 *  `numerator` is overwritten with the quotient in the same layout.
 *
 *  This is the workhorse of the encoding path: `tokens.cpp` calls it in a
 *  loop to extract successive base-58¹⁰ coefficients from the binary
 *  representation of the value being encoded.
 *
 *  @param numerator The big integer to divide, modified in place to hold
 *      the quotient. An empty span is treated as zero (returns 0).
 *  @param divisor   The scalar divisor. Behavior is undefined if zero.
 *  @return The remainder of the division (`numerator % divisor`).
 */
[[nodiscard]] inline std::uint64_t
inplaceBigintDivRem(std::span<uint64_t> numerator, std::uint64_t divisor)
{
    if (numerator.empty())
    {
        // should never happen, but if it does then it seems natural to define
        // the a null set of numbers to be zero, so the remainder is also zero.
        // LCOV_EXCL_START
        UNREACHABLE(
            "xrpl::b58_fast::detail::inplaceBigintDivRem : empty "
            "numerator");
        return 0;
        // LCOV_EXCL_STOP
    }

    auto toU128 = [](std::uint64_t high, std::uint64_t low) -> unsigned __int128 {
        unsigned __int128 const high128 = high;
        unsigned __int128 const low128 = low;
        return ((high128 << 64) | low128);
    };
    auto divRe64 = [](unsigned __int128 num,
                      std::uint64_t denom) -> std::tuple<std::uint64_t, std::uint64_t> {
        unsigned __int128 const denom128 = denom;
        unsigned __int128 const d = num / denom128;
        unsigned __int128 const r = num - (denom128 * d);
        XRPL_ASSERT(
            d >> 64 == 0,
            "xrpl::b58_fast::detail::inplaceBigintDivRem::divRe64 : "
            "valid division result");
        XRPL_ASSERT(
            r >> 64 == 0,
            "xrpl::b58_fast::detail::inplaceBigintDivRem::divRe64 : "
            "valid remainder");
        return {static_cast<std::uint64_t>(d), static_cast<std::uint64_t>(r)};
    };

    std::uint64_t prevRem = 0;
    int const lastIndex = numerator.size() - 1;
    std::tie(numerator[lastIndex], prevRem) = divRem(numerator[lastIndex], divisor);
    for (int i = lastIndex - 1; i >= 0; --i)
    {
        unsigned __int128 const curNum = toU128(prevRem, numerator[i]);
        std::tie(numerator[i], prevRem) = divRe64(curNum, divisor);
    }
    return prevRem;
}

/** Decompose a single base-58¹⁰ coefficient into 10 base-58 digits, most significant first.
 *
 *  Repeatedly divides `input` by 58, filling the result array from the back
 *  so that the most significant digit occupies index 0 (big-endian digit
 *  order). The caller (`tokens.cpp`) maps each digit through
 *  `alphabetForward[]` to produce the final Base58 character.
 *
 *  @param input A value in [0, 58¹⁰). The precondition `input < 58¹⁰` is
 *      checked by `XRPL_ASSERT`; a value at or above this bound indicates a
 *      logic error in the preceding division step.
 *  @return An array of 10 `uint8_t` values in [0, 58), ordered
 *      most-significant digit first.
 *  @note `[[nodiscard]]` — the caller must use the returned digit array.
 */
[[nodiscard]] inline std::array<std::uint8_t, 10>
b5810ToB58Be(std::uint64_t input)
{
    [[maybe_unused]] static constexpr std::uint64_t kB_58_10 = 430804206899405824;  // 58^10;
    XRPL_ASSERT(input < kB_58_10, "xrpl::b58_fast::detail::b5810ToB58Be : valid input");
    constexpr std::size_t kRESULT_SIZE = 10;
    std::array<std::uint8_t, kRESULT_SIZE> result{};
    int i = 0;
    while (input > 0)
    {
        std::uint64_t rem = 0;
        std::tie(input, rem) = divRem(input, 58);
        result[kRESULT_SIZE - 1 - i] = rem;
        i += 1;
    }

    return result;
}
}  // namespace b58_fast::detail

#endif

}  // namespace xrpl
