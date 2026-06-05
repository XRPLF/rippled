#include <xrpl/basics/Number.h>

#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

#ifdef _MSC_VER
#pragma message("Using boost::multiprecision::uint128_t and int128_t")
#include <boost/multiprecision/cpp_int.hpp>
using uint128_t = boost::multiprecision::uint128_t;
using int128_t = boost::multiprecision::int128_t;
#else   // !defined(_MSC_VER)
using uint128_t = __uint128_t;
using int128_t = __int128_t;
#endif  // !defined(_MSC_VER)

namespace xrpl {

thread_local Number::RoundingMode Number::mode = Number::RoundingMode::ToNearest;
thread_local std::reference_wrapper<MantissaRange const> Number::kRange =
    MantissaRange::getMantissaRange(MantissaRange::MantissaScale::Large);

std::set<MantissaRange::MantissaScale> const&
MantissaRange::getAllScales()
{
    static std::set<MantissaRange::MantissaScale> const kScales = {
        MantissaRange::MantissaScale::Small,
        MantissaRange::MantissaScale::LargeLegacy,
        MantissaRange::MantissaScale::Large,
    };
    return kScales;
}

std::unordered_map<MantissaRange::MantissaScale, MantissaRange> const&
MantissaRange::getRanges()
{
    static auto const kMap = []() {
        std::unordered_map<MantissaScale, MantissaRange> map;
        for (auto const scale : getAllScales())
        {
            map.emplace(scale, scale);
        }

        // Use these constexpr declarations to do static_asserts to verify the MantissaRanges are
        // created correctly, but nothing else.
        {
            [[maybe_unused]]
            constexpr static MantissaRange kRange{MantissaRange::MantissaScale::Small};
            static_assert(isPowerOfTen(kRange.min));
            static_assert(kRange.min == 1'000'000'000'000'000LL);
            static_assert(kRange.max == 9'999'999'999'999'999LL);
            static_assert(kRange.log == 15);
            static_assert(kRange.min < Number::kMaxRep);
            static_assert(kRange.max < Number::kMaxRep);
            static_assert(kRange.cuspRoundingFixEnabled == CuspRoundingFix::Disabled);
        }
        {
            [[maybe_unused]]
            constexpr static MantissaRange kRange{MantissaRange::MantissaScale::LargeLegacy};
            static_assert(isPowerOfTen(kRange.min));
            static_assert(kRange.min == 1'000'000'000'000'000'000ULL);
            static_assert(kRange.max == rep(9'999'999'999'999'999'999ULL));
            static_assert(kRange.log == 18);
            static_assert(kRange.min < Number::kMaxRep);
            static_assert(kRange.max > Number::kMaxRep);
            static_assert(kRange.cuspRoundingFixEnabled == CuspRoundingFix::Disabled);
        }
        {
            [[maybe_unused]]
            constexpr static MantissaRange kRange{MantissaRange::MantissaScale::Large};
            static_assert(isPowerOfTen(kRange.min));
            static_assert(kRange.min == 1'000'000'000'000'000'000ULL);
            static_assert(kRange.max == rep(9'999'999'999'999'999'999ULL));
            static_assert(kRange.log == 18);
            static_assert(kRange.min < Number::kMaxRep);
            static_assert(kRange.max > Number::kMaxRep);
            static_assert(kRange.cuspRoundingFixEnabled == CuspRoundingFix::Enabled);
        }
        return map;
    }();

    return kMap;
}

MantissaRange const&
MantissaRange::getMantissaRange(MantissaScale scale)
{
    return getRanges().at(scale);
}

Number::RoundingMode
Number::getround()
{
    return mode;
}

Number::RoundingMode
Number::setround(RoundingMode inMode)
{
    return std::exchange(Number::mode, inMode);
}

MantissaRange::MantissaScale
Number::getMantissaScale()
{
    return kRange.get().scale;
}

void
Number::setMantissaScale(MantissaRange::MantissaScale scale)
{
    if (!MantissaRange::getAllScales().contains(scale))
        logicError("Unknown mantissa scale");
    kRange = MantissaRange::getMantissaRange(scale);
}

// Optimization equivalent to:
// auto r = static_cast<unsigned>(u % 10);
// u /= 10;
// return r;
// Derived from Hacker's Delight Second Edition Chapter 10
// by Henry S. Warren, Jr.
static inline unsigned
divu10(uint128_t& u)
{
    // q = u * 0.75
    auto q = (u >> 1) + (u >> 2);
    // iterate towards q = u * 0.8
    q += q >> 4;
    q += q >> 8;
    q += q >> 16;
    q += q >> 32;
    q += q >> 64;
    // q /= 8 approximately == u / 10
    q >>= 3;
    // r = u - q * 10  approximately == u % 10
    auto r = static_cast<unsigned>(u - ((q << 3) + (q << 1)));
    // correction c is 1 if r >= 10 else 0
    auto c = (r + 6) >> 4;
    u = q + c;
    r -= c * 10;
    return r;
}

// Guard

// The Guard class is used to temporarily add extra digits of
// precision to an operation.  This enables the final result
// to be correctly rounded to the internal precision of Number.

template <class T>
concept UnsignedMantissa = std::is_unsigned_v<T> || std::is_same_v<T, uint128_t>;

class Number::Guard
{
    std::uint64_t digits_{0};    // 16 decimal guard digits
    std::uint8_t xbit_ : 1 {0};  // has a non-zero digit been shifted off the end
    std::uint8_t sbit_ : 1 {0};  // the sign of the guard digits

public:
    explicit Guard() = default;

    // set & test the sign bit
    void
    setPositive() noexcept;
    void
    setNegative() noexcept;
    // Should only be called by doNormalize, and then only for division
    // operations with remainders.
    void
    setDropped() noexcept;
    [[nodiscard]] bool
    isNegative() const noexcept;

    // add a digit
    template <class T>
    void
    push(T d) noexcept;

    // recover a digit
    unsigned
    pop() noexcept;

    /** Drop a digit from the mantissa, and increment the exponent, storing the dropped digit in
     * this Guard.
     *
     * Substitute for:
                push(mantissa % 10);
                mantissa /= 10;
                ++exponent;
     */
    template <class T>
    void
    doDropDigit(T& mantissa, int& exponent) noexcept;

    // Indicate round direction:  1 is up, -1 is down, 0 is even
    // This enables the client to round towards nearest, and on
    // tie, round towards even.
    [[nodiscard]] int
    round() const noexcept;

    // Modify the result to the correctly rounded value
    template <UnsignedMantissa T>
    void
    doRoundUp(
        bool& negative,
        T& mantissa,
        int& exponent,
        internalrep const& minMantissa,
        internalrep const& maxMantissa,
        MantissaRange::CuspRoundingFix cuspRoundingFixEnabled,
        std::string location);

    // Modify the result to the correctly rounded value
    template <UnsignedMantissa T>
    void
    doRoundDown(bool& negative, T& mantissa, int& exponent, internalrep const& minMantissa);

    // Modify the result to the correctly rounded value
    void
    doRound(rep& drops, std::string location) const;

private:
    void
    doPush(unsigned d) noexcept;

    template <UnsignedMantissa T>
    void
    bringIntoRange(bool& negative, T& mantissa, int& exponent, internalrep const& minMantissa);
};

inline void
Number::Guard::setPositive() noexcept
{
    sbit_ = 0;
}

inline void
Number::Guard::setNegative() noexcept
{
    sbit_ = 1;
}

inline void
Number::Guard::setDropped() noexcept
{
    xbit_ = 1;
}

inline bool
Number::Guard::isNegative() const noexcept
{
    return sbit_ == 1;
}

inline void
Number::Guard::doPush(unsigned d) noexcept
{
    xbit_ = xbit_ || ((digits_ & 0x0000'0000'0000'000F) != 0);
    digits_ >>= 4;
    digits_ |= (d & 0x0000'0000'0000'000FULL) << 60;
}

template <class T>
inline void
Number::Guard::push(T d) noexcept
{
    doPush(static_cast<unsigned>(d));
}

inline unsigned
Number::Guard::pop() noexcept
{
    unsigned const d = (digits_ & 0xF000'0000'0000'0000) >> 60;
    digits_ <<= 4;
    return d;
}

template <class T>
void
Number::Guard::doDropDigit(T& mantissa, int& exponent) noexcept
{
    push(mantissa % 10);
    mantissa /= 10;
    ++exponent;
}

// Use the divu10 optimization for uint128s
template <>
void
Number::Guard::doDropDigit<uint128_t>(uint128_t& mantissa, int& exponent) noexcept
{
    // The following is optimization for:
    // push(static_cast<unsigned>(mantissa % 10));
    // mantissa /= 10;
    push(divu10(mantissa));
    ++exponent;
}

// Returns:
//     -1 if Guard is less than half
//      0 if Guard is exactly half
//      1 if Guard is greater than half
int
Number::Guard::round() const noexcept
{
    auto mode = Number::getround();

    if (mode == RoundingMode::TowardsZero)
        return -1;

    if (mode == RoundingMode::Downward)
    {
        if (sbit_)
        {
            if (digits_ > 0 || xbit_)
                return 1;
        }
        return -1;
    }

    if (mode == RoundingMode::Upward)
    {
        if (sbit_)
            return -1;
        if (digits_ > 0 || xbit_)
            return 1;
        return -1;
    }

    // assume round to nearest if mode is not one of the predefined values
    if (digits_ > 0x5000'0000'0000'0000)
        return 1;
    if (digits_ < 0x5000'0000'0000'0000)
        return -1;
    if (xbit_)
        return 1;
    return 0;
}

template <UnsignedMantissa T>
void
Number::Guard::bringIntoRange(
    bool& negative,
    T& mantissa,
    int& exponent,
    internalrep const& minMantissa)
{
    // Bring mantissa back into the minMantissa / maxMantissa range AFTER
    // rounding
    if (mantissa < minMantissa)
    {
        mantissa *= 10;
        --exponent;
    }
    if (exponent < kMinExponent)
    {
        static constexpr Number kZero = Number{};

        negative = kZero.negative_;
        mantissa = kZero.mantissa_;
        exponent = kZero.exponent_;
    }
}

template <UnsignedMantissa T>
void
Number::Guard::doRoundUp(
    bool& negative,
    T& mantissa,
    int& exponent,
    internalrep const& minMantissa,
    internalrep const& maxMantissa,
    MantissaRange::CuspRoundingFix cuspRoundingFixEnabled,
    std::string location)
{
    auto r = round();
    if (r == 1 || (r == 0 && (mantissa & 1) == 1))
    {
        auto const safeToIncrement = [&maxMantissa](auto const& mantissa) {
            return mantissa < maxMantissa && mantissa < kMaxRep;
        };
        if (cuspRoundingFixEnabled == MantissaRange::CuspRoundingFix::Enabled)
        {
            // Ensure mantissa after incrementing fits within both the
            // min/maxMantissa range and is a valid "rep".
            if (safeToIncrement(mantissa))
            {
                // Nothing unusual here, just increment the mantissa
                ++mantissa;
            }
            else
            {
                // Incrementing the mantissa will require dividing, which will require rounding. So
                // _don't_ increment the mantissa. Instead, divide and round recursively. It should
                // be impossible to recurse more than once, because once the mantissa is divided by
                // 10, it will be _well_ under maxMantissa and kMaxRep, so adding 1 will have no
                // chance of bringing it back over.
                doDropDigit(mantissa, exponent);
                XRPL_ASSERT_PARTS(
                    safeToIncrement(mantissa),
                    "xrpl::Number::Guard::doRoundUp",
                    "can't recurse more than once");
                doRoundUp(
                    negative,
                    mantissa,
                    exponent,
                    minMantissa,
                    maxMantissa,
                    cuspRoundingFixEnabled,
                    location);
                return;
            }
        }
        else
        {
            // Need to preserve the incorrect behavior until the fix amendment can be retired,
            // because otherwise would risk an unplanned ledger fork.
            ++mantissa;
            // Ensure mantissa after incrementing fits within both the
            // min/maxMantissa range and is a valid "rep".
            if (mantissa > maxMantissa || mantissa > kMaxRep)
            {
                // Don't use doDropDigit here
                mantissa /= 10;
                ++exponent;
            }
        }
    }
    bringIntoRange(negative, mantissa, exponent, minMantissa);
    if (exponent > kMaxExponent)
        Throw<std::overflow_error>(std::string(location));
}

template <UnsignedMantissa T>
void
Number::Guard::doRoundDown(
    bool& negative,
    T& mantissa,
    int& exponent,
    internalrep const& minMantissa)
{
    auto r = round();
    if (r == 1 || (r == 0 && (mantissa & 1) == 1))
    {
        --mantissa;
        if (mantissa < minMantissa)
        {
            mantissa *= 10;
            --exponent;
        }
    }
    bringIntoRange(negative, mantissa, exponent, minMantissa);
}

// Modify the result to the correctly rounded value
void
Number::Guard::doRound(rep& drops, std::string location) const
{
    auto r = round();
    if (r == 1 || (r == 0 && (drops & 1) == 1))
    {
        if (drops >= kMaxRep)
        {
            static_assert(sizeof(internalrep) == sizeof(rep));
            // This should be impossible, because it's impossible to represent
            // "kMaxRep + 0.6" in Number, regardless of the scale. There aren't
            // enough digits available. You'd either get a mantissa of "kMaxRep"
            // or "(kMaxRep + 1) / 10", neither of which will round up when
            // converting to rep, though the latter might overflow _before_
            // rounding.
            Throw<std::overflow_error>(std::string(location));  // LCOV_EXCL_LINE
        }
        ++drops;
    }
    if (isNegative())
        drops = -drops;
}

// Number

// Safely convert rep (int64) mantissa to internalrep (uint64). If the rep is
// negative, returns the positive value. This takes a little extra work because
// converting std::numeric_limits<std::int64_t>::min() flirts with UB, and can
// vary across compilers.
Number::internalrep
Number::externalToInternal(rep mantissa)
{
    // If the mantissa is already positive, just return it
    if (mantissa >= 0)
        return mantissa;
    // If the mantissa is negative, but fits within the positive range of rep,
    // return it negated
    if (mantissa >= -std::numeric_limits<rep>::max())
        return -mantissa;

    // If the mantissa doesn't fit within the positive range, convert to
    // int128_t, negate that, and cast it back down to the internalrep
    // In practice, this is only going to cover the case of
    // std::numeric_limits<rep>::min().
    int128_t const temp = mantissa;
    return static_cast<internalrep>(-temp);
}

Number
Number::one()
{
    auto const& range = kRange.get();
    return Number{false, range.min, -range.log, Number::Unchecked{}};
}

template <class T>
void
doNormalize(
    bool& negative,
    T& mantissa,
    int& exponent,
    MantissaRange::rep const& minMantissa,
    MantissaRange::rep const& maxMantissa,
    MantissaRange::CuspRoundingFix cuspRoundingFixEnabled,
    bool dropped)
{
    static constexpr auto kMinExponent = Number::kMinExponent;
    static constexpr auto kMaxExponent = Number::kMaxExponent;
    static constexpr auto kMaxRep = Number::kMaxRep;

    using Guard = Number::Guard;

    static constexpr Number kZero = Number{};
    if (mantissa == 0)
    {
        mantissa = kZero.mantissa_;
        exponent = kZero.exponent_;
        negative = kZero.negative_;
        return;
    }
    auto m = mantissa;
    while ((m < minMantissa) && (exponent > kMinExponent))
    {
        m *= 10;
        --exponent;
    }
    Guard g;
    if (negative)
        g.setNegative();
    if (dropped)
        g.setDropped();
    while (m > maxMantissa)
    {
        if (exponent >= kMaxExponent)
            throw std::overflow_error("Number::normalize 1");
        g.doDropDigit(m, exponent);
    }
    if ((exponent < kMinExponent) || (m < minMantissa))
    {
        mantissa = kZero.mantissa_;
        exponent = kZero.exponent_;
        negative = kZero.negative_;
        return;
    }

    // When using the largeRange, "m" needs fit within an int64, even if
    // the final mantissa is going to end up larger to fit within the
    // MantissaRange. Cut it down here so that the rounding will be done while
    // it's smaller.
    //
    // Example: 9,900,000,000,000,123,456 > 9,223,372,036,854,775,807,
    //      so "m" will be modified to 990,000,000,000,012,345. Then that value
    //      will be rounded to 990,000,000,000,012,345 or
    //      990,000,000,000,012,346, depending on the rounding mode. Finally,
    //      mantissa will be "m*10" so it fits within the range, and end up as
    //      9,900,000,000,000,123,450 or 9,900,000,000,000,123,460.
    // mantissa() will return mantissa / 10, and exponent() will return
    // exponent + 1.
    if (m > kMaxRep)
    {
        if (exponent >= kMaxExponent)
            throw std::overflow_error("Number::normalize 1.5");
        g.doDropDigit(m, exponent);
    }
    // Before modification, m should be within the min/max range. After
    // modification, it must be less than kMaxRep. In other words, the original
    // value should have been no more than kMaxRep * 10.
    // (kMaxRep * 10 > maxMantissa)
    XRPL_ASSERT_PARTS(m <= kMaxRep, "xrpl::doNormalize", "intermediate mantissa fits in int64");
    mantissa = m;

    g.doRoundUp(
        negative,
        mantissa,
        exponent,
        minMantissa,
        maxMantissa,
        cuspRoundingFixEnabled,
        "Number::normalize 2");
    XRPL_ASSERT_PARTS(
        mantissa >= minMantissa && mantissa <= maxMantissa,
        "xrpl::doNormalize",
        "final mantissa fits in range");
}

template <>
void
Number::normalize<uint128_t>(
    bool& negative,
    uint128_t& mantissa,
    int& exponent,
    internalrep const& minMantissa,
    internalrep const& maxMantissa,
    MantissaRange::CuspRoundingFix cuspRoundingFixEnabled)
{
    // Not used by every compiler version, and thus not necessarily
    // counted by coverage build
    // LCOV_EXCL_START
    doNormalize(
        negative, mantissa, exponent, minMantissa, maxMantissa, cuspRoundingFixEnabled, false);
    // LCOV_EXCL_STOP
}

template <>
void
Number::normalize<unsigned long long>(
    bool& negative,
    unsigned long long& mantissa,
    int& exponent,
    internalrep const& minMantissa,
    internalrep const& maxMantissa,
    MantissaRange::CuspRoundingFix cuspRoundingFixEnabled)
{
    // Not used by every compiler version, and thus not necessarily
    // counted by coverage build
    // LCOV_EXCL_START
    doNormalize(
        negative, mantissa, exponent, minMantissa, maxMantissa, cuspRoundingFixEnabled, false);
    // LCOV_EXCL_STOP
}

template <>
void
Number::normalize<unsigned long>(
    bool& negative,
    unsigned long& mantissa,
    int& exponent,
    internalrep const& minMantissa,
    internalrep const& maxMantissa,
    MantissaRange::CuspRoundingFix cuspRoundingFixEnabled)
{
    doNormalize(
        negative, mantissa, exponent, minMantissa, maxMantissa, cuspRoundingFixEnabled, false);
}

void
Number::normalize(MantissaRange const& range)
{
    normalize(negative_, mantissa_, exponent_, range.min, range.max, range.cuspRoundingFixEnabled);
}

// Copy the number, but set a new exponent. Because the mantissa doesn't change,
// the result will be "mostly" normalized, but the exponent could go out of
// range.
Number
Number::shiftExponent(int exponentDelta) const
{
    XRPL_ASSERT_PARTS(isnormal(), "xrpl::Number::shiftExponent", "normalized");
    auto const newExponent = exponent_ + exponentDelta;
    if (newExponent >= kMaxExponent)
        throw std::overflow_error("Number::shiftExponent");
    if (newExponent < kMinExponent)
    {
        return Number{};
    }
    Number const result{negative_, mantissa_, newExponent, Unchecked{}};
    XRPL_ASSERT_PARTS(result.isnormal(), "xrpl::Number::shiftExponent", "result is normalized");
    return result;
}

Number&
Number::operator+=(Number const& y)
{
    static constexpr Number kZero = Number{};
    if (y == kZero)
        return *this;
    if (*this == kZero)
    {
        *this = y;
        return *this;
    }
    if (*this == -y)
    {
        *this = kZero;
        return *this;
    }

    XRPL_ASSERT(isnormal() && y.isnormal(), "xrpl::Number::operator+=(Number) : is normal");
    // *n = negative
    // *s = sign
    // *m = mantissa
    // *e = exponent

    // Need to use uint128_t, because large mantissas can overflow when added
    // together.
    bool xn = negative_;
    uint128_t xm = mantissa_;
    auto xe = exponent_;

    bool const yn = y.negative_;
    uint128_t ym = y.mantissa_;
    auto ye = y.exponent_;
    Guard g;
    if (xe < ye)
    {
        if (xn)
            g.setNegative();
        do
        {
            g.doDropDigit(xm, xe);
        } while (xe < ye);
    }
    else if (xe > ye)
    {
        if (yn)
            g.setNegative();
        do
        {
            g.doDropDigit(ym, ye);
        } while (xe > ye);
    }

    auto const& range = kRange.get();
    auto const& minMantissa = range.min;
    auto const& maxMantissa = range.max;
    auto const cuspRoundingFixEnabled = range.cuspRoundingFixEnabled;

    if (xn == yn)
    {
        xm += ym;
        if (xm > maxMantissa || xm > kMaxRep)
        {
            g.doDropDigit(xm, xe);
        }
        g.doRoundUp(
            xn,
            xm,
            xe,
            minMantissa,
            maxMantissa,
            cuspRoundingFixEnabled,
            "Number::addition overflow");
    }
    else
    {
        if (xm > ym)
        {
            xm = xm - ym;
        }
        else
        {
            xm = ym - xm;
            xe = ye;
            xn = yn;
        }
        while (xm < minMantissa && xm * 10 <= kMaxRep)
        {
            xm *= 10;
            xm -= g.pop();
            --xe;
        }
        g.doRoundDown(xn, xm, xe, minMantissa);
    }

    negative_ = xn;
    mantissa_ = static_cast<internalrep>(xm);
    exponent_ = xe;
    normalize(range);
    return *this;
}

Number&
Number::operator*=(Number const& y)
{
    static constexpr Number kZero = Number{};
    if (*this == kZero)
        return *this;
    if (y == kZero)
    {
        *this = y;
        return *this;
    }
    // *n = negative
    // *s = sign
    // *m = mantissa
    // *e = exponent

    bool const xn = negative_;
    int const xs = xn ? -1 : 1;
    internalrep xm = mantissa_;
    auto xe = exponent_;

    bool const yn = y.negative_;
    int const ys = yn ? -1 : 1;
    internalrep const ym = y.mantissa_;
    auto ye = y.exponent_;

    auto zm = uint128_t(xm) * uint128_t(ym);
    auto ze = xe + ye;
    auto zs = xs * ys;
    bool zn = (zs == -1);
    Guard g;
    if (zn)
        g.setNegative();

    auto const& range = kRange.get();
    auto const& minMantissa = range.min;
    auto const& maxMantissa = range.max;
    auto const cuspRoundingFixEnabled = range.cuspRoundingFixEnabled;

    while (zm > maxMantissa || zm > kMaxRep)
    {
        g.doDropDigit(zm, ze);
    }

    xm = static_cast<internalrep>(zm);
    xe = ze;
    g.doRoundUp(
        zn,
        xm,
        xe,
        minMantissa,
        maxMantissa,
        cuspRoundingFixEnabled,
        "Number::multiplication overflow : exponent is " + std::to_string(xe));
    negative_ = zn;
    mantissa_ = xm;
    exponent_ = xe;

    normalize(range);
    return *this;
}

Number&
Number::operator/=(Number const& y)
{
    static constexpr Number kZero = Number{};
    if (y == kZero)
        throw std::overflow_error("Number: divide by 0");
    if (*this == kZero)
        return *this;
    // n* = numerator
    // d* = denominator
    // z* = result (quotient)
    // *p = negative (p for positive, even though the value means not
    //      positive?)
    // *s = sign
    // *m = mantissa
    // *e = exponent

    bool const np = negative_;
    int const ns = (np ? -1 : 1);
    auto nm = mantissa_;
    auto ne = exponent_;

    bool const dp = y.negative_;
    int const ds = (dp ? -1 : 1);
    // Create the denominator as 128-bit unsigned, since that's what we
    // need to work with.
    uint128_t const dm = static_cast<uint128_t>(y.mantissa_);
    auto const de = y.exponent_;

    auto const& range = kRange.get();
    auto const& minMantissa = range.min;
    auto const& maxMantissa = range.max;
    auto const cuspRoundingFixEnabled = range.cuspRoundingFixEnabled;

    // Division operates on two large integers (16-digit for small
    // mantissas, 19-digit for large) using integer math. If the values
    // were just divided directly, the result would be only ever be one
    // digit or zero - not very useful.
    // e.g. 9'876'543'210'987'654 / 1'234'567'890'123'456 = 8
    //      1'234'567'890'123'456 / 9'876'543'210'987'654 = 0
    // Introduce a power-of-ten multiplication factor for the numerator
    // which will ensure the result has a meaningful number of digits.
    //
    // Consider numbers with a 2-digit mantissa:
    // * Assume both numbers have an exponent of 0, using "ToNearest" rounding
    // * 23 / 67 = 0
    // * Use a factor of 10^4
    // * 230'000 / 67 = 3432 with an exponent of -4
    // * The normalized result will be 34, exponent -2, or 0.34
    //
    // The most extreme results are 10/99 and 99/10
    // * 100'000 / 99 =  1'010e-4 = 10e-2 or 0.10
    // * 990'000 / 10 = 99'000e-4 = 99e-1 or 9.9
    //
    // Note that the computations give 2 or 3 digits after the
    // decimal point to determine which way to round for most scenarios.
    //
    // For small mantissas (where the MantissaRange.log == 15), shifting by 10^17 gives sufficient
    // precision while not overflowing uint128_t or the cast back to int64_t. (This is legacy
    // behavior, which must not be changed.)
    //
    // For large mantissas (where the MantissaRange.log == 18), a shift by 10^20 would be optimal
    // for most scenarios. However, larger mantissa values would overflow 2^128.
    //
    // * log(2^128,10) ~ 38.5
    // * largeRange.log = 18, fits in 10^19
    // * The expanded numerator must fit in 10^38
    // * f not be more than 10^(38-19) = 10^19 safely
    //
    // So, we do the division into stages:
    //
    // Stage 1: Use the same factor of 10^17, for the initial division. This
    // will frequently not result in a whole number quotient.
    //
    // Stage 2: If there is a remainder from the first step, repeat the
    // process with a "correction" factor of 10^5. Shift the
    // result of Stage 1 over by 5 places, and add the second result to it.
    // This is equivalent to if we had used an initial factor of 10^22,
    // a couple digits more than we actually need.
    //
    // Stage 3: If there is still a remainder, and the CuspRoundingFix
    // is enabled, pass a flag indicating such to doNormalize. The Guard
    // in doNormalize will treat that flag as if non-zero digits had
    // been dropped from the mantissa when shrinking it into range.
    // This is only relevant when rounding away from zero (Upward for
    // positive numbers, Downward for negative), or if the "regular"
    // remainder is exactly 0.5 for "ToNearest". This will give the
    // rounding the most accurate result possible, as if infinite
    // precision was used in the initial calculation.

    // Stage 1: Do the initial division with a factor of 10^17.
    auto constexpr factorExponent = 17;

    uint128_t constexpr f = kPowerOfTen[factorExponent];

    auto const numerator = uint128_t(nm) * f;

    auto zm = numerator / dm;
    auto ze = ne - de - factorExponent;
    bool zp = (ns * ds) < 0;
    // dropped is used in the same way as Guard::xbit_. In the case of
    // division, it indicates if there's any remainder left over after
    // we have been as precise as reasonable. If there is, it would be as
    // if we were using infinite precision math, and a non-zero digit
    // had been shifted off the end of the result when normalizing.
    bool dropped = false;

    if (range.scale != MantissaRange::MantissaScale::Small)
    {
        // Stage 2
        //
        // If there is a remainder, treat it as a secondary numerator.
        // Multiply by correctionFactor separately from stage 1.
        // The math for this would work for small mantissas, but we need to
        // preserve legacy behavior.
        //
        // Consider:
        // ((numerator * correctionFactor) / dm) / correctionFactor
        // = ((numerator / dm) * correctionFactor) / correctionFactor)
        //
        // But that assumes infinite precision. With integer math, this is
        // equivalent to
        //
        // = ((numerator / dm * correctionFactor)
        //   + ((numerator % dm) * correctionFactor) / dm) / correctionFactor
        // = ((zm * correctionFactor)
        //   + (remainder * correctionFactor) / dm) / correctionFactor
        //
        // The trick is that multiplication by correctionFactor is done on the mantissa, but
        // division by correctionFactor is done by modifying the exponent, so no precision is lost
        // until we normalize.
        //
        // If remainder is zero, we can skip this stage entirely because
        // the first stage gave an exact answer.
        auto constexpr correctionExponent = 5;
        uint128_t constexpr correctionFactor = kPowerOfTen[correctionExponent];
        static_assert(factorExponent + correctionExponent == 22);

        auto const remainder = (numerator % dm);
        if (remainder != 0)
        {
            auto const partialNumerator = remainder * correctionFactor;
            auto const correction = partialNumerator / dm;

            // If the correction is zero, we do not have to make any
            // modifications to z*, because it will not have any
            // effect on the final result. (We'd be adding a bunch of
            // zeros to the end of zm that would just be removed in
            // normalize.) However, if that is the case, then Stage 3 is
            // even more important for accuracy.
            if (correction != 0)
            {
                zm *= correctionFactor;
                // divide by the correctionFactor by moving the exponent, so we don't lose the
                // integer value we just computed
                ze -= correctionExponent;

                zm += correction;
            }

            // Stage 3: If there's still anything left, and the cusp
            // rounding fix is enabled, flag if there is still
            // a remainder from stage 2.
            bool const useTrailingRemainder =
                cuspRoundingFixEnabled == MantissaRange::CuspRoundingFix::Enabled;
            if (useTrailingRemainder)
            {
                dropped = partialNumerator % dm != 0;
            }
        }
    }
    doNormalize(zp, zm, ze, minMantissa, maxMantissa, cuspRoundingFixEnabled, dropped);
    negative_ = zp;
    mantissa_ = static_cast<internalrep>(zm);
    exponent_ = ze;
    XRPL_ASSERT_PARTS(isnormal(), "xrpl::Number::operator/=", "result is normalized");

    return *this;
}

Number::
operator rep() const
{
    rep drops = mantissa();
    int offset = exponent();
    Guard g;
    if (drops != 0)
    {
        if (negative_)
        {
            g.setNegative();
            drops = -drops;
        }
        while (offset < 0)
        {
            g.doDropDigit(drops, offset);
        }
        for (; offset > 0; --offset)
        {
            if (drops > kMaxRep / 10)
                throw std::overflow_error("Number::operator rep() overflow");
            drops *= 10;
        }
        g.doRound(drops, "Number::operator rep() rounding overflow");
    }
    return drops;
}

Number
Number::truncate() const noexcept
{
    if (exponent_ >= 0 || mantissa_ == 0)
        return *this;

    Number ret = *this;
    while (ret.exponent_ < 0 && ret.mantissa_ != 0)
    {
        ret.exponent_ += 1;
        ret.mantissa_ /= rep(10);
    }
    // We are guaranteed that normalize() will never throw an exception
    // because exponent is either negative or zero at this point.
    ret.normalize(kRange);
    return ret;
}

std::string
to_string(Number const& amount)
{
    // keep full internal accuracy, but make more human friendly if possible
    static constexpr Number kZero = Number{};
    if (amount == kZero)
        return "0";

    auto exponent = amount.exponent_;
    auto mantissa = amount.mantissa_;
    bool const negative = amount.negative_;

    // Use scientific notation for exponents that are too small or too large
    auto const rangeLog = Number::mantissaLog();
    if (((exponent != 0) && ((exponent < -(rangeLog + 10)) || (exponent > -(rangeLog - 10)))))
    {
        while (mantissa != 0 && mantissa % 10 == 0 && exponent < Number::kMaxExponent)
        {
            mantissa /= 10;
            ++exponent;
        }
        std::string ret = negative ? "-" : "";
        ret.append(std::to_string(mantissa));
        ret.append(1, 'e');
        ret.append(std::to_string(exponent));
        return ret;
    }

    XRPL_ASSERT(exponent + 43 > 0, "xrpl::to_string(Number) : minimum exponent");

    ptrdiff_t const padPrefix = rangeLog + 12;
    ptrdiff_t const padSuffix = rangeLog + 8;

    std::string const rawValue(std::to_string(mantissa));
    std::string val;

    val.reserve(rawValue.length() + padPrefix + padSuffix);
    val.append(padPrefix, '0');
    val.append(rawValue);
    val.append(padSuffix, '0');

    ptrdiff_t const offset(exponent + padPrefix + rangeLog + 1);

    auto preFrom(val.begin());
    auto const preTo(val.begin() + offset);

    auto const postFrom(val.begin() + offset);
    auto postTo(val.end());

    // Crop leading zeroes. Take advantage of the fact that there's always a
    // fixed amount of leading zeroes and skip them.
    if (std::distance(preFrom, preTo) > padPrefix)
        preFrom += padPrefix;

    XRPL_ASSERT(postTo >= postFrom, "xrpl::to_string(Number) : first distance check");

    preFrom = std::find_if(preFrom, preTo, [](char c) { return c != '0'; });

    // Crop trailing zeroes. Take advantage of the fact that there's always a
    // fixed amount of trailing zeroes and skip them.
    if (std::distance(postFrom, postTo) > padSuffix)
        postTo -= padSuffix;

    XRPL_ASSERT(postTo >= postFrom, "xrpl::to_string(Number) : second distance check");

    postTo = std::find_if(
                 std::make_reverse_iterator(postTo),
                 std::make_reverse_iterator(postFrom),
                 [](char c) { return c != '0'; })
                 .base();

    std::string ret;

    if (negative)
        ret.append(1, '-');

    // Assemble the output:
    if (preFrom == preTo)
    {
        ret.append(1, '0');
    }
    else
    {
        ret.append(preFrom, preTo);
    }

    if (postTo != postFrom)
    {
        ret.append(1, '.');
        ret.append(postFrom, postTo);
    }

    return ret;
}

// Returns f^n
// Uses a log_2(n) number of multiplications

Number
power(Number const& f, unsigned n)
{
    if (n == 0)
        return Number::one();
    if (n == 1)
        return f;
    auto r = power(f, n / 2);
    r *= r;
    if (n % 2 != 0)
        r *= f;
    return r;
}

// Returns f^(1/d)
// Uses Newton–Raphson iterations until the result stops changing
// to find the non-negative root of the polynomial g(x) = x^d - f

// This function, and power(Number f, unsigned n, unsigned d)
// treat corner cases such as 0 roots as advised by Annex F of
// the C standard, which itself is consistent with the IEEE
// floating point standards.

Number
root(Number f, unsigned d)
{
    static constexpr Number kZero = Number{};
    auto const one = Number::one();

    if (f == one || d == 1)
        return f;
    if (d == 0)
    {
        if (f == -one)
            return one;
        if (abs(f) < one)
            return kZero;
        throw std::overflow_error("Number::root infinity");
    }
    if (f < kZero && d % 2 == 0)
        throw std::overflow_error("Number::root nan");
    if (f == kZero)
        return f;

    // Scale f into the range (0, 1) such that f's exponent is a multiple of d
    auto e = f.exponent_ + Number::mantissaLog() + 1;
    auto const di = static_cast<int>(d);
    auto ex = [e = e, di = di]()  // Euclidean remainder of e/d
    {
        int const k = (e >= 0 ? e : e - (di - 1)) / di;
        int const k2 = e - (k * di);
        if (k2 == 0)
            return 0;
        return di - k2;
    }();
    e += ex;
    f = f.shiftExponent(-e);  // f /= 10^e;

    XRPL_ASSERT_PARTS(f.isnormal(), "xrpl::root(Number, unsigned)", "f is normalized");
    bool neg = false;
    if (f < kZero)
    {
        neg = true;
        f = -f;
    }

    // Quadratic least squares curve fit of f^(1/d) in the range [0, 1]
    auto const D = (((6 * di + 11) * di + 6) * di) + 1;  // NOLINT(readability-identifier-naming)
    auto const a0 = 3 * di * ((2 * di - 3) * di + 1);
    auto const a1 = 24 * di * (2 * di - 1);
    auto const a2 = -30 * (di - 1) * di;
    Number r = ((Number{a2} * f + Number{a1}) * f + Number{a0}) / Number{D};
    if (neg)
    {
        f = -f;
        r = -r;
    }

    //  Newton–Raphson iteration of f^(1/d) with initial guess r
    //  halt when r stops changing, checking for bouncing on the last iteration
    Number rm1{};
    Number rm2{};
    do
    {
        rm2 = rm1;
        rm1 = r;
        r = (Number(d - 1) * r + f / power(r, d - 1)) / Number(d);
    } while (r != rm1 && r != rm2);

    //  return r * 10^(e/d) to reverse scaling
    auto const result = r.shiftExponent(e / di);
    XRPL_ASSERT_PARTS(result.isnormal(), "xrpl::root(Number, unsigned)", "result is normalized");
    return result;
}

Number
root2(Number f)
{
    static constexpr Number kZero = Number{};
    auto const one = Number::one();

    if (f == one)
        return f;
    if (f < kZero)
        throw std::overflow_error("Number::root nan");
    if (f == kZero)
        return f;

    // Scale f into the range (0, 1) such that f's exponent is a multiple of d
    auto e = f.exponent_ + Number::mantissaLog() + 1;
    if (e % 2 != 0)
        ++e;
    f = f.shiftExponent(-e);  // f /= 10^e;
    XRPL_ASSERT_PARTS(f.isnormal(), "xrpl::root2(Number)", "f is normalized");

    // Quadratic least squares curve fit of f^(1/d) in the range [0, 1]
    auto const D = 105;  // NOLINT(readability-identifier-naming)
    auto const a0 = 18;
    auto const a1 = 144;
    auto const a2 = -60;
    Number r = ((Number{a2} * f + Number{a1}) * f + Number{a0}) / Number{D};

    //  Newton–Raphson iteration of f^(1/2) with initial guess r
    //  halt when r stops changing, checking for bouncing on the last iteration
    Number rm1{};
    Number rm2{};
    do
    {
        rm2 = rm1;
        rm1 = r;
        r = (r + f / r) / Number(2);
    } while (r != rm1 && r != rm2);

    //  return r * 10^(e/2) to reverse scaling
    auto const result = r.shiftExponent(e / 2);
    XRPL_ASSERT_PARTS(result.isnormal(), "xrpl::root2(Number)", "result is normalized");

    return result;
}

// Returns f^(n/d)

Number
power(Number const& f, unsigned n, unsigned d)
{
    static constexpr Number kZero = Number{};
    auto const one = Number::one();

    if (f == one)
        return f;
    auto g = std::gcd(n, d);
    if (g == 0)
        throw std::overflow_error("Number::power nan");
    if (d == 0)
    {
        if (f == -one)
            return one;
        if (abs(f) < one)
            return kZero;
        // abs(f) > one
        throw std::overflow_error("Number::power infinity");
    }
    if (n == 0)
        return one;
    n /= g;
    d /= g;
    if ((n % 2) == 1 && (d % 2) == 0 && f < kZero)
        throw std::overflow_error("Number::power nan");
    return root(power(f, n), d);
}

}  // namespace xrpl
