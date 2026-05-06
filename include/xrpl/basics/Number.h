#pragma once

#include <xrpl/beast/utility/instrumentation.h>

#include <concepts>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>

#ifdef _MSC_VER
#include <boost/multiprecision/cpp_int.hpp>
#endif  // !defined(_MSC_VER)

namespace xrpl {

class Number;

std::string
to_string(Number const& amount);

/** Returns a rough estimate of log10(value).
 *
 * The return value is a pair (log, rem), where log is the estimated
 * base-10 logarithm (roughly floor(log10(value))), and rem is value with
 * all trailing 0s removed (i.e., divided by the largest power of 10 that
 * evenly divides value). If rem is 1, then value is an exact power of ten, and
 * log is the exact log10(value).
 *
 * This function only works for positive values.
 */
template <std::unsigned_integral T>
constexpr std::pair<int, T>
logTenEstimate(T value)
{
    int log = 0;
    T remainder = value;
    while (value >= 10)
    {
        if (value % 10 == 0)
            remainder = remainder / 10;
        value /= 10;
        ++log;
    }
    return {log, remainder};
}

template <typename T>
constexpr std::optional<int>
logTen(T value)
{
    auto const est = logTenEstimate(value);
    if (est.second == 1)
        return est.first;
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
 *
 * The MantissaScale enum indicates whether the range is "small" or
 * "large".  This intentionally prevents the creation of any
 * MantissaRanges representing other values.
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
 * by an STAmount - IOUs, XRP, and MPTs. It has a min value of 2^63/10+1
 * (truncated), and a max value of 2^63-1.
 *
 * Note that if the mentioned amendments are eventually retired, this class
 * should be left in place, but the "small" scale option should be removed. This
 * will allow for future expansion beyond 64-bits if it is ever needed.
 */
struct MantissaRange
{
    using rep = std::uint64_t;
    enum class MantissaScale { Small, Large };

    explicit constexpr MantissaRange(MantissaScale scale)
        : max(getMax(scale)), internalMin(getInternalMin(scale, min)), scale(scale)
    {
        // Keep the error messages terse. Since this is constexpr, if any of these throw, it won't
        // compile, so there's no real need to worry about runtime exceptions here.
        if (min * 10 <= max)
            throw std::out_of_range("Invalid mantissa range: min * 10 <= max");
        if (max / 10 >= min)
            throw std::out_of_range("Invalid mantissa range: max / 10 >= min");
        if ((min - 1) * 10 > max)
            throw std::out_of_range("Invalid mantissa range: (min - 1) * 10 > max");
        // This is a little hacky
        if ((max + 10) / 10 < min)
            throw std::out_of_range("Invalid mantissa range: (max + 10) / 10 < min");
        if (computeLog(internalMin) != log)
            throw std::out_of_range("Invalid mantissa range: computeLog(internalMin) != log");
    }

    // Explicitly delete copy and move operations
    MantissaRange(MantissaRange const&) = delete;
    MantissaRange(MantissaRange&&) = delete;
    MantissaRange&
    operator=(MantissaRange const&) = delete;
    MantissaRange&
    operator=(MantissaRange&&) = delete;

    rep max;
    rep min{computeMin(max)};
    /* Used to determine if mantissas are in range, but have fewer digits than max.
     *
     * Unlike min, internalMin is always an exact power of 10, so a mantissa in the internal
     * representation will always have a consistent number of digits.
     */
    rep internalMin;
    int log{computeLog(min)};
    MantissaScale scale;

private:
    static constexpr rep
    getMax(MantissaScale scale)
    {
        switch (scale)
        {
            case MantissaScale::Small:
                return 9'999'999'999'999'999ULL;
            case MantissaScale::Large:
                return std::numeric_limits<std::int64_t>::max();
            default:
                // Since this can never be called outside a non-constexpr
                // context, this throw assures that the build fails if an
                // invalid scale is used.
                throw std::runtime_error("Unknown mantissa scale");
        }
    }

    static constexpr rep
    computeMin(rep max)
    {
        return (max / 10) + 1;
    }

    static constexpr rep
    getInternalMin(MantissaScale scale, rep min)
    {
        switch (scale)
        {
            case MantissaScale::Large:
                return 1'000'000'000'000'000'000ULL;
            default:
                if (isPowerOfTen(min))
                    return min;
                throw std::runtime_error("Unknown/bad mantissa scale");
        }
    }

    static constexpr int
    computeLog(rep min)
    {
        auto const estimate = logTenEstimate(min);
        return estimate.first + (estimate.second == 1 ? 0 : 1);
    }
};

// Like std::integral, but only 64-bit integral types.
template <class T>
concept Integral64 = std::is_same_v<T, std::int64_t> || std::is_same_v<T, std::uint64_t>;

namespace detail {
#ifdef _MSC_VER
using uint128_t = boost::multiprecision::uint128_t;
using int128_t = boost::multiprecision::int128_t;
#else   // !defined(_MSC_VER)
using uint128_t = __uint128_t;
using int128_t = __int128_t;
#endif  // !defined(_MSC_VER)

template <class T>
concept UnsignedMantissa = std::is_unsigned_v<T> || std::is_same_v<T, uint128_t>;
}  // namespace detail

/** Number is a floating point type that can represent a wide range of values.
 *
 * It can represent all values that can be represented by an STAmount -
 * regardless of asset type - XRPAmount, MPTAmount, and IOUAmount, with at least
 * as much precision as those types require.
 *
 * ---- Internal Operational Representation ----
 *
 * Internally, Number is represented with three values:
 *   1. a bool sign flag,
 *   2. a std::uint64_t mantissa,
 *   3. an int exponent.
 *
 * The internal mantissa is an unsigned integer in the range defined by the
 * current MantissaRange. The exponent is an integer in the range
 * [kMIN_EXPONENT, kMAX_EXPONENT].
 *
 * See the description of MantissaRange for more details on the ranges.
 *
 * A non-zero mantissa is (almost) always normalized, meaning it and the
 * exponent are grown or shrunk until the mantissa is in the range
 * [MantissaRange.internalMin, MantissaRange.internalMin * 10 - 1].
 *
 * This internal representation is only used during some operations to ensure
 * that the mantissa is a known, predictable size. The class itself stores the
 * values using the external representation described below.
 *
 * Note:
 *   1. Normalization can be disabled by using the "unchecked" ctor tag. This
 *      should only be used at specific conversion points, some constexpr
 *      values, and in unit tests.
 *   2. Unlike MantissaRange.min, internalMin is always an exact power of 10,
 *      so a mantissa in the internal representation will always have a
 *      consistent number of digits.
 *   3. The functions toInternal() and fromInternal() are used to convert
 *      between the two representations.
 *
 * ---- External Interface ----
 *
 * The external interface of Number consists of a std::int64_t mantissa, which
 * is restricted to 63-bits, and an int exponent, which must be in the range
 * [kMIN_EXPONENT, kMAX_EXPONENT]. The range of the mantissa depends on which
 * MantissaRange is currently active. For the "short" range, the mantissa will
 * be between 10^15 and 10^16-1. For the "large" range, the mantissa will be
 * between -(2^63-1) and 2^63-1. As noted above, the "large" range is needed to
 * represent the full range of valid XRP and MPT integer values accurately.
 *
 * Note:
 *   1. The "large" mantissa range is (2^63/10+1) to 2^63-1. 2^63-1 is between
 *      10^18 and 10^19-1, and (2^63/10+1) is between 10^17 and 10^18-1. Thus,
 *      the mantissa may have 18 or 19 digits. This value will be modified to
 *      always have 19 digits before some operations to ensure consistency.
 *   2. The functions mantissa() and exponent() return the external view of the
 *      Number value, specifically using a signed 63-bit mantissa.
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

    rep mantissa_{0};
    int exponent_{std::numeric_limits<int>::lowest()};

public:
    // The range for the exponent when normalized
    constexpr static int kMIN_EXPONENT = -32768;
    constexpr static int kMAX_EXPONENT = 32768;

    // May need to make unchecked private
    struct Unchecked
    {
        explicit Unchecked() = default;
    };

    // Like unchecked, normalized is used with the ctors that take an
    // internalrep mantissa. Unlike unchecked, those ctors will normalize the
    // value.
    // Only unit tests are expected to use this class
    struct Normalized
    {
        explicit Normalized() = default;
    };

    explicit constexpr Number() = default;

    Number(rep mantissa);
    explicit Number(rep mantissa, int exponent);
    explicit constexpr Number(
        bool negative,
        internalrep mantissa,
        int exponent,
        Unchecked) noexcept;
    // Assume unsigned values are... unsigned. i.e. positive
    explicit constexpr Number(internalrep mantissa, int exponent, Unchecked) noexcept;
    // Only unit tests are expected to use this ctor
    explicit Number(bool negative, internalrep mantissa, int exponent, Normalized);
    // Assume unsigned values are... unsigned. i.e. positive
    explicit Number(internalrep mantissa, int exponent, Normalized);

    [[nodiscard]] constexpr rep
    mantissa() const noexcept;
    [[nodiscard]] constexpr int
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
        return x.mantissa_ == y.mantissa_ && x.exponent_ == y.exponent_;
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
        bool const lneg = x.mantissa_ < 0;
        bool const rneg = y.mantissa_ < 0;

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
    [[nodiscard]] constexpr int
    signum() const noexcept
    {
        if (mantissa_ < 0)
        {
            return -1;
        }
        return (mantissa_ != 0 ? 1 : 0);
    }

    [[nodiscard]] Number
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

    friend Number
    power(Number const& f, unsigned n, unsigned d);

    // Thread local rounding control.  Default is to_nearest
    enum class RoundingMode { ToNearest, TowardsZero, Downward, Upward };

    static RoundingMode
    getround();

    static RoundingMode
    setround(RoundingMode inMode);

    /** Returns which mantissa scale is currently in use for normalization.
     *
     * If you think you need to call this outside of unit tests, no you don't.
     */
    static MantissaRange::MantissaScale
    getMantissaScale();

    /** Changes which mantissa scale is used for normalization.
     *
     * If you think you need to call this outside of unit tests, no you don't.
     */
    static void
    setMantissaScale(MantissaRange::MantissaScale scale);

    static internalrep
    minMantissa()
    {
        return kRANGE.get().min;
    }

    static internalrep
    maxMantissa()
    {
        return kRANGE.get().max;
    }

    static int
    mantissaLog()
    {
        return kRANGE.get().log;
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
    static thread_local RoundingMode mode;
    // The available ranges for mantissa

    constexpr static MantissaRange kSMALL_RANGE{MantissaRange::MantissaScale::Small};
    static_assert(isPowerOfTen(kSMALL_RANGE.min));
    static_assert(kSMALL_RANGE.min == 1'000'000'000'000'000LL);
    static_assert(kSMALL_RANGE.max == 9'999'999'999'999'999LL);
    static_assert(kSMALL_RANGE.internalMin == kSMALL_RANGE.min);
    static_assert(kSMALL_RANGE.log == 15);
    constexpr static MantissaRange kLARGE_RANGE{MantissaRange::MantissaScale::Large};
    static_assert(!isPowerOfTen(kLARGE_RANGE.min));
    static_assert(kLARGE_RANGE.min == 922'337'203'685'477'581ULL);
    static_assert(kLARGE_RANGE.max == internalrep(9'223'372'036'854'775'807ULL));
    static_assert(kLARGE_RANGE.max == std::numeric_limits<rep>::max());
    static_assert(kLARGE_RANGE.internalMin == 1'000'000'000'000'000'000ULL);
    static_assert(kLARGE_RANGE.log == 18);
    // There are 2 values that will not fit in kLARGE_RANGE without some extra
    // work
    // * 9223372036854775808
    // * 9223372036854775809
    // They both end up < min, but with a leftover. If they round up, everything
    // will be fine. If they don't, we'll need to bring them up into range.
    // Guard::bringIntoRange handles this situation.

    // The range for the mantissa when normalized.
    // Use reference_wrapper to avoid making copies, and prevent accidentally
    // changing the values inside the range.
    static thread_local std::reference_wrapper<MantissaRange const> kRANGE;

    // And one is needed because it needs to choose between oneSmall and
    // oneLarge based on the current range
    static Number
    one(MantissaRange const& range);

    static Number
    root(MantissaRange const& range, Number f, unsigned d);

    void
    normalize(MantissaRange const& range);

    void
    normalize();

    /** Normalize Number components to an arbitrary range.
     *
     * min/maxMantissa are parameters because this function is used by both
     * normalize(), which reads from kRANGE, and by normalizeToRange,
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
        T& mantissa,
        int& exponent,
        MantissaRange::rep const& minMantissa,
        MantissaRange::rep const& maxMantissa);

    [[nodiscard]]
    bool
    isnormal(MantissaRange const& range) const noexcept;

    [[nodiscard]] bool
    isnormal() const noexcept;

    // Copy the number, but modify the exponent by "exponentDelta". Because the
    // mantissa doesn't change, the result will be "mostly" normalized, but the
    // exponent could go out of range, so it will be checked.
    [[nodiscard]] Number
    shiftExponent(int exponentDelta) const;

    // Safely return the absolute value of a rep (int64) mantissa as an internalrep (uint64).
    static internalrep
    externalToInternal(rep mantissa);

    /** Breaks down the number into components, potentially de-normalizing it.
     *
     * Ensures that the mantissa always has kRANGE.log + 1 digits.
     *
     */
    template <detail::UnsignedMantissa Rep = internalrep>
    std::tuple<bool, Rep, int>
    toInternal(MantissaRange const& range) const;

    /** Breaks down the number into components, potentially de-normalizing it.
     *
     * Ensures that the mantissa always has kRANGE.log + 1 digits.
     *
     */
    template <detail::UnsignedMantissa Rep = internalrep>
    std::tuple<bool, Rep, int>
    toInternal() const;

    /** Rebuilds the number from components.
     *
     * If "expectNormal" is true, the values are expected to be normalized - all
     * in their valid ranges.
     *
     * If "expectNormal" is false, the values are expected to be "near
     * normalized", meaning that the mantissa has to be modified at most once to
     * bring it back into range.
     *
     */
    template <bool expectNormal = true, detail::UnsignedMantissa Rep = internalrep>
    void
    fromInternal(bool negative, Rep mantissa, int exponent, MantissaRange const* pRange);

    /** Rebuilds the number from components.
     *
     * If "expectNormal" is true, the values are expected to be normalized - all
     * in their valid ranges.
     *
     * If "expectNormal" is false, the values are expected to be "near
     * normalized", meaning that the mantissa has to be modified at most once to
     * bring it back into range.
     *
     */
    template <bool expectNormal = true, detail::UnsignedMantissa Rep = internalrep>
    void
    fromInternal(bool negative, Rep mantissa, int exponent);

    class Guard;

public:
    constexpr static internalrep kLARGEST_MANTISSA = kLARGE_RANGE.max;
};

inline constexpr Number::Number(
    bool negative,
    internalrep mantissa,
    int exponent,
    Unchecked) noexcept
    : mantissa_{negative ? -static_cast<rep>(mantissa) : static_cast<rep>(mantissa)}
    , exponent_{exponent}
{
}

constexpr Number::Number(internalrep mantissa, int exponent, Unchecked) noexcept
    : Number(false, mantissa, exponent, Unchecked{})
{
}

constexpr static Number kNUM_ZERO{};

inline Number::Number(internalrep mantissa, int exponent, Normalized)
    : Number(false, mantissa, exponent, Normalized{})
{
}

inline Number::Number(rep mantissa, int exponent)
    : Number(mantissa < 0, externalToInternal(mantissa), exponent, Normalized{})
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
constexpr Number::rep
Number::mantissa() const noexcept
{
    return mantissa_;
}

/** Returns the exponent of the external view of the Number.
 *
 * Please see the "---- External Interface ----" section of the class
 * documentation for an explanation of why the internal value may be modified.
 */
constexpr int
Number::exponent() const noexcept
{
    return exponent_;
}

constexpr Number
Number::operator+() const noexcept
{
    return *this;
}

constexpr Number
Number::operator-() const noexcept
{
    if (mantissa_ == 0)
        return Number{};
    auto x = *this;
    x.mantissa_ = -x.mantissa_;
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
    return Number{false, kRANGE.get().min, kMIN_EXPONENT, Unchecked{}};
}

inline Number
Number::max() noexcept
{
    return Number{false, kRANGE.get().max, kMAX_EXPONENT, Unchecked{}};
}

inline Number
Number::lowest() noexcept
{
    return Number{true, kRANGE.get().max, kMAX_EXPONENT, Unchecked{}};
}

inline bool
Number::isnormal(MantissaRange const& range) const noexcept
{
    auto const absM = externalToInternal(mantissa_);

    return *this == Number{} ||
        (range.min <= absM && absM <= range.max &&  //
         kMIN_EXPONENT <= exponent_ && exponent_ <= kMAX_EXPONENT);
}

inline bool
Number::isnormal() const noexcept
{
    return isnormal(kRANGE);
}

template <Integral64 T>
std::pair<T, int>
Number::normalizeToRange(T minMantissa, T maxMantissa) const
{
    bool negative = mantissa_ < 0;
    internalrep mantissa = externalToInternal(mantissa_);
    int exponent = exponent_;

    if constexpr (std::is_unsigned_v<T>)
    {
        XRPL_ASSERT_PARTS(
            !negative,
            "xrpl::Number::normalizeToRange",
            "Number is non-negative for unsigned range.");
        // To avoid logical errors in release builds, throw if the Number is
        // negative for an unsigned range.
        if (negative)
            throw std::runtime_error(
                "Number::normalizeToRange: Number is negative for "
                "unsigned range.");
    }
    Number::normalize(negative, mantissa, exponent, minMantissa, maxMantissa);

    // Cast mantissa to signed type first (if T is a signed type) to avoid
    // unsigned integer overflow when multiplying by negative sign
    T signedMantissa = negative ? -static_cast<T>(mantissa) : static_cast<T>(mantissa);
    return std::make_pair(signedMantissa, exponent);
}

constexpr Number
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

constexpr Number
squelch(Number const& x, Number const& limit) noexcept
{
    if (abs(x) < limit)
        return Number{};
    return x;
}

inline std::string
to_string(MantissaRange::MantissaScale const& scale)
{
    switch (scale)
    {
        case MantissaRange::MantissaScale::Small:
            return "small";
        case MantissaRange::MantissaScale::Large:
            return "large";
        default:
            throw std::runtime_error("Bad scale");
    }
}

class SaveNumberRoundMode
{
    Number::RoundingMode mode_;

public:
    ~SaveNumberRoundMode()
    {
        Number::setround(mode_);
    }
    explicit SaveNumberRoundMode(Number::RoundingMode mode) noexcept : mode_{mode}
    {
    }
    SaveNumberRoundMode(SaveNumberRoundMode const&) = delete;
    SaveNumberRoundMode&
    operator=(SaveNumberRoundMode const&) = delete;
};

// saveNumberRoundMode doesn't do quite enough for us.  What we want is a
// Number::RoundModeGuard that sets the new mode and restores the old mode
// when it leaves scope.  Since Number doesn't have that facility, we'll
// build it here.
class NumberRoundModeGuard
{
    SaveNumberRoundMode saved_;

public:
    explicit NumberRoundModeGuard(Number::RoundingMode mode) noexcept
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
    MantissaRange::MantissaScale const saved_;

public:
    explicit NumberMantissaScaleGuard(MantissaRange::MantissaScale scale) noexcept
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
