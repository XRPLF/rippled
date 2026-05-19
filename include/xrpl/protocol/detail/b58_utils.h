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

template <class T>
using Result = boost::outcome_v2::result<T, std::error_code>;

#ifndef _MSC_VER

namespace b58_fast::detail {

// This optimizes to what hand written asm would do (single divide)
[[nodiscard]] inline std::tuple<std::uint64_t, std::uint64_t>
divRem(std::uint64_t a, std::uint64_t b)
{
    return {a / b, a % b};
}

// This optimizes to what hand written asm would do (single multiply)
[[nodiscard]] inline std::tuple<std::uint64_t, std::uint64_t>
carryingMul(std::uint64_t a, std::uint64_t b, std::uint64_t carry)
{
    unsigned __int128 const x = a;
    unsigned __int128 const y = b;
    unsigned __int128 const c = (x * y) + carry;
    return {c & 0xffff'ffff'ffff'ffff, c >> 64};
}

[[nodiscard]] inline std::tuple<std::uint64_t, std::uint64_t>
carryingAdd(std::uint64_t a, std::uint64_t b)
{
    unsigned __int128 const x = a;
    unsigned __int128 const y = b;
    unsigned __int128 const c = x + y;
    return {c & 0xffff'ffff'ffff'ffff, c >> 64};
}

// Add a u64 to a "big uint" value inplace.
// The bigint value is stored with the smallest coefficients first
// (i.e a[0] is the 2^0 coefficient, a[n] is the 2^(64*n) coefficient)
// panics if overflows (this is a specialized adder for b58 decoding.
// it should never overflow).
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

// divide a "big uint" value inplace and return the mod
// numerator is stored so smallest coefficients come first
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

// convert from base 58^10 to base 58
// put largest coeffs first
// the `_be` suffix stands for "big endian"
[[nodiscard]] inline std::array<std::uint8_t, 10>
b5810ToB58Be(std::uint64_t input)
{
    [[maybe_unused]] static constexpr std::uint64_t kB5810 = 430804206899405824;  // 58^10;
    XRPL_ASSERT(input < kB5810, "xrpl::b58_fast::detail::b5810ToB58Be : valid input");
    static constexpr std::size_t kResultSize = 10;
    std::array<std::uint8_t, kResultSize> result{};
    int i = 0;
    while (input > 0)
    {
        std::uint64_t rem = 0;
        std::tie(input, rem) = divRem(input, 58);
        result[kResultSize - 1 - i] = rem;
        i += 1;
    }

    return result;
}
}  // namespace b58_fast::detail

#endif

}  // namespace xrpl
