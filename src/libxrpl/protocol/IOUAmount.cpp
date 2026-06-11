#include <xrpl/protocol/IOUAmount.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/STAmount.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace xrpl {

/* The range for the mantissa when normalized */
// log(2^63,10) ~ 18.96
//
static constexpr std::int64_t kMinMantissa = STAmount::kMinValue;
static constexpr std::int64_t kMaxMantissa = STAmount::kMaxValue;
/* The range for the exponent when normalized */
static constexpr int kMinExponent = STAmount::kMinOffset;
static constexpr int kMaxExponent = STAmount::kMaxOffset;

IOUAmount
IOUAmount::fromNumber(Number const& number)
{
    // Need to create a default IOUAmount and assign directly so it doesn't try
    // to normalize, which calls fromNumber
    IOUAmount result{};
    std::tie(result.mantissa_, result.exponent_) =
        number.normalizeToRange<kMinMantissa, kMaxMantissa>();
    return result;
}

IOUAmount
IOUAmount::minPositiveAmount()
{
    return IOUAmount(kMinMantissa, kMinExponent);
}

void
IOUAmount::normalize()
{
    if (mantissa_ == 0)
    {
        *this = beast::kZero;
        return;
    }

    Number const v{mantissa_, exponent_};
    *this = IOUAmount(v);
}

IOUAmount::IOUAmount(Number const& other) : IOUAmount(fromNumber(other))
{
    if (exponent_ > kMaxExponent)
    {
        Throw<std::overflow_error>("value overflow");
    }
    if (exponent_ < kMinExponent)
    {
        *this = beast::kZero;
    }
}

IOUAmount&
IOUAmount::operator+=(IOUAmount const& other)
{
    if (other == beast::kZero)
        return *this;

    if (*this == beast::kZero)
    {
        *this = other;
        return *this;
    }

    *this = IOUAmount{Number{*this} + Number{other}};
    return *this;
}

std::string
to_string(IOUAmount const& amount)
{
    return to_string(Number{amount});
}

IOUAmount
mulRatio(IOUAmount const& amt, std::uint32_t num, std::uint32_t den, bool roundUp)
{
    using namespace boost::multiprecision;

    if (den == 0u)
        Throw<std::runtime_error>("division by zero");

    // A vector with the value 10^index for indexes from 0 to 29
    // The largest intermediate value we expect is 2^96, which
    // is less than 10^29
    static auto const kPowerTable = [] {
        std::vector<uint128_t> result;
        result.reserve(30);  // 2^96 is largest intermediate result size
        uint128_t cur(1);
        for (int i = 0; i < 30; ++i)
        {
            result.push_back(cur);
            cur *= 10;
        };
        return result;
    }();

    // Return floor(log10(v))
    // Note: Returns -1 for v == 0
    static auto kLoG10Floor = [](uint128_t const& v) {
        // Find the index of the first element >= the requested element, the
        // index is the log of the element in the log table.
        auto const l = std::ranges::lower_bound(kPowerTable, v);
        int index = std::distance(kPowerTable.begin(), l);
        // If we're not equal, subtract to get the floor
        if (*l != v)
            --index;
        return index;
    };

    // Return ceil(log10(v))
    static auto kLoG10Ceil = [](uint128_t const& v) {
        // Find the index of the first element >= the requested element, the
        // index is the log of the element in the log table.
        auto const l = std::ranges::lower_bound(kPowerTable, v);
        return int(std::distance(kPowerTable.begin(), l));
    };

    static auto const kFl64 = kLoG10Floor(std::numeric_limits<std::int64_t>::max());

    bool const neg = amt.mantissa() < 0;
    uint128_t const den128(den);
    // a 32 value * a 64 bit value and stored in a 128 bit value. This will
    // never overflow
    uint128_t const mul = uint128_t(neg ? -amt.mantissa() : amt.mantissa()) * uint128_t(num);

    auto low = mul / den128;
    uint128_t rem(mul - low * den128);

    int exponent = amt.exponent();

    if (rem)
    {
        // Mathematically, the result is low + rem/den128. However, since this
        // uses integer division rem/den128 will be zero. Scale the result so
        // low does not overflow the largest amount we can store in the mantissa
        // and (rem/den128) is as large as possible. Scale by multiplying low
        // and rem by 10 and subtracting one from the exponent. We could do this
        // with a loop, but it's more efficient to use logarithms.
        auto const roomToGrow = kFl64 - kLoG10Ceil(low);
        if (roomToGrow > 0)
        {
            exponent -= roomToGrow;
            low *= kPowerTable[roomToGrow];
            rem *= kPowerTable[roomToGrow];
        }
        auto const addRem = rem / den128;
        low += addRem;
        rem = rem - addRem * den128;
    }

    // The largest result we can have is ~2^95, which overflows the 64 bit
    // result we can store in the mantissa. Scale result down by dividing by ten
    // and adding one to the exponent until the low will fit in the 64-bit
    // mantissa. Use logarithms to avoid looping.
    bool hasRem = bool(rem);
    auto const mustShrink = kLoG10Ceil(low) - kFl64;
    if (mustShrink > 0)
    {
        uint128_t const sav(low);
        exponent += mustShrink;
        low /= kPowerTable[mustShrink];
        if (!hasRem)
            hasRem = bool(sav - low * kPowerTable[mustShrink]);
    }

    std::int64_t mantissa = low.convert_to<std::int64_t>();

    // normalize before rounding
    if (neg)
        mantissa *= -1;

    IOUAmount result(mantissa, exponent);

    if (hasRem)
    {
        // handle rounding
        if (roundUp && !neg)
        {
            if (!result)
            {
                return IOUAmount::minPositiveAmount();
            }
            // This addition cannot overflow because the mantissa is already
            // normalized
            return IOUAmount(result.mantissa() + 1, result.exponent());
        }

        if (!roundUp && neg)
        {
            if (!result)
            {
                return IOUAmount(-kMinMantissa, kMinExponent);
            }
            // This subtraction cannot underflow because `result` is not zero
            return IOUAmount(result.mantissa() - 1, result.exponent());
        }
    }

    return result;
}

}  // namespace xrpl
