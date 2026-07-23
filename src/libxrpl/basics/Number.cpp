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
    MantissaRange::Access::mantissaRange(MantissaRange::MantissaScale::Large330);

std::string
to_string(MantissaRange::MantissaScale const& scale)
{
    switch (scale)
    {
        case MantissaRange::MantissaScale::Small:
            return "Small";
        case MantissaRange::MantissaScale::LargeLegacy:
            return "LargeLegacy";
        case MantissaRange::MantissaScale::Large320:
            return "Large320";
        case MantissaRange::MantissaScale::Large330:
            return "Large330";
        default:
            throw std::runtime_error("Bad scale");  // LCOV_EXCL_LINE
    }
}

std::string
to_string(Number::RoundingMode const& round)
{
    switch (round)
    {
        case Number::RoundingMode::ToNearest:
            return "ToNearest";
        case Number::RoundingMode::TowardsZero:
            return "TowardsZero";
        case Number::RoundingMode::Downward:
            return "Downward";
        case Number::RoundingMode::Upward:
            return "Upward";
        default:
            throw std::runtime_error("Bad rounding mode");  // LCOV_EXCL_LINE
    }
}

constexpr MantissaRange const&
MantissaRange::Access::mantissaRange(MantissaScale scale)
{
    static constexpr MantissaRange kSmall{MantissaScale::Small};
    static constexpr MantissaRange kLegacy{MantissaScale::LargeLegacy};
    static constexpr MantissaRange kLarge320{MantissaScale::Large320};
    static constexpr MantissaRange kLarge330{MantissaScale::Large330};

    switch (scale)
    {
        case MantissaScale::Small:
            return kSmall;
        case MantissaScale::LargeLegacy:
            return kLegacy;
        case MantissaScale::Large320:
            return kLarge320;
        case MantissaScale::Large330:
            return kLarge330;
    }
    throw std::logic_error("Unknown mantissa scale");

    // static_asserts are checked at compile time, so it doesn't matter where in the function they
    // are located. For readability of the main body, put them after it.

    // Small
    static_assert(isPowerOfTen(kSmall.min));
    static_assert(kSmall.min == 1'000'000'000'000'000LL);
    static_assert(kSmall.max == 9'999'999'999'999'999LL);
    static_assert(kSmall.log == 15);
    static_assert(kSmall.min < Number::kMaxRep);
    static_assert(kSmall.max < Number::kMaxRep);
    static_assert(kSmall.cuspRoundingFix == CuspRoundingFix::Disabled);

    // LargeLegacy
    static_assert(isPowerOfTen(kLegacy.min));
    static_assert(kLegacy.min == 1'000'000'000'000'000'000ULL);
    static_assert(kLegacy.max == rep(9'999'999'999'999'999'999ULL));
    static_assert(kLegacy.log == 18);
    static_assert(kLegacy.min < Number::kMaxRep);
    static_assert(kLegacy.max > Number::kMaxRep);
    static_assert(kLegacy.cuspRoundingFix == CuspRoundingFix::Disabled);

    // Large320
    static_assert(isPowerOfTen(kLarge320.min));
    static_assert(kLarge320.min == 1'000'000'000'000'000'000ULL);
    static_assert(kLarge320.max == rep(9'999'999'999'999'999'999ULL));
    static_assert(kLarge320.log == 18);
    static_assert(kLarge320.min < Number::kMaxRep);
    static_assert(kLarge320.max > Number::kMaxRep);
    static_assert(kLarge320.cuspRoundingFix == CuspRoundingFix::Enabled320);

    // Large330
    static_assert(isPowerOfTen(kLarge330.min));
    static_assert(kLarge330.min == 1'000'000'000'000'000'000ULL);
    static_assert(kLarge330.max == rep(9'999'999'999'999'999'999ULL));
    static_assert(kLarge330.log == 18);
    static_assert(kLarge330.min < Number::kMaxRep);
    static_assert(kLarge330.max > Number::kMaxRep);
    static_assert(kLarge330.cuspRoundingFix == CuspRoundingFix::Enabled330);
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
    kRange = MantissaRange::Access::mantissaRange(scale);
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

template <class T>
concept UnsignedMantissa = std::is_unsigned_v<T> || std::is_same_v<T, uint128_t>;

/**
 * Guard
 *
 * The Guard class is used to temporarily add extra digits of
 * precision to an operation.  This enables the final result
 * to be correctly rounded to the internal precision of Number.
 *
 * At its core, the Guard really only needs three pieces of information to determine how to round:
 * 1. The rounding mode
 * 2. The last digit dropped from the mantissa (i.e. the first digit after the decimal point).
 * (first byte of digits_)
 * 3. Whether any other non-zero digits were dropped from the mantissa. (remaining bytes of digits_
 * and xbit_)
 *
 * Upward and Downward rounding modes round the unsigned mantissa toward or away from zero
 * depending on whether the sign is negative (sbit_). For positive values, Upward is away, and
 * Downward is toward. For negative values, that's reversed. For simplicity, I'm going to describe
 * the logic using "TowardZero" and "AwayFromZero".
 *
 * TowardZero is the easiest rounding mode. It always rounds down. digits_ and xbit_ are
 * irrelevant.
 * AwayFromZero is almost as simple. If both "digits_" and "xbit_" are zero (0), it rounds down.
 * Else it rounds up.
 * ToNearest is only a little more complicated. If the last dropped digit is < 5, then round
 * down. If it is > 5, round up. If it is exactly 5, and there are _any_ other digits (the
 * remainder of "digits_" or "xbit_"), round up, else round to even.
 *
 * The current implementation stores 16 digits in "digits_" so that digits can be "pop"ped back
 * out if needed during subtraction (negative addition) operations.
 */
class Number::Guard
{
    std::uint64_t digits_{0};    // 16 decimal guard digits
    std::uint8_t xbit_ : 1 {0};  // has a non-zero digit been shifted off the end
    std::uint8_t sbit_ : 1 {0};  // the sign of the guard digits

public:
    internalrep const minMantissa;
    internalrep const maxMantissa;
    MantissaRange::CuspRoundingFix const cuspRoundingFix;

    explicit Guard(
        internalrep const& minMantissa,
        internalrep const& maxMantissa,
        MantissaRange::CuspRoundingFix cuspRoundingFix)
        : minMantissa(minMantissa), maxMantissa(maxMantissa), cuspRoundingFix(cuspRoundingFix)
    {
    }

    explicit Guard(MantissaRange const& range) : Guard(range.min, range.max, range.cuspRoundingFix)
    {
    }

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

    // if true, there are no digits in the guard, including dropped digits (xbit_)
    [[nodiscard]] bool
    empty() const noexcept;

    /**
     * Drop a digit from the mantissa, and increment the exponent, storing the dropped digit in
     * this Guard.
     *
     * Substitute for:
     * push(mantissa % 10);
     * mantissa /= 10;
     * ++exponent;
     */
    template <class T>
    void
    doDropDigit(T& mantissa, int& exponent) noexcept;

    // Modify the result to the correctly rounded value
    template <UnsignedMantissa T>
    void
    doRoundUp(bool& negative, T& mantissa, int& exponent, std::string location);

    // Modify the result to the correctly rounded value
    template <UnsignedMantissa T>
    void
    doRoundDown(bool& negative, T& mantissa, int& exponent) const;

    // Modify the result to the correctly rounded value
    void
    doRound(rep& drops, std::string location) const;

private:
    template <UnsignedMantissa T>
    void
    pushOverflow(T mantissa);

    enum class Round {
        // The result is exact. No rounding is needed. Only used if cuspRoundingFix is Enabled330 or
        // higher.
        Exact = -2,
        // Round down. Since we use integer math, that usually means no change is needed.
        // Exceptions are for when the result is between kMaxRep and kMaxRepUp (round to kMaxRep),
        // or after subtraction where _any_ remainder will modify the result. The latter is what
        // distinguishes Exact from Down.
        Down = -1,
        // The result was exactly half-way between two integers. This will round to even.
        Even = 0,
        // Round up. Always adds 1 (or subtracts 1 in some cases if cuspRoundingFix is not
        // Enabled330)
        Up = 1,
    };

    // Indicate round direction. See Round enum above.
    // This enables the client to round towards nearest, and on
    // tie, round towards even.
    [[nodiscard]] Round
    round() const noexcept;

    void
    doPush(unsigned d) noexcept;

    template <UnsignedMantissa T>
    void
    bringIntoRange(bool& negative, T& mantissa, int& exponent) const;
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
    XRPL_ASSERT(d < 10, "xrpl::Number::Guard::doPush : valid digit");
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

inline bool
Number::Guard::empty() const noexcept
{
    return digits_ == 0 && !xbit_;
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

template <UnsignedMantissa T>
void
Number::Guard::pushOverflow(T mantissa)
{
    XRPL_ASSERT(mantissa <= kMaxRepUp, "xrpl::Number::Guard::pushOverflow : valid mantissa");
    if (cuspRoundingFix >= MantissaRange::CuspRoundingFix::Enabled330 && mantissa >= kMaxRep &&
        mantissa < kMaxRepUp)
    {
        // Special case rounding rules for the values in the range [kMaxRep, kMaxRepUp).

        auto constexpr spread = kMaxRepUp - kMaxRep;
        static_assert(spread == 3);

        // Round in two steps.

        // The first step uses the digits _already_ in the Guard to possibly round the mantissa up.
        // Ultimately, the purpose of this step is to capture rounding where the stored digits would
        // change the decision without those digits. (e.g. From just _below_ the midpoint to just
        // _above_ the midpoint for ToNearest, or from kMaxRep into the in-between for Upward. Make
        // an exception if the final digit is 9, because it can only get larger, and we don't want
        // to bump up to kMaxRepUp.
        if (mantissa % 10 < 9)
        {
            // Intentionally use integer math to get the largest value under the midpoint.
            auto constexpr kMidpoint = kMaxRep + (spread / 2);
            static_assert(kMidpoint == kMaxRep + 1);
            auto const r = round();
            if (r == Round::Up || (r == Round::Even && mantissa == kMidpoint))
            {
                ++mantissa;
            }
        }

        // The second step scales the final digit of the updated mantissa proportionally, converting
        // from (kMaxRep, kMaxRepUp) to (0 to 9]. It then pushes that scaled digit onto the guard as
        // if it was a digit that got removed, but doesn't actually remove it. This method should be
        // future-proof in case the number of mantissa bits ever changes. (Though for integer values
        // of the form 2^(2^x-1), the spread will always be the same.) Effects:
        // * For round to nearest
        //      * if the updated mantissa is below the midpoint, it'll round "down" to kMaxRep
        //      * if above the midpoint, it'll round "up" to kMaxRepUp
        //      * it can never be exactly at the midpoint, because kMaxRepUp is always even, and
        //        kMaxRep is always odd, so don't worry about that case.
        // * For round upward, will round up to kMaxRepUp for positive values, down to kMaxRep for
        //   negative.
        // * For round downward, does the opposite of upward.
        // * For round toward zero, always rounds down to kMaxRep.

        auto const diff = mantissa - kMaxRep;
        auto const digit = static_cast<unsigned>((diff * 10) / spread);
        XRPL_ASSERT(
            digit < 10u && digit != 5, "xrpl::Number::Guard::pushOverflow : valid overflow digit");

        // Don't remove the digit from the mantissa, but add it to the guard as if it was.
        push(digit);
    }
}

// Returns:
//     Exact if Guard is _zero_, and appropriate amendments are enabled
//     Down  if Guard is less than half
//     Even  if Guard is exactly half
//     Up    if Guard is greater than half
Number::Guard::Round
Number::Guard::round() const noexcept
{
    // Local "mode" shadows and has the same value as the static thread_local "Number::mode".
    // This ensures the overhead of loading the thread_local is only incurred once.
    auto const mode = Number::getround();

    if (cuspRoundingFix >= MantissaRange::CuspRoundingFix::Enabled330 && empty())
    {
        // No remainder
        return Round::Exact;
    }

    if (mode == RoundingMode::TowardsZero)
        return Round::Down;

    // Also Towards Zero
    if ((mode == RoundingMode::Downward && !sbit_) || (mode == RoundingMode::Upward && sbit_))
    {
        return Round::Down;
    }

    // Away from Zero. Since we checked sbit_ in the previous block, we don't need to check it
    // again.
    if (mode == RoundingMode::Downward || mode == RoundingMode::Upward)
    {
        if (empty())
            return Round::Down;
        return Round::Up;
    }

    XRPL_ASSERT(
        mode == RoundingMode::ToNearest, "xrpl::Number::Guard::Round : fallthrough to ToNearest");
    // assume round to nearest if mode is not one of the predefined values
    if (digits_ > 0x5000'0000'0000'0000)
        return Round::Up;
    if (digits_ < 0x5000'0000'0000'0000)
        return Round::Down;
    if (xbit_)
        return Round::Up;
    return Round::Even;
}

template <UnsignedMantissa T>
void
Number::Guard::bringIntoRange(bool& negative, T& mantissa, int& exponent) const
{
    // Bring mantissa back into the minMantissa / maxMantissa range AFTER
    // rounding.
    if (mantissa < minMantissa &&
        (cuspRoundingFix < MantissaRange::CuspRoundingFix::Enabled330 || mantissa != 0))
    {
        mantissa *= 10;
        --exponent;
    }
    // mantissa should never be 0, but if it _is_ assert, but fall back to making the result kZero.
    if (exponent < kMinExponent ||
        (cuspRoundingFix >= MantissaRange::CuspRoundingFix::Enabled330 && mantissa == 0))
    {
        // Engineers: If you hit this assert, you probably did something wrong in the operation
        // leading up to the rounding work.
        XRPL_ASSERT(mantissa != 0, "xrpl::Number::Guard::bringIntoRange : valid mantissa");
        static constexpr Number kZero = Number{};

        negative = kZero.negative_;
        mantissa = kZero.mantissa_;
        exponent = kZero.exponent_;
    }
}

template <UnsignedMantissa T>
void
Number::Guard::doRoundUp(bool& negative, T& mantissa, int& exponent, std::string location)
{
    pushOverflow(mantissa);

    auto const r = round();
    if (r == Round::Up || (r == Round::Even && (mantissa & 1) == 1))
    {
        auto const safeToIncrement = [this](auto const& mantissa) {
            return mantissa < maxMantissa && mantissa < kMaxRep;
        };
        if (cuspRoundingFix != MantissaRange::CuspRoundingFix::Disabled)
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
                if (cuspRoundingFix >= MantissaRange::CuspRoundingFix::Enabled330 &&
                    mantissa > kMaxRep && mantissa < kMaxRepUp)
                {
                    // When rounding up a value in between kMaxRep, and kMaxRepUp, round to
                    // kMaxRepUp. Note that the decision for this rounding is dominated by the
                    // results of pushOverflow.
                    mantissa = kMaxRepUp;
                }
                else
                {
                    // Incrementing the mantissa will require dividing, which will require rounding.
                    // So _don't_ increment the mantissa. Instead, divide and round recursively. It
                    // should be impossible to recurse more than once, because once the mantissa is
                    // divided by 10, it will be _well_ under maxMantissa and kMaxRep, so adding 1
                    // will have no chance of bringing it back over.
                    doDropDigit(mantissa, exponent);
                    XRPL_ASSERT_PARTS(
                        safeToIncrement(mantissa),
                        "xrpl::Number::Guard::doRoundUp",
                        "can't recurse more than once");
                    doRoundUp(negative, mantissa, exponent, location);
                    return;
                }
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
    else if (
        cuspRoundingFix >= MantissaRange::CuspRoundingFix::Enabled330 && mantissa > kMaxRep &&
        mantissa < kMaxRepUp)
    {
        // When rounding down a value in between kMaxRep, and kMaxRepUp, round to kMaxRep.
        // Note that the decision for this rounding is dominated by the results of pushOverflow.
        mantissa = kMaxRep;
    }
    bringIntoRange(negative, mantissa, exponent);
    if (exponent > kMaxExponent)
        Throw<std::overflow_error>(std::string(location));
}

template <UnsignedMantissa T>
void
Number::Guard::doRoundDown(bool& negative, T& mantissa, int& exponent) const
{
    // Do not pushOverflow here.

    auto r = round();
    if (cuspRoundingFix >= MantissaRange::CuspRoundingFix::Enabled330)
    {
        // If there was any remainder, subtract 1 from the result. This is sufficient to get the
        // best rounding.
        XRPL_ASSERT(
            r == Round::Exact || mantissa > maxMantissa,
            "xrpl::Number::Guard::doRoundDown : mantissa is expected size");
        if (r != Round::Exact)
        {
            --mantissa;
        }
    }
    else
    {
        // Need to preserve the incorrect behavior until the fix amendment can be retired,
        // because otherwise would risk an unplanned ledger fork.
        if (r == Round::Up || (r == Round::Even && (mantissa & 1) == 1))
        {
            --mantissa;
            if (mantissa < minMantissa)
            {
                mantissa *= 10;
                --exponent;
            }
        }
    }
    bringIntoRange(negative, mantissa, exponent);
}

// Modify the result to the correctly rounded value
void
Number::Guard::doRound(rep& drops, std::string location) const
{
    // Do not pushOverflow here.

    auto r = round();
    if (r == Round::Up || (r == Round::Even && (drops & 1) == 1))
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
    XRPL_ASSERT(drops >= 0, "xrpl::Number::Guard::doRound : positive magnitude");

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
    MantissaRange::CuspRoundingFix cuspRoundingFix,
    bool dropped)
{
    static constexpr auto kMinExponent = Number::kMinExponent;
    static constexpr auto kMaxExponent = Number::kMaxExponent;
    auto const repLimit = cuspRoundingFix >= MantissaRange::CuspRoundingFix::Enabled330
        ? Number::kMaxRepUp
        : Number::kMaxRep;

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
    Guard g(minMantissa, maxMantissa, cuspRoundingFix);
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
    if (m > repLimit)
    {
        if (exponent >= kMaxExponent)
            throw std::overflow_error("Number::normalize 1.5");
        g.doDropDigit(m, exponent);
    }
    // Before modification, m should be within the min/max range. After
    // modification, it must be less than repLimit. In other words, the original
    // value should have been no more than repLimit * 10.
    // (repLimit * 10 > maxMantissa)
    XRPL_ASSERT_PARTS(m <= repLimit, "xrpl::doNormalize", "intermediate mantissa fits in limit");
    mantissa = m;

    g.doRoundUp(negative, mantissa, exponent, "Number::normalize 2");
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
    MantissaRange::CuspRoundingFix cuspRoundingFix)
{
    // Not used by every compiler version, and thus not necessarily
    // counted by coverage build
    // LCOV_EXCL_START
    doNormalize(negative, mantissa, exponent, minMantissa, maxMantissa, cuspRoundingFix, false);
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
    MantissaRange::CuspRoundingFix cuspRoundingFix)
{
    // Not used by every compiler version, and thus not necessarily
    // counted by coverage build
    // LCOV_EXCL_START
    doNormalize(negative, mantissa, exponent, minMantissa, maxMantissa, cuspRoundingFix, false);
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
    MantissaRange::CuspRoundingFix cuspRoundingFix)
{
    doNormalize(negative, mantissa, exponent, minMantissa, maxMantissa, cuspRoundingFix, false);
}

void
Number::normalize(MantissaRange const& range)
{
    normalize(negative_, mantissa_, exponent_, range.min, range.max, range.cuspRoundingFix);
}

void
Number::normalize(Guard const& guard)
{
    normalize(
        negative_,
        mantissa_,
        exponent_,
        guard.minMantissa,
        guard.maxMantissa,
        guard.cuspRoundingFix);
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
    Guard g(kRange);

    auto const& minMantissa = g.minMantissa;
    auto const& maxMantissa = g.maxMantissa;
    auto const cuspRoundingFix = g.cuspRoundingFix;

    auto const repLimit =
        cuspRoundingFix >= MantissaRange::CuspRoundingFix::Enabled330 ? kMaxRepUp : kMaxRep;

    // Bring the exponents of both values into agreement, so the mantissas are on the same scale
    //   and can be added directly together.

    auto const upperLimit = static_cast<uint128_t>(g.minMantissa) * 1000;
    // For the "adjust" lambda
    // expandM / expandE: The values for which the mantissa will be expanded, and the exponent
    //  decreased to match. Mantissa won't be expanded beyond upperLimit.
    //  (37e8 == 37000e5 == 37000000e2)
    // shrinkM / shrinkE: The values for which the mantissa will be shrunk, and exponent increased
    //  to match, if necessary.
    auto const adjust = [&g, &upperLimit](
                            uint128_t& expandM, int& expandE, uint128_t& shrinkM, int& shrinkE) {
        // Adjust up and down until the exponents match
        if (g.cuspRoundingFix == MantissaRange::CuspRoundingFix::Enabled330)
        {
            // For Enabled330, there are three steps.
            // 1. First, shrink the mantissa of shrinkM/shrinkE while shrinkM ends in 0.
            while (shrinkE < expandE && shrinkM % 10 == 0)
            {
                g.doDropDigit(shrinkM, shrinkE);
            }

            // 2. Then expand the mantissa of expandM/expandE, with a limit for expandM a few orders
            // of magnitude above the MantissaRange. This will leave a few extra digits for rounding
            // later, but nothing excessive.
            while (shrinkE < expandE && expandE > kMinExponent && expandM < upperLimit)
            {
                expandM *= 10;
                --expandE;
            }
        }

        // 3. Finally, shrink the mantissa of shrinkM/shrinkE until the exponents match. Any removed
        // digits will be put into the Guard. This is the only step for non-Enabled330 modes.
        while (shrinkE < expandE)
        {
            g.doDropDigit(shrinkM, shrinkE);
        }
    };

    // Shrink the mantissa and raise the exponent of the value with the lower exponent. Store any
    // dropped digits in the Guard.
    if (xe < ye)
    {
        if (xn)
            g.setNegative();

        adjust(ym, ye, xm, xe);
    }
    else if (xe > ye)
    {
        if (yn)
            g.setNegative();

        adjust(xm, xe, ym, ye);
    }
    else if (g.cuspRoundingFix == MantissaRange::CuspRoundingFix::Enabled330)
    {
        // Both values have the same exponent.
        // Set the sign of the Guard based on the sign of the Number with the smallest
        // unsigned _mantissa_
        if ((xm < ym && xn) || (ym < xm && yn))
            g.setNegative();
    }

    if (xn == yn)
    {
        xm += ym;

        if (g.cuspRoundingFix >= MantissaRange::CuspRoundingFix::Enabled330)
        {
            // Don't do any adjustments for Enabled330. Normalize will take care of it
            // Because of "adjust", the only way there can be data in the Guard is if we first grew
            // the mantissa past the maxMantissa. Since we added here, it can only get bigger.
            // If xm > maxMantissa, then doNormalize has all the data it needs from the last 3-4
            // digits, plus the "dropped" flag that will be passed in.
            // If not, then the mantissa will only need to be padded out with 0s and won't need to
            // round.
            XRPL_ASSERT(
                xm > maxMantissa || g.empty(),
                "xrpl::Number::operator+ : rounding state expected after add");
        }
        else
        {
            if (xm > maxMantissa || xm > repLimit)
            {
                g.doDropDigit(xm, xe);
            }
            g.doRoundUp(xn, xm, xe, "Number::addition overflow");
        }
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
        if (cuspRoundingFix >= MantissaRange::CuspRoundingFix::Enabled330)
        {
            // Because we subtracted, xm can have any number of digits from 1 up to
            // upperLimit * 10, and g can be in any state. (Note that xm can't be zero, because that
            // special case was tested earlier.)

            // Grow xm/xe and pull digits out of the Guard until xm reaches upperLimit, but stop if
            // the Guard empties out, because no rounding will be necessary. This will ensure that
            // normalize will have enough information to make an accurate rounding decision.
            // (Normalize will pad a small mantissa back into range.) Note that if any digits were
            // lost (xbit_), the Guard will never be empty, so xm will grow larger than upperLimit.
            while (xm < upperLimit && !g.empty())
            {
                xm *= 10;
                xm -= g.pop();
                --xe;
            }
            XRPL_ASSERT(
                xm > maxMantissa || g.empty(),
                "xrpl::Number::operator+ : rounding state expected after subtract");
        }
        else
        {
            // Grow xm/xe and pull digits out of the Guard until it's back in the
            // minMantissa/maxMantissa range.
            while (xm < minMantissa && xm * 10 <= repLimit)
            {
                xm *= 10;
                xm -= g.pop();
                --xe;
            }
        }
        // Rounding down can result in decrementing xm, based on whether there is any data left in
        // the Guard (depending on cuspRoundingFix). Note that if that happens, then the Guard is
        // not empty. For Enabled330, that will also result in the "dropped" flag being passed to
        // doNormalize, which may result in the mantissa being incremented again. It doesn't matter
        // what the dropped digits are, only that they exist. This is because subtracting one
        // "overcorrects", so we know there are still trailing digits to be accounted for in the
        // rounding.
        //
        // This works because
        // 1. The rounding up will be done _after_ the mantissa is brought into range. It may not
        //    be in range right now, and
        // 2. The "dropped" flag is only ever used as a tie-breaker, specifically when rounding
        //    away from zero, and the dropped digits are 0, or when rounding to nearest, and
        //    the dropped digits represent exactly 0.5.
        g.doRoundDown(xn, xm, xe);
    }

    doNormalize(
        xn,
        xm,
        xe,
        minMantissa,
        maxMantissa,
        cuspRoundingFix,
        cuspRoundingFix == MantissaRange::CuspRoundingFix::Enabled330 && !g.empty());
    negative_ = xn;
    mantissa_ = static_cast<internalrep>(xm);
    exponent_ = xe;
    XRPL_ASSERT(isnormal(), "xrpl::Number::operator+= : result is normal");
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
    Guard g(kRange);
    if (zn)
        g.setNegative();

    auto const& maxMantissa = g.maxMantissa;
    auto const repLimit =
        g.cuspRoundingFix >= MantissaRange::CuspRoundingFix::Enabled330 ? kMaxRepUp : kMaxRep;

    while (zm > maxMantissa || zm > repLimit)
    {
        g.doDropDigit(zm, ze);
    }

    xm = static_cast<internalrep>(zm);
    xe = ze;
    g.doRoundUp(zn, xm, xe, "Number::multiplication overflow : exponent is " + std::to_string(xe));
    negative_ = zn;
    mantissa_ = xm;
    exponent_ = xe;

    normalize(g);
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
    auto const dm = static_cast<uint128_t>(y.mantissa_);
    auto const de = y.exponent_;

    auto const& range = kRange.get();
    auto const& minMantissa = range.min;
    auto const& maxMantissa = range.max;
    auto const cuspRoundingFix = range.cuspRoundingFix;

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
    // Stage 3: If there is still a remainder, and the cuspRoundingFix
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
                cuspRoundingFix != MantissaRange::CuspRoundingFix::Disabled;
            if (useTrailingRemainder)
            {
                dropped = partialNumerator % dm != 0;
            }
        }
    }
    doNormalize(zp, zm, ze, minMantissa, maxMantissa, cuspRoundingFix, dropped);
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
    Guard g(kRange);
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
        if (exponent != 0)
        {
            ret.append(1, 'e');
            ret.append(std::to_string(exponent));
        }
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

    // NOLINTNEXTLINE(readability-identifier-naming)
    auto const D = (((((6 * di) + 11) * di) + 6) * di) + 1;
    auto const a0 = 3 * di * ((((2 * di) - 3) * di) + 1);
    auto const a1 = 24 * di * ((2 * di) - 1);
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
