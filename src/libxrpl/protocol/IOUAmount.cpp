#include <xrpl/protocol/IOUAmount.h>

#include <xrpl/basics/LocalValue.h>
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

namespace {

// Use a static inside a function to help prevent order-of-initialization issues
LocalValue<bool>&
getStaticSTNumberSwitchover()
{
    static LocalValue<bool> kR{true};
    return kR;
}
}  // namespace

bool
getSTNumberSwitchover()
{
    return *getStaticSTNumberSwitchover();
}

void
setSTNumberSwitchover(bool v)
{
    *getStaticSTNumberSwitchover() = v;
}

/* The range for the mantissa when normalized */
// log(2^63,10) ~ 18.96
//
static std::int64_t constexpr kMIN_MANTISSA = STAmount::kMIN_VALUE;
static std::int64_t constexpr kMAX_MANTISSA = STAmount::kMAX_VALUE;
/* The range for the exponent when normalized */
static int constexpr kMIN_EXPONENT = STAmount::kMIN_OFFSET;
static int constexpr kMAX_EXPONENT = STAmount::kMAX_OFFSET;

IOUAmount
IOUAmount::fromNumber(Number const& number)
{
    // Need to create a default IOUAmount and assign directly so it doesn't try
    // to normalize, which calls fromNumber
    IOUAmount result{};
    std::tie(result.mantissa_, result.exponent_) =
        number.normalizeToRange(kMIN_MANTISSA, kMAX_MANTISSA);
    return result;
}

IOUAmount
IOUAmount::minPositiveAmount()
{
    return IOUAmount(kMIN_MANTISSA, kMIN_EXPONENT);
}

void
IOUAmount::normalize()
{
    if (mantissa_ == 0)
    {
        *this = beast::kZERO;
        return;
    }

    if (getSTNumberSwitchover())
    {
        Number const v{mantissa_, exponent_};
        *this = fromNumber(v);
        if (exponent_ > kMAX_EXPONENT)
            Throw<std::overflow_error>("value overflow");
        if (exponent_ < kMIN_EXPONENT)
            *this = beast::kZERO;
        return;
    }

    bool const negative = (mantissa_ < 0);

    if (negative)
        mantissa_ = -mantissa_;

    while ((mantissa_ < kMIN_MANTISSA) && (exponent_ > kMIN_EXPONENT))
    {
        mantissa_ *= 10;
        --exponent_;
    }

    while (mantissa_ > kMAX_MANTISSA)
    {
        if (exponent_ >= kMAX_EXPONENT)
            Throw<std::overflow_error>("IOUAmount::normalize");

        mantissa_ /= 10;
        ++exponent_;
    }

    if ((exponent_ < kMIN_EXPONENT) || (mantissa_ < kMIN_MANTISSA))
    {
        *this = beast::kZERO;
        return;
    }

    if (exponent_ > kMAX_EXPONENT)
        Throw<std::overflow_error>("value overflow");

    if (negative)
        mantissa_ = -mantissa_;
}

IOUAmount::IOUAmount(Number const& other) : IOUAmount(fromNumber(other))
{
    if (exponent_ > kMAX_EXPONENT)
        Throw<std::overflow_error>("value overflow");
    if (exponent_ < kMIN_EXPONENT)
        *this = beast::kZERO;
}

IOUAmount&
IOUAmount::operator+=(IOUAmount const& other)
{
    if (other == beast::kZERO)
        return *this;

    if (*this == beast::kZERO)
    {
        *this = other;
        return *this;
    }

    if (getSTNumberSwitchover())
    {
        *this = IOUAmount{Number{*this} + Number{other}};
        return *this;
    }
    auto m = other.mantissa_;
    auto e = other.exponent_;

    while (exponent_ < e)
    {
        mantissa_ /= 10;
        ++exponent_;
    }

    while (e < exponent_)
    {
        m /= 10;
        ++e;
    }

    // This addition cannot overflow an std::int64_t but we may throw from
    // normalize if the result isn't representable.
    mantissa_ += m;

    if (mantissa_ >= -10 && mantissa_ <= 10)
    {
        *this = beast::kZERO;
        return *this;
    }

    normalize();
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
    static auto const kPOWER_TABLE = [] {
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
    static auto kLOG10_FLOOR = [](uint128_t const& v) {
        // Find the index of the first element >= the requested element, the
        // index is the log of the element in the log table.
        auto const l = std::ranges::lower_bound(kPOWER_TABLE, v);
        int index = std::distance(kPOWER_TABLE.begin(), l);
        // If we're not equal, subtract to get the floor
        if (*l != v)
            --index;
        return index;
    };

    // Return ceil(log10(v))
    static auto kLOG10_CEIL = [](uint128_t const& v) {
        // Find the index of the first element >= the requested element, the
        // index is the log of the element in the log table.
        auto const l = std::ranges::lower_bound(kPOWER_TABLE, v);
        return int(std::distance(kPOWER_TABLE.begin(), l));
    };

    static auto const kFL64 = kLOG10_FLOOR(std::numeric_limits<std::int64_t>::max());

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
        auto const roomToGrow = kFL64 - kLOG10_CEIL(low);
        if (roomToGrow > 0)
        {
            exponent -= roomToGrow;
            low *= kPOWER_TABLE[roomToGrow];
            rem *= kPOWER_TABLE[roomToGrow];
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
    auto const mustShrink = kLOG10_CEIL(low) - kFL64;
    if (mustShrink > 0)
    {
        uint128_t const sav(low);
        exponent += mustShrink;
        low /= kPOWER_TABLE[mustShrink];
        if (!hasRem)
            hasRem = bool(sav - low * kPOWER_TABLE[mustShrink]);
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
                return IOUAmount(-kMIN_MANTISSA, kMIN_EXPONENT);
            }
            // This subtraction cannot underflow because `result` is not zero
            return IOUAmount(result.mantissa() - 1, result.exponent());
        }
    }

    return result;
}

}  // namespace xrpl
