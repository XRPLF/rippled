#pragma once

#include <xrpl/beast/utility/instrumentation.h>

#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <ostream>
#include <string>

namespace xrpl {

class Number;

std::string
to_string(Number const& amount);

template <typename T>
constexpr std::optional<int>
logTen(T value)
{
    int log = 0;
    while (value >= 10 && value % 10 == 0)
    {
        value /= 10;
        ++log;
    }
    if (value == 1)
        return log;
    return std::nullopt;
}

template <typename T>
constexpr bool
isPowerOfTen(T value)
{
    return logTen(value).has_value();
}

/** MantissaRange defines a range for the mantissa of a normalized Number.
 *
 * The mantissa is in the range [min, max], where
 * * min is a power of 10, and
 * * max = min * 10 - 1.
 *
 * The mantissa_scale enum indicates whether the range is "small" or "large".
 * This intentionally restricts the number of MantissaRanges that can be
 * instantiated to two: one for each scale.
 *
 * The "small" scale is based on the behavior of STAmount for IOUs. It has a min
 * value of 10^15, and a max value of 10^16-1. This was sufficient for
 * uses before Lending Protocol was implemented, mostly related to AMM.
 *
 * However, it does not have sufficient precision to represent the full integer
 * range of int64_t values (-2^63 to 2^63-1), which are needed for XRP and MPT
 * values. The implementation of SingleAssetVault, and LendingProtocol need to
 * represent those integer values accurately and precisely, both for the
 * STNumber field type, and for internal calculations. That necessitated the
 * "large" scale.
 *
 * The "large" scale is intended to represent all values that can be represented
 * by an STAmount - IOUs, XRP, and MPTs. It has a min value of 10^18, and a max
 * value of 10^19-1.
 *
 * Note that if the mentioned amendments are eventually retired, this class
 * should be left in place, but the "small" scale option should be removed. This
 * will allow for future expansion beyond 64-bits if it is ever needed.
 */
struct MantissaRange
{
    using rep = std::uint64_t;
    enum mantissa_scale { small, large };

    explicit constexpr MantissaRange(mantissa_scale scale_)
        : min(getMin(scale_)), max(min * 10 - 1), log(logTen(min).value_or(-1)), scale(scale_)
    {
    }

    rep min;
    rep max;
    int log;
    mantissa_scale scale;

private:
    static constexpr rep
    getMin(mantissa_scale scale_)
    {
        switch (scale_)
        {
            case small:
                return 1'000'000'000'000'000ULL;
            case large:
                return 1'000'000'000'000'000'000ULL;
            default:
                // Since this can never be called outside a non-constexpr
                // context, this throw assures that the build fails if an
                // invalid scale is used.
                throw std::runtime_error("Unknown mantissa scale");
        }
    }
};

// Like std::integral, but only 64-bit integral types.
template <class T>
concept Integral64 = std::is_same_v<T, std::int64_t> || std::is_same_v<T, std::uint64_t>;

/** Number is a floating point type that can represent a wide range of values.
 *
 * It can represent all values that can be represented by an STAmount -
 * regardless of asset type - XRPAmount, MPTAmount, and IOUAmount, with at least
 * as much precision as those types require.
 *
 * ---- Internal Representation ----
 *
 * Internally, Number is represented with three values:
 *   1. a bool sign flag,
 *   2. a std::uint64_t mantissa,
 *   3. an int exponent.
 *
 * The internal mantissa is an unsigned integer in the range defined by the
 * current MantissaRange. The exponent is an integer in the range
 * [minExponent, maxExponent].
 *
 * See the description of MantissaRange for more details on the ranges.
 *
 * A non-zero mantissa is (almost) always normalized, meaning it and the
 * exponent are grown or shrunk until the mantissa is in the range
 * [MantissaRange.min, MantissaRange.max].
 *
 * Note:
 *   1. Normalization can be disabled by using the "unchecked" ctor tag. This
 *      should only be used at specific conversion points, some constexpr
 *      values, and in unit tests.
 *   2. The max of the "large" range, 10^19-1, is the largest 10^X-1 value that
 *      fits in an unsigned 64-bit number. (10^19-1 < 2^64-1 and
 *      10^20-1 > 2^64-1). This avoids under- and overflows.
 *
 * ---- External Interface ----
 *
 * The external interface of Number consists of a std::int64_t mantissa, which
 * is restricted to 63-bits, and an int exponent, which must be in the range
 * [minExponent, maxExponent]. The range of the mantissa depends on which
 * MantissaRange is currently active. For the "short" range, the mantissa will
 * be between 10^15 and 10^16-1. For the "large" range, the mantissa will be
 * between -(2^63-1) and 2^63-1. As noted above, the "large" range is needed to
 * represent the full range of valid XRP and MPT integer values accurately.
 *
 * Note:
 *   1. 2^63-1 is between 10^18 and 10^19-1, which are the limits of the "large"
 *      mantissa range.
 *   2. The functions mantissa() and exponent() return the external view of the
 *      Number value, specifically using a signed 63-bit mantissa. This may
 *      require altering the internal representation to fit into that range
 *      before the value is returned. The interface guarantees consistency of
 *      the two values.
 *   3. Number cannot represent -2^63 (std::numeric_limits<std::int64_t>::min())
 *      as an exact integer, but it doesn't need to, because all asset values
 *      on-ledger are non-negative. This is due to implementation details of
 *      several operations which use unsigned arithmetic internally. This is
 *      sufficient to represent all valid XRP values (where the absolute value
 *      can not exceed INITIAL_XRP: 10^17), and MPT values (where the absolute
 *      value can not exceed maxMPTokenAmount: 2^63-1).
 *
 * ---- Mantissa Range Switching ----
 *
 * The mantissa range may be changed at runtime via setMantissaScale(). The
 * default mantissa range is "large". The range is updated whenever transaction
 * processing begins, based on whether SingleAssetVault or LendingProtocol are
 * enabled. If either is enabled, the mantissa range is set to "large". If not,
 * it is set to "small", preserving backward compatibility and correct
 * "amendment-gating".
 *
 * It is extremely unlikely that any more calls to setMantissaScale() will be
 * needed outside of unit tests.
 *
 * ---- Usage With Different Ranges ----
 *
 * Outside of unit tests, and existing checks, code that uses Number should not
 * know or care which mantissa range is active.
 *
 * The results of computations using Numbers with a small mantissa may differ
 * from computations using Numbers with a large mantissa, specifically as it
 * effects the results after rounding. That is why the large mantissa range is
 * amendment gated in transaction processing.
 *
 * It is extremely unlikely that any more calls to getMantissaScale() will be
 * needed outside of unit tests.
 *
 * Code that uses Number should not assume or check anything about the
 * mantissa() or exponent() except that they fit into the "large" range
 * specified in the "External Interface" section.
 *
 * ----- Unit Tests -----
 *
 * Within unit tests, it may be useful to explicitly switch between the two
 * ranges, or to check which range is active when checking the results of
 * computations. If the test is doing the math directly, the
 * set/getMantissaScale() functions may be most appropriate. However, if the
 * test has anything to do with transaction processing, it should enable or
 * disable the amendments that control the mantissa range choice
 * (SingleAssetVault and LendingProtocol), and/or check if either of those
 * amendments are enabled to determine which result to expect.
 *
 */
class Number
{
    using rep = std::int64_t;
    using internalrep = MantissaRange::rep;

    bool negative_{false};
    internalrep mantissa_{0};
    int exponent_{std::numeric_limits<int>::lowest()};

public:
    // The range for the exponent when normalized
    constexpr static int minExponent = -32768;
    constexpr static int maxExponent = 32768;

    constexpr static internalrep maxRep = std::numeric_limits<rep>::max();
    static_assert(maxRep == 9'223'372'036'854'775'807);
    static_assert(-maxRep == std::numeric_limits<rep>::min() + 1);

    // May need to make unchecked private
    struct unchecked
    {
        explicit unchecked() = default;
    };

    // Like unchecked, normalized is used with the ctors that take an
    // internalrep mantissa. Unlike unchecked, those ctors will normalize the
    // value.
    // Only unit tests are expected to use this class
    struct normalized
    {
        explicit normalized() = default;
    };

    explicit constexpr Number() = default;

    Number(rep mantissa);
    explicit Number(rep mantissa, int exponent);
    explicit constexpr Number(
        bool negative,
        internalrep mantissa,
        int exponent,
        unchecked) noexcept;
    // Assume unsigned values are... unsigned. i.e. positive
    explicit constexpr Number(internalrep mantissa, int exponent, unchecked) noexcept;
    // Only unit tests are expected to use this ctor
    explicit Number(bool negative, internalrep mantissa, int exponent, normalized);
    // Assume unsigned values are... unsigned. i.e. positive
    explicit Number(internalrep mantissa, int exponent, normalized);

    constexpr rep
    mantissa() const noexcept;
    constexpr int
    exponent() const noexcept;

    constexpr Number
    operator+() const noexcept;
    constexpr Number
    operator-() const noexcept;
    Number&
    operator++();
    Number
    operator++(int);
    Number&
    operator--();
    Number
    operator--(int);

    Number&
    operator+=(Number const& x);
    Number&
    operator-=(Number const& x);

    Number&
    operator*=(Number const& x);
    Number&
    operator/=(Number const& x);

    static Number
    min() noexcept;
    static Number
    max() noexcept;
    static Number
    lowest() noexcept;

    /** Conversions to Number are implicit and conversions away from Number
     *  are explicit. This design encourages and facilitates the use of Number
     *  as the preferred type for floating point arithmetic as it makes
     *  "mixed mode" more convenient, e.g. MPTAmount + Number.
     */
    explicit
    operator rep() const;  // round to nearest, even on tie

    friend constexpr bool
    operator==(Number const& x, Number const& y) noexcept
    {
        return x.negative_ == y.negative_ && x.mantissa_ == y.mantissa_ &&
            x.exponent_ == y.exponent_;
    }

    friend constexpr bool
    operator!=(Number const& x, Number const& y) noexcept
    {
        return !(x == y);
    }

    friend constexpr bool
    operator<(Number const& x, Number const& y) noexcept
    {
        // If the two amounts have different signs (zero is treated as positive)
        // then the comparison is true iff the left is negative.
        bool const lneg = x.negative_;
        bool const rneg = y.negative_;

        if (lneg != rneg)
            return lneg;

        // Both have same sign and the left is zero: the right must be
        // greater than 0.
        if (x.mantissa_ == 0)
            return y.mantissa_ > 0;

        // Both have same sign, the right is zero and the left is non-zero.
        if (y.mantissa_ == 0)
            return false;

        // Both have the same sign, compare by exponents:
        if (x.exponent_ > y.exponent_)
            return lneg;
        if (x.exponent_ < y.exponent_)
            return !lneg;

        // If equal exponents, compare mantissas
        return x.mantissa_ < y.mantissa_;
    }

    /** Return the sign of the amount */
    constexpr int
    signum() const noexcept
    {
        return negative_ ? -1 : (mantissa_ ? 1 : 0);
    }

    Number
    truncate() const noexcept;

    friend constexpr bool
    operator>(Number const& x, Number const& y) noexcept
    {
        return y < x;
    }

    friend constexpr bool
    operator<=(Number const& x, Number const& y) noexcept
    {
        return !(y < x);
    }

    friend constexpr bool
    operator>=(Number const& x, Number const& y) noexcept
    {
        return !(x < y);
    }

    friend std::ostream&
    operator<<(std::ostream& os, Number const& x)
    {
        return os << to_string(x);
    }

    friend std::string
    to_string(Number const& amount);

    friend Number
    root(Number f, unsigned d);

    friend Number
    root2(Number f);

    // Thread local rounding control.  Default is to_nearest
    enum rounding_mode { to_nearest, towards_zero, downward, upward };
    static rounding_mode
    getround();
    // Returns previously set mode
    static rounding_mode
    setround(rounding_mode mode);

    /** Returns which mantissa scale is currently in use for normalization.
     *
     * If you think you need to call this outside of unit tests, no you don't.
     */
    static MantissaRange::mantissa_scale
    getMantissaScale();
    /** Changes which mantissa scale is used for normalization.
     *
     * If you think you need to call this outside of unit tests, no you don't.
     */
    static void
    setMantissaScale(MantissaRange::mantissa_scale scale);

    inline static internalrep
    minMantissa()
    {
        return range_.get().min;
    }

    inline static internalrep
    maxMantissa()
    {
        return range_.get().max;
    }

    inline static int
    mantissaLog()
    {
        return range_.get().log;
    }

    /// oneSmall is needed because the ranges are private
    constexpr static Number
    oneSmall();
    /// oneLarge is needed because the ranges are private
    constexpr static Number
    oneLarge();

    // And one is needed because it needs to choose between oneSmall and
    // oneLarge based on the current range
    static Number
    one();

    template <Integral64 T>
    [[nodiscard]]
    std::pair<T, int>
    normalizeToRange(T minMantissa, T maxMantissa) const;

private:
    static thread_local rounding_mode mode_;
    // The available ranges for mantissa

    constexpr static MantissaRange smallRange{MantissaRange::small};
    static_assert(isPowerOfTen(smallRange.min));
    static_assert(smallRange.min == 1'000'000'000'000'000LL);
    static_assert(smallRange.max == 9'999'999'999'999'999LL);
    static_assert(smallRange.log == 15);
    static_assert(smallRange.min < maxRep);
    static_assert(smallRange.max < maxRep);
    constexpr static MantissaRange largeRange{MantissaRange::large};
    static_assert(isPowerOfTen(largeRange.min));
    static_assert(largeRange.min == 1'000'000'000'000'000'000ULL);
    static_assert(largeRange.max == internalrep(9'999'999'999'999'999'999ULL));
    static_assert(largeRange.log == 18);
    static_assert(largeRange.min < maxRep);
    static_assert(largeRange.max > maxRep);

    // The range for the mantissa when normalized.
    // Use reference_wrapper to avoid making copies, and prevent accidentally
    // changing the values inside the range.
    static thread_local std::reference_wrapper<MantissaRange const> range_;

    void
    normalize();

    /** Normalize Number components to an arbitrary range.
     *
     * min/maxMantissa are parameters because this function is used by both
     * normalize(), which reads from range_, and by normalizeToRange,
     * which is public and can accept an arbitrary range from the caller.
     */
    template <class T>
    static void
    normalize(
        bool& negative,
        T& mantissa,
        int& exponent,
        internalrep const& minMantissa,
        internalrep const& maxMantissa);

    template <class T>
    friend void
    doNormalize(
        bool& negative,
        T& mantissa_,
        int& exponent_,
        MantissaRange::rep const& minMantissa,
        MantissaRange::rep const& maxMantissa);

    bool
    isnormal() const noexcept;

    // Copy the number, but modify the exponent by "exponentDelta". Because the
    // mantissa doesn't change, the result will be "mostly" normalized, but the
    // exponent could go out of range, so it will be checked.
    Number
    shiftExponent(int exponentDelta) const;

    // Safely convert rep (int64) mantissa to internalrep (uint64). If the rep
    // is negative, returns the positive value. This takes a little extra work
    // because converting std::numeric_limits<std::int64_t>::min() flirts with
    // UB, and can vary across compilers.
    static internalrep
    externalToInternal(rep mantissa);

    class Guard;
};

inline constexpr Number::Number(
    bool negative,
    internalrep mantissa,
    int exponent,
    unchecked) noexcept
    : negative_(negative), mantissa_{mantissa}, exponent_{exponent}
{
}

inline constexpr Number::Number(internalrep mantissa, int exponent, unchecked) noexcept
    : Number(false, mantissa, exponent, unchecked{})
{
}

constexpr static Number numZero{};

inline Number::Number(bool negative, internalrep mantissa, int exponent, normalized)
    : Number(negative, mantissa, exponent, unchecked{})
{
    normalize();
}

inline Number::Number(internalrep mantissa, int exponent, normalized)
    : Number(false, mantissa, exponent, normalized{})
{
}

inline Number::Number(rep mantissa, int exponent)
    : Number(mantissa < 0, externalToInternal(mantissa), exponent, normalized{})
{
}

inline Number::Number(rep mantissa) : Number{mantissa, 0}
{
}

/** Returns the mantissa of the external view of the Number.
 *
 * Please see the "---- External Interface ----" section of the class
 * documentation for an explanation of why the internal value may be modified.
 */
inline constexpr Number::rep
Number::mantissa() const noexcept
{
    auto m = mantissa_;
    if (m > maxRep)
    {
        XRPL_ASSERT_PARTS(
            !isnormal() || (m % 10 == 0 && m / 10 <= maxRep),
            "xrpl::Number::mantissa",
            "large normalized mantissa has no remainder");
        m /= 10;
    }
    auto const sign = negative_ ? -1 : 1;
    return sign * static_cast<Number::rep>(m);
}

/** Returns the exponent of the external view of the Number.
 *
 * Please see the "---- External Interface ----" section of the class
 * documentation for an explanation of why the internal value may be modified.
 */
inline constexpr int
Number::exponent() const noexcept
{
    auto e = exponent_;
    if (mantissa_ > maxRep)
    {
        XRPL_ASSERT_PARTS(
            !isnormal() || (mantissa_ % 10 == 0 && mantissa_ / 10 <= maxRep),
            "xrpl::Number::exponent",
            "large normalized mantissa has no remainder");
        ++e;
    }
    return e;
}

inline constexpr Number
Number::operator+() const noexcept
{
    return *this;
}

inline constexpr Number
Number::operator-() const noexcept
{
    if (mantissa_ == 0)
        return Number{};
    auto x = *this;
    x.negative_ = !x.negative_;
    return x;
}

inline Number&
Number::operator++()
{
    *this += one();
    return *this;
}

inline Number
Number::operator++(int)
{
    auto x = *this;
    ++(*this);
    return x;
}

inline Number&
Number::operator--()
{
    *this -= one();
    return *this;
}

inline Number
Number::operator--(int)
{
    auto x = *this;
    --(*this);
    return x;
}

inline Number&
Number::operator-=(Number const& x)
{
    return *this += -x;
}

inline Number
operator+(Number const& x, Number const& y)
{
    auto z = x;
    z += y;
    return z;
}

inline Number
operator-(Number const& x, Number const& y)
{
    auto z = x;
    z -= y;
    return z;
}

inline Number
operator*(Number const& x, Number const& y)
{
    auto z = x;
    z *= y;
    return z;
}

inline Number
operator/(Number const& x, Number const& y)
{
    auto z = x;
    z /= y;
    return z;
}

inline Number
Number::min() noexcept
{
    return Number{false, range_.get().min, minExponent, unchecked{}};
}

inline Number
Number::max() noexcept
{
    return Number{false, std::min(range_.get().max, maxRep), maxExponent, unchecked{}};
}

inline Number
Number::lowest() noexcept
{
    return Number{true, std::min(range_.get().max, maxRep), maxExponent, unchecked{}};
}

inline bool
Number::isnormal() const noexcept
{
    MantissaRange const& range = range_;
    auto const abs_m = mantissa_;
    return *this == Number{} ||
        (range.min <= abs_m && abs_m <= range.max && (abs_m <= maxRep || abs_m % 10 == 0) &&
         minExponent <= exponent_ && exponent_ <= maxExponent);
}

template <Integral64 T>
std::pair<T, int>
Number::normalizeToRange(T minMantissa, T maxMantissa) const
{
    bool negative = negative_;
    internalrep mantissa = mantissa_;
    int exponent = exponent_;

    if constexpr (std::is_unsigned_v<T>)
        XRPL_ASSERT_PARTS(
            !negative,
            "xrpl::Number::normalizeToRange",
            "Number is non-negative for unsigned range.");
    Number::normalize(negative, mantissa, exponent, minMantissa, maxMantissa);

    auto const sign = negative ? -1 : 1;
    return std::make_pair(static_cast<T>(sign * mantissa), exponent);
}

inline constexpr Number
abs(Number x) noexcept
{
    if (x < Number{})
        x = -x;
    return x;
}

// Returns f^n
// Uses a log_2(n) number of multiplications

Number
power(Number const& f, unsigned n);

// Returns f^(1/d)
// Uses Newton–Raphson iterations until the result stops changing
// to find the root of the polynomial g(x) = x^d - f

Number
root(Number f, unsigned d);

Number
root2(Number f);

// Returns f^(n/d)

Number
power(Number const& f, unsigned n, unsigned d);

// Return 0 if abs(x) < limit, else returns x

inline constexpr Number
squelch(Number const& x, Number const& limit) noexcept
{
    if (abs(x) < limit)
        return Number{};
    return x;
}

inline std::string
to_string(MantissaRange::mantissa_scale const& scale)
{
    switch (scale)
    {
        case MantissaRange::small:
            return "small";
        case MantissaRange::large:
            return "large";
        default:
            throw std::runtime_error("Bad scale");
    }
}

class saveNumberRoundMode
{
    Number::rounding_mode mode_;

public:
    ~saveNumberRoundMode()
    {
        Number::setround(mode_);
    }
    explicit saveNumberRoundMode(Number::rounding_mode mode) noexcept : mode_{mode}
    {
    }
    saveNumberRoundMode(saveNumberRoundMode const&) = delete;
    saveNumberRoundMode&
    operator=(saveNumberRoundMode const&) = delete;
};

// saveNumberRoundMode doesn't do quite enough for us.  What we want is a
// Number::RoundModeGuard that sets the new mode and restores the old mode
// when it leaves scope.  Since Number doesn't have that facility, we'll
// build it here.
class NumberRoundModeGuard
{
    saveNumberRoundMode saved_;

public:
    explicit NumberRoundModeGuard(Number::rounding_mode mode) noexcept
        : saved_{Number::setround(mode)}
    {
    }

    NumberRoundModeGuard(NumberRoundModeGuard const&) = delete;

    NumberRoundModeGuard&
    operator=(NumberRoundModeGuard const&) = delete;
};

/** Sets the new scale and restores the old scale when it leaves scope.
 *
 * If you think you need to use this class outside of unit tests, no you don't.
 *
 */
class NumberMantissaScaleGuard
{
    MantissaRange::mantissa_scale const saved_;

public:
    explicit NumberMantissaScaleGuard(MantissaRange::mantissa_scale scale) noexcept
        : saved_{Number::getMantissaScale()}
    {
        Number::setMantissaScale(scale);
    }

    ~NumberMantissaScaleGuard()
    {
        Number::setMantissaScale(saved_);
    }

    NumberMantissaScaleGuard(NumberMantissaScaleGuard const&) = delete;

    NumberMantissaScaleGuard&
    operator=(NumberMantissaScaleGuard const&) = delete;
};

}  // namespace xrpl
