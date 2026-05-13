/** @file
 *  Complete arithmetic implementation of `xrpl::Number`, the XRPL ledger's
 *  custom fixed-precision decimal floating-point type.
 *
 *  `Number` was introduced to replace ad-hoc arithmetic embedded in `STAmount`
 *  and related types, giving the ledger a single, auditable numeric type that
 *  supports all asset classes — IOU amounts, XRP drops, and MPT amounts — with
 *  correct, amendment-controlled precision.
 *
 *  Two overriding design constraints dominate the implementation:
 *   - **Exact decimal rounding** to satisfy on-ledger determinism requirements.
 *   - **Dual mantissa precision** selected at runtime (via thread-local state)
 *     based on which amendments are active: 16 significant digits (small range)
 *     for legacy IOU arithmetic, and 19 significant digits (large range) for
 *     full `int64_t` integer fidelity required by XRP drops and MPT amounts.
 */
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

/** Thread-local rounding mode; defaults to `ToNearest` (round-half-to-even).
 *
 *  Each worker thread owns an independent copy so that amendment-gate changes
 *  at transaction start do not race with arithmetic already in progress.
 */
thread_local Number::RoundingMode Number::mode = Number::RoundingMode::ToNearest;

/** Thread-local active mantissa range; defaults to `kLARGE_RANGE`.
 *
 *  Stored as a `reference_wrapper` so that switching ranges is a cheap pointer
 *  swap and so the range constants themselves cannot be mutated accidentally.
 *  Changed via `setMantissaScale()` at the start of each transaction context.
 */
thread_local std::reference_wrapper<MantissaRange const> Number::kRANGE = kLARGE_RANGE;

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
    return kRANGE.get().scale;
}

void
Number::setMantissaScale(MantissaRange::MantissaScale scale)
{
    if (scale != MantissaRange::MantissaScale::Small &&
        scale != MantissaRange::MantissaScale::Large)
        logicError("Unknown mantissa scale");
    kRANGE = scale == MantissaRange::MantissaScale::Small ? kSMALL_RANGE : kLARGE_RANGE;
}

/** Concept restricting template parameters to unsigned 64-bit or 128-bit integer
 *  types used as mantissa carriers throughout the arithmetic implementation.
 */
template <class T>
concept UnsignedMantissa = std::is_unsigned_v<T> || std::is_same_v<T, uint128_t>;

/** Accumulates extra decimal digits shed during normalization so the final
 *  result can be correctly rounded without accumulated truncation error.
 *
 *  Guard stores up to 16 decimal guard digits packed as 4-bit BCD nibbles in a
 *  single `uint64_t`.  A one-bit sticky flag (`xbit_`) records whether any
 *  non-zero digit was ever shifted off the bottom of the guard register; this
 *  is used by `round()` to break ties correctly for `ToNearest` mode.
 *
 *  The usual protocol is `push(d)` when a digit falls out of the main
 *  mantissa, then `doRoundUp` or `doRoundDown` once to apply the rounding
 *  decision.  For subtraction, `pop()` reclaims the most-significant guard
 *  digit back into the mantissa.
 */
class Number::Guard
{
    /** Packed BCD: 16 digits × 4 bits, most-significant digit at bit 63. */
    std::uint64_t digits_{0};
    /** Sticky bit: set when any non-zero digit has been shifted off the bottom. */
    std::uint8_t xbit_ : 1 {0};
    /** Sign of the operand that owns these guard digits. */
    std::uint8_t sbit_ : 1 {0};

public:
    explicit Guard() = default;

    // set & test the sign bit
    void
    setPositive() noexcept;
    void
    setNegative() noexcept;
    [[nodiscard]] bool
    isNegative() const noexcept;

    /** Push one decimal digit into the top of the guard register.
     *
     *  Any digit previously at the bottom is OR-ed into `xbit_` before it
     *  is lost.  Only the lowest 4 bits of `d` are used.
     *
     *  @tparam T An unsigned or `uint128_t` type.
     *  @param d  The digit to store (0–9; higher bits are masked off).
     */
    template <class T>
    void
    push(T d) noexcept;

    /** Recover the most-significant guard digit, shifting the rest down.
     *
     *  Used during subtraction to reclaim precision lost when aligning
     *  exponents.
     *
     *  @return The top digit (0–9).
     */
    unsigned
    pop() noexcept;

    /** Determine whether the guard digits are above, below, or exactly half.
     *
     *  Interprets the current thread-local rounding mode and the packed guard
     *  value to produce a rounding direction consistent with IEEE 754.
     *
     *  @return  1  if the guard value is more than half (round up),
     *           -1 if the guard value is less than half (round down),
     *            0 if the guard value is exactly half (tie — round to even).
     */
    [[nodiscard]] int
    round() const noexcept;

    /** Apply upward rounding to a normalized `(negative, mantissa, exponent)` triple.
     *
     *  Increments the mantissa when `round()` indicates an upward adjustment
     *  (including ties rounded to even).  If the increment pushes the mantissa
     *  above `maxMantissa` or `kMAX_REP`, divides by 10 and increments the
     *  exponent to preserve normalization.
     *
     *  @tparam T         Unsigned mantissa carrier type.
     *  @param negative   Sign flag; adjusted if the result collapses to zero.
     *  @param mantissa   Mantissa to round; updated in place.
     *  @param exponent   Exponent; updated in place.
     *  @param minMantissa  Lower bound of the active normalization range.
     *  @param maxMantissa  Upper bound of the active normalization range.
     *  @param location   Diagnostic string embedded in the `overflow_error` message.
     *  @throws std::overflow_error if the rounded exponent exceeds `kMAX_EXPONENT`.
     */
    template <UnsignedMantissa T>
    void
    doRoundUp(
        bool& negative,
        T& mantissa,
        int& exponent,
        internalrep const& minMantissa,
        internalrep const& maxMantissa,
        std::string location);

    /** Apply downward rounding to a normalized `(negative, mantissa, exponent)` triple.
     *
     *  Decrements the mantissa when `round()` indicates a downward adjustment.
     *  If the decrement drops the mantissa below `minMantissa`, multiplies by
     *  10 and decrements the exponent to restore normalization.
     *
     *  @tparam T         Unsigned mantissa carrier type.
     *  @param negative   Sign flag; adjusted if the result collapses to zero.
     *  @param mantissa   Mantissa to round; updated in place.
     *  @param exponent   Exponent; updated in place.
     *  @param minMantissa  Lower bound of the active normalization range.
     */
    template <UnsignedMantissa T>
    void
    doRoundDown(bool& negative, T& mantissa, int& exponent, internalrep const& minMantissa);

    /** Round a scaled integer `rep` value and apply its sign.
     *
     *  Used by `Number::operator rep()` after the mantissa has been fully
     *  scaled to an integer.  Applies the sign stored in `sbit_` after
     *  rounding.
     *
     *  @param drops    The integer value to round; updated in place.
     *  @param location Diagnostic string embedded in the `overflow_error` message.
     *  @throws std::overflow_error if rounding would push `drops` above `kMAX_REP`
     *      (theoretically impossible given correct normalization, but guarded
     *      defensively).
     */
    void
    doRound(rep& drops, std::string location) const;

private:
    /** Core push implementation; operates on a plain `unsigned`. */
    void
    doPush(unsigned d) noexcept;

    /** Restore normalization after a rounding step changes the mantissa.
     *
     *  If rounding caused `mantissa` to drop below `minMantissa`, multiplies
     *  by 10 and decrements the exponent.  If the exponent underflows
     *  `kMIN_EXPONENT` the triple is reset to canonical zero.
     */
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
    if (mantissa < minMantissa)
    {
        mantissa *= 10;
        --exponent;
    }
    if (exponent < kMIN_EXPONENT)
    {
        constexpr Number kZERO = Number{};

        negative = kZERO.negative_;
        mantissa = kZERO.mantissa_;
        exponent = kZERO.exponent_;
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
    std::string location)
{
    auto r = round();
    if (r == 1 || (r == 0 && (mantissa & 1) == 1))
    {
        ++mantissa;
        // Ensure mantissa after incrementing fits within both the
        // min/maxMantissa range and is a valid "rep".
        if (mantissa > maxMantissa || mantissa > kMAX_REP)
        {
            mantissa /= 10;
            ++exponent;
        }
    }
    bringIntoRange(negative, mantissa, exponent, minMantissa);
    if (exponent > kMAX_EXPONENT)
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

void
Number::Guard::doRound(rep& drops, std::string location) const
{
    auto r = round();
    if (r == 1 || (r == 0 && (drops & 1) == 1))
    {
        if (drops >= kMAX_REP)
        {
            static_assert(sizeof(internalrep) == sizeof(rep));
            // This should be impossible, because it's impossible to represent
            // "maxRep + 0.6" in Number, regardless of the scale. There aren't
            // enough digits available. You'd either get a mantissa of "maxRep"
            // or "(maxRep + 1) / 10", neither of which will round up when
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

/** Convert a signed `int64_t` mantissa to its unsigned absolute value.
 *
 *  Converting `INT64_MIN` via negation of `int64_t` is undefined behavior in
 *  C++; this function handles that edge case safely by routing through
 *  `int128_t` for the one value that overflows the positive `int64_t` range.
 *
 *  @param mantissa  Signed external mantissa, possibly negative.
 *  @return Unsigned magnitude; always fits in `uint64_t`.
 */
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

/** Return the value 1 normalized for the small mantissa range (10^15 scale).
 *
 *  Constructs 1.0 without normalization overhead so it can be a `constexpr`.
 *  The mantissa is `kSMALL_RANGE.min` (10^15) and the exponent is
 *  `−kSMALL_RANGE.log` (−15), giving exactly 1.0 × 10^0.
 */
constexpr Number
Number::oneSmall()
{
    return Number{false, Number::kSMALL_RANGE.min, -Number::kSMALL_RANGE.log, Number::Unchecked{}};
};

constexpr Number kONE_SML = Number::oneSmall();

/** Return the value 1 normalized for the large mantissa range (10^18 scale).
 *
 *  Constructs 1.0 without normalization overhead so it can be a `constexpr`.
 *  The mantissa is `kLARGE_RANGE.min` (10^18) and the exponent is
 *  `−kLARGE_RANGE.log` (−18), giving exactly 1.0 × 10^0.
 */
constexpr Number
Number::oneLarge()
{
    return Number{false, Number::kLARGE_RANGE.min, -Number::kLARGE_RANGE.log, Number::Unchecked{}};
};

constexpr Number kONE_LRG = Number::oneLarge();

/** Return the value 1 in the currently active mantissa range.
 *
 *  Dispatches to the pre-computed `kONE_SML` or `kONE_LRG` constant based on
 *  the thread-local `kRANGE` setting, avoiding a full normalization pass.
 */
Number
Number::one()
{
    if (&kRANGE.get() == &kSMALL_RANGE)
        return kONE_SML;
    XRPL_ASSERT(&kRANGE.get() == &kLARGE_RANGE, "Number::one() : valid range");
    return kONE_LRG;
}

// TODO: Rename the function parameters to get rid of the "_" suffix

/** Bring a `(negative, mantissa, exponent)` triple into canonical normalized form.
 *
 *  This is the shared implementation invoked by all three explicit
 *  specializations of `Number::normalize<T>`.  The algorithm:
 *   1. A zero mantissa is mapped to canonical zero and returns early.
 *   2. While `mantissa < minMantissa` and `exponent > kMIN_EXPONENT`, the
 *      mantissa is multiplied by 10 and the exponent decremented.
 *   3. While `mantissa > maxMantissa` or `mantissa > kMAX_REP`, the last
 *      digit is pushed into a `Guard` and the mantissa is divided by 10.
 *   4. An extra division handles the case where the intermediate value exceeds
 *      `INT64_MAX` but the final stored value must not (relevant for the large
 *      range, e.g. 9.9 × 10^18 > INT64_MAX).
 *   5. `Guard::doRoundUp` is called to apply correctly-rounded rounding to the
 *      result.
 *
 *  @tparam T          Mantissa type: `uint128_t`, `unsigned long long`, or
 *      `unsigned long`.  The 128-bit type is needed for `operator*=`, which
 *      computes a product that temporarily requires wider storage.
 *  @param negative    Sign flag; updated to canonical zero sign when result is 0.
 *  @param mantissa    The raw mantissa to normalize; updated in place.
 *  @param exponent    The raw exponent; updated in place.
 *  @param minMantissa Lower bound of the active normalization range.
 *  @param maxMantissa Upper bound of the active normalization range.
 *  @throws std::overflow_error if normalization would push `exponent` above
 *      `kMAX_EXPONENT`.
 *  @note The function is declared a `friend` of `Number` so it can access the
 *      `kMIN_EXPONENT`, `kMAX_EXPONENT`, and `kMAX_REP` constants as well as
 *      the `Guard` inner class.
 */
template <class T>
void
doNormalize(
    bool& negative,
    T& mantissa,
    int& exponent,
    MantissaRange::rep const& minMantissa,
    MantissaRange::rep const& maxMantissa)
{
    auto constexpr kMIN_EXPONENT = Number::kMIN_EXPONENT;
    auto constexpr kMAX_EXPONENT = Number::kMAX_EXPONENT;
    auto constexpr kMAX_REP = Number::kMAX_REP;

    using Guard = Number::Guard;

    constexpr Number kZERO = Number{};
    if (mantissa == 0)
    {
        mantissa = kZERO.mantissa_;
        exponent = kZERO.exponent_;
        negative = kZERO.negative_;
        return;
    }
    auto m = mantissa;
    while ((m < minMantissa) && (exponent > kMIN_EXPONENT))
    {
        m *= 10;
        --exponent;
    }
    Guard g;
    if (negative)
        g.setNegative();
    while (m > maxMantissa)
    {
        if (exponent >= kMAX_EXPONENT)
            throw std::overflow_error("Number::normalize 1");
        g.push(m % 10);
        m /= 10;
        ++exponent;
    }
    if ((exponent < kMIN_EXPONENT) || (m < minMantissa))
    {
        mantissa = kZERO.mantissa_;
        exponent = kZERO.exponent_;
        negative = kZERO.negative_;
        return;
    }

    // When using the largeRange, "m" needs fit within an int64, even if
    // the final mantissa_ is going to end up larger to fit within the
    // MantissaRange. Cut it down here so that the rounding will be done while
    // it's smaller.
    //
    // Example: 9,900,000,000,000,123,456 > 9,223,372,036,854,775,807,
    //      so "m" will be modified to 990,000,000,000,012,345. Then that value
    //      will be rounded to 990,000,000,000,012,345 or
    //      990,000,000,000,012,346, depending on the rounding mode. Finally,
    //      mantissa_ will be "m*10" so it fits within the range, and end up as
    //      9,900,000,000,000,123,450 or 9,900,000,000,000,123,460.
    // mantissa() will return mantissa_ / 10, and exponent() will return
    // exponent_ + 1.
    if (m > kMAX_REP)
    {
        if (exponent >= kMAX_EXPONENT)
            throw std::overflow_error("Number::normalize 1.5");
        g.push(m % 10);
        m /= 10;
        ++exponent;
    }
    // Before modification, m should be within the min/max range. After
    // modification, it must be less than maxRep. In other words, the original
    // value should have been no more than maxRep * 10.
    // (maxRep * 10 > maxMantissa)
    XRPL_ASSERT_PARTS(m <= kMAX_REP, "xrpl::doNormalize", "intermediate mantissa fits in int64");
    mantissa = m;

    g.doRoundUp(negative, mantissa, exponent, minMantissa, maxMantissa, "Number::normalize 2");
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
    internalrep const& maxMantissa)
{
    doNormalize(negative, mantissa, exponent, minMantissa, maxMantissa);
}

template <>
void
Number::normalize<unsigned long long>(
    bool& negative,
    unsigned long long& mantissa,
    int& exponent,
    internalrep const& minMantissa,
    internalrep const& maxMantissa)
{
    doNormalize(negative, mantissa, exponent, minMantissa, maxMantissa);
}

template <>
void
Number::normalize<unsigned long>(
    bool& negative,
    unsigned long& mantissa,
    int& exponent,
    internalrep const& minMantissa,
    internalrep const& maxMantissa)
{
    doNormalize(negative, mantissa, exponent, minMantissa, maxMantissa);
}

void
Number::normalize()
{
    auto const& range = kRANGE.get();
    normalize(negative_, mantissa_, exponent_, range.min, range.max);
}

/** Return a copy of this `Number` with its exponent shifted by `exponentDelta`.
 *
 *  The mantissa is unchanged, so the returned value is mathematically equal to
 *  `*this × 10^exponentDelta`.  Normalization is not re-run; the mantissa
 *  remains in its existing range as long as the new exponent is valid.  Used
 *  internally by `root` and `root2` to undo the pre-scaling step without the
 *  cost of a full normalization pass.
 *
 *  @param exponentDelta  Signed amount to add to the current exponent.
 *  @return  Adjusted `Number`, or zero if `exponent_ + exponentDelta` underflows
 *      `kMIN_EXPONENT`.
 *  @throws std::overflow_error if `exponent_ + exponentDelta >= kMAX_EXPONENT`.
 *  @pre `*this` must satisfy `isnormal()`.
 */
Number
Number::shiftExponent(int exponentDelta) const
{
    XRPL_ASSERT_PARTS(isnormal(), "xrpl::Number::shiftExponent", "normalized");
    auto const newExponent = exponent_ + exponentDelta;
    if (newExponent >= kMAX_EXPONENT)
        throw std::overflow_error("Number::shiftExponent");
    if (newExponent < kMIN_EXPONENT)
    {
        return Number{};
    }
    Number const result{negative_, mantissa_, newExponent, Unchecked{}};
    XRPL_ASSERT_PARTS(result.isnormal(), "xrpl::Number::shiftExponent", "result is normalized");
    return result;
}

/** Add `y` to `*this`, rounding the result to the active mantissa range.
 *
 *  Operands are aligned to a common exponent by dividing the smaller-exponent
 *  operand by 10 repeatedly, accumulating shed digits in a `Guard`.  Same-sign
 *  operands are added via `uint128_t` (avoiding overflow on large mantissas) and
 *  rounded up.  Opposite-sign operands are subtracted and guard digits are
 *  reclaimed via `pop()` before rounding down.
 *
 *  @param y  Value to add.
 *  @return   Reference to `*this` after the operation.
 *  @throws std::overflow_error if the result's exponent exceeds `kMAX_EXPONENT`.
 */
Number&
Number::operator+=(Number const& y)
{
    constexpr Number kZERO = Number{};
    if (y == kZERO)
        return *this;
    if (*this == kZERO)
    {
        *this = y;
        return *this;
    }
    if (*this == -y)
    {
        *this = kZERO;
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
            g.push(xm % 10);
            xm /= 10;
            ++xe;
        } while (xe < ye);
    }
    else if (xe > ye)
    {
        if (yn)
            g.setNegative();
        do
        {
            g.push(ym % 10);
            ym /= 10;
            ++ye;
        } while (xe > ye);
    }

    auto const& range = kRANGE.get();
    auto const& minMantissa = range.min;
    auto const& maxMantissa = range.max;

    if (xn == yn)
    {
        xm += ym;
        if (xm > maxMantissa || xm > kMAX_REP)
        {
            g.push(xm % 10);
            xm /= 10;
            ++xe;
        }
        g.doRoundUp(xn, xm, xe, minMantissa, maxMantissa, "Number::addition overflow");
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
        while (xm < minMantissa && xm * 10 <= kMAX_REP)
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
    normalize();
    return *this;
}

/** Divide a `uint128_t` by 10 in place and return the remainder.
 *
 *  Equivalent to `r = u % 10; u /= 10; return r;` but avoids the native
 *  128-bit modulo instruction, which is expensive on common architectures.
 *  The algorithm approximates `u / 10` using bit-shifts and a correction
 *  step derived from Hacker's Delight (2nd ed., §10) by Henry S. Warren Jr.
 *
 *  @param u  Value to divide; updated in place to `u / 10`.
 *  @return   The remainder `u % 10` (0–9) before the division.
 */
static inline unsigned
divu10(uint128_t& u)
{
    auto q = (u >> 1) + (u >> 2);
    q += q >> 4;
    q += q >> 8;
    q += q >> 16;
    q += q >> 32;
    q += q >> 64;
    q >>= 3;
    auto r = static_cast<unsigned>(u - ((q << 3) + (q << 1)));
    auto c = (r + 6) >> 4;
    u = q + c;
    r -= c * 10;
    return r;
}

/** Multiply `*this` by `y`, rounding the result to the active mantissa range.
 *
 *  The two mantissas are multiplied as `uint128_t` to avoid overflow, the
 *  exponents are summed, and the 128-bit product is trimmed into range using
 *  `divu10` — which is cheaper than the native 128-bit modulo instruction.
 *
 *  @param y  Multiplier.
 *  @return   Reference to `*this` after the operation.
 *  @throws std::overflow_error if the product's exponent exceeds `kMAX_EXPONENT`.
 */
Number&
Number::operator*=(Number const& y)
{
    constexpr Number kZERO = Number{};
    if (*this == kZERO)
        return *this;
    if (y == kZERO)
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

    auto const& range = kRANGE.get();
    auto const& minMantissa = range.min;
    auto const& maxMantissa = range.max;

    while (zm > maxMantissa || zm > kMAX_REP)
    {
        // The following is optimization for:
        // g.push(static_cast<unsigned>(zm % 10));
        // zm /= 10;
        g.push(divu10(zm));
        ++ze;
    }
    xm = static_cast<internalrep>(zm);
    xe = ze;
    g.doRoundUp(
        zn,
        xm,
        xe,
        minMantissa,
        maxMantissa,
        "Number::multiplication overflow : exponent is " + std::to_string(xe));
    negative_ = zn;
    mantissa_ = xm;
    exponent_ = xe;

    normalize();
    return *this;
}

/** Divide `*this` by `y`, rounding the result to the active mantissa range.
 *
 *  The numerator is pre-scaled by a power of 10 before integer division to
 *  preserve precision: 10^17 for the small range, 10^19 for the large range.
 *  For the large range an additional 1000× correction term is computed from
 *  the division remainder (because the combined scale factor would overflow
 *  `uint128_t`) and folded in before normalization.
 *
 *  @param y  Divisor.
 *  @return   Reference to `*this` after the operation.
 *  @throws std::overflow_error if `y` is zero.
 *  @note The pre-scale factor for the large range is 10^19 rather than the
 *      10^17 used for the small range; a comment in the source tracks an open
 *      question about whether a larger factor could be used safely.
 */
Number&
Number::operator/=(Number const& y)
{
    constexpr Number kZERO = Number{};
    if (y == kZERO)
        throw std::overflow_error("Number: divide by 0");
    if (*this == kZERO)
        return *this;
    // n* = numerator
    // d* = denominator
    // *p = negative (positive?)
    // *s = sign
    // *m = mantissa
    // *e = exponent

    bool const np = negative_;
    int const ns = (np ? -1 : 1);
    auto nm = mantissa_;
    auto ne = exponent_;

    bool const dp = y.negative_;
    int const ds = (dp ? -1 : 1);
    auto dm = y.mantissa_;
    auto de = y.exponent_;

    auto const& range = kRANGE.get();
    auto const& minMantissa = range.min;
    auto const& maxMantissa = range.max;

    // Shift by 10^17 gives greatest precision while not overflowing
    // uint128_t or the cast back to int64_t
    // TODO: Can/should this be made bigger for largeRange?
    // log(2^128,10) ~ 38.5
    // largeRange.log = 18, fits in 10^19
    // f can be up to 10^(38-19) = 10^19 safely
    static_assert(kSMALL_RANGE.log == 15);
    static_assert(kLARGE_RANGE.log == 18);
    bool const small = Number::getMantissaScale() == MantissaRange::MantissaScale::Small;
    uint128_t const f = small ? 100'000'000'000'000'000 : 10'000'000'000'000'000'000ULL;
    XRPL_ASSERT_PARTS(f >= minMantissa * 10, "Number::operator/=", "factor expected size");

    // unsigned denominator
    auto const dmu = static_cast<uint128_t>(dm);
    // correctionFactor can be anything between 10 and f, depending on how much
    // extra precision we want to only use for rounding with the
    // largeRange. Three digits seems like plenty, and is more than
    // the smallRange uses.
    uint128_t const correctionFactor = 1'000;

    auto const numerator = uint128_t(nm) * f;

    auto zm = numerator / dmu;
    auto ze = ne - de - (small ? 17 : 19);
    bool zn = (ns * ds) < 0;
    if (!small)
    {
        // Virtually multiply numerator by correctionFactor. Since that would
        // overflow in the existing uint128_t, we'll do that part separately.
        // The math for this would work for small mantissas, but we need to
        // preserve existing behavior.
        //
        // Consider:
        // ((numerator * correctionFactor) / dmu) / correctionFactor
        // = ((numerator / dmu) * correctionFactor) / correctionFactor)
        //
        // But that assumes infinite precision. With integer math, this is
        // equivalent to
        //
        // = ((numerator / dmu * correctionFactor)
        //   + ((numerator % dmu) * correctionFactor) / dmu) / correctionFactor
        //
        // We have already set `mantissa_ = numerator / dmu`. Now we
        // compute `remainder = numerator % dmu`, and if it is
        // nonzero, we do the rest of the arithmetic. If it's zero, we can skip
        // it.
        auto const remainder = (numerator % dmu);
        if (remainder != 0)
        {
            zm *= correctionFactor;
            auto const correction = remainder * correctionFactor / dmu;
            zm += correction;
            // divide by 1000 by moving the exponent, so we don't lose the
            // integer value we just computed
            ze -= 3;
        }
    }
    normalize(zn, zm, ze, minMantissa, maxMantissa);
    negative_ = zn;
    mantissa_ = static_cast<internalrep>(zm);
    exponent_ = ze;
    XRPL_ASSERT_PARTS(isnormal(), "xrpl::Number::operator/=", "result is normalized");

    return *this;
}

/** Convert this `Number` to a signed 64-bit integer, rounding to the active mode.
 *
 *  Scales the mantissa to an integer by repeatedly dividing (when the exponent
 *  is negative, pushing each dropped digit into a `Guard`) or multiplying
 *  (when the exponent is positive, checking for overflow), then calls
 *  `Guard::doRound` to apply the thread-local rounding mode and sign.  This
 *  is the primary way XRP drop values are materialized from `Number` arithmetic.
 *
 *  @return  The rounded integer value of this `Number`.
 *  @throws std::overflow_error if scaling overflows `int64_t`.
 */
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
        for (; offset < 0; ++offset)
        {
            g.push(drops % 10);
            drops /= 10;
        }
        for (; offset > 0; --offset)
        {
            if (drops > kMAX_REP / 10)
                throw std::overflow_error("Number::operator rep() overflow");
            drops *= 10;
        }
        g.doRound(drops, "Number::operator rep() rounding overflow");
    }
    return drops;
}

/** Remove the fractional part of this `Number` without rounding.
 *
 *  Repeatedly divides the internal mantissa by 10 and increments the exponent
 *  until `exponent_ >= 0`, then renormalizes.  Unlike `operator rep()`, this
 *  does not apply any rounding mode — the result is always truncated toward zero.
 *
 *  @return  A `Number` whose value is the integer part of `*this`.
 *  @note The `noexcept` guarantee holds because the resulting exponent is never
 *      positive (the loop terminates at 0) and normalization cannot throw when
 *      the exponent is non-positive.
 */
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
    ret.normalize();
    return ret;
}

/** Format a `Number` as a human-readable decimal string.
 *
 *  For values whose exponent falls outside a window centered on the active
 *  `mantissaLog()`, scientific notation is used (`Me+E`).  Otherwise, the
 *  function constructs a zero-padded string of the integer mantissa, locates
 *  the decimal point via pointer arithmetic, and strips leading and trailing
 *  zeros to produce a compact decimal representation.
 *
 *  @param amount  The value to format.
 *  @return  Human-readable decimal string, e.g. `"1.5"`, `"-0.001"`,
 *      `"1e+20"`, or `"0"` for the zero value.
 */
std::string
to_string(Number const& amount)
{
    // keep full internal accuracy, but make more human friendly if possible
    constexpr Number kZERO = Number{};
    if (amount == kZERO)
        return "0";

    auto exponent = amount.exponent_;
    auto mantissa = amount.mantissa_;
    bool const negative = amount.negative_;

    // Use scientific notation for exponents that are too small or too large
    auto const rangeLog = Number::mantissaLog();
    if (((exponent != 0) && ((exponent < -(rangeLog + 10)) || (exponent > -(rangeLog - 10)))))
    {
        while (mantissa != 0 && mantissa % 10 == 0 && exponent < Number::kMAX_EXPONENT)
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

/** Raise `f` to the non-negative integer power `n`.
 *
 *  Uses binary (fast) exponentiation, requiring O(log₂ n) multiplications.
 *  Returns `Number::one()` for `n == 0` and `f` itself for `n == 1`.
 *
 *  @param f  Base value.
 *  @param n  Non-negative integer exponent.
 *  @return   f^n, rounded to the active mantissa range.
 */
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

/** Compute the `d`-th root of `f` (i.e. f^(1/d)) via Newton–Raphson iteration.
 *
 *  Finds the non-negative root of g(x) = x^d − f.  To ensure rapid convergence
 *  regardless of scale, `f` is first shifted so that its exponent is a multiple
 *  of `d` and the value lies in (0, 1).  A quadratic least-squares curve fit
 *  provides the initial guess; the iteration `r ← ((d−1)r + f/r^(d−1)) / d`
 *  continues until the result is stable or oscillates between two values (a
 *  cycle-of-2 detector prevents infinite loops near the rounding boundary).
 *  The exponent shift is reversed with `shiftExponent` before returning.
 *
 *  Corner cases follow IEEE 754 Annex F / C standard Annex F conventions:
 *   - `d == 0`, `f == ±one`: returns `one`.
 *   - `d == 0`, `abs(f) < one`: returns zero.
 *   - `d == 0`, `abs(f) > one`: throws (infinity).
 *   - Even `d` with negative `f`: throws (NaN).
 *   - `f == zero`: returns zero.
 *
 *  @param f  Radicand.  May be negative only when `d` is odd.
 *  @param d  Root degree.
 *  @return   f^(1/d), rounded to the active mantissa range.
 *  @throws std::overflow_error for semantically infinite or NaN results.
 */
Number
root(Number f, unsigned d)
{
    constexpr Number kZERO = Number{};
    auto const one = Number::one();

    if (f == one || d == 1)
        return f;
    if (d == 0)
    {
        if (f == -one)
            return one;
        if (abs(f) < one)
            return kZERO;
        throw std::overflow_error("Number::root infinity");
    }
    if (f < kZERO && d % 2 == 0)
        throw std::overflow_error("Number::root nan");
    if (f == kZERO)
        return f;

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
    if (f < kZERO)
    {
        neg = true;
        f = -f;
    }

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

    Number rm1{};
    Number rm2{};
    do
    {
        rm2 = rm1;
        rm1 = r;
        r = (Number(d - 1) * r + f / power(r, d - 1)) / Number(d);
    } while (r != rm1 && r != rm2);

    auto const result = r.shiftExponent(e / di);
    XRPL_ASSERT_PARTS(result.isnormal(), "xrpl::root(Number, unsigned)", "result is normalized");
    return result;
}

/** Compute the square root of `f` using a specialised Newton–Raphson loop.
 *
 *  Functionally equivalent to `root(f, 2)` but uses hardcoded coefficients
 *  (D=105, a0=18, a1=144, a2=−60) for the initial quadratic guess and the
 *  simplified iteration `r ← (r + f/r) / 2`, making it faster than the
 *  general `root` for the common `d == 2` case.
 *
 *  @param f  Non-negative radicand.
 *  @return   √f, rounded to the active mantissa range.
 *  @throws std::overflow_error if `f` is negative (NaN semantics).
 */
Number
root2(Number f)
{
    constexpr Number kZERO = Number{};
    auto const one = Number::one();

    if (f == one)
        return f;
    if (f < kZERO)
        throw std::overflow_error("Number::root nan");
    if (f == kZERO)
        return f;

    auto e = f.exponent_ + Number::mantissaLog() + 1;
    if (e % 2 != 0)
        ++e;
    f = f.shiftExponent(-e);  // f /= 10^e;
    XRPL_ASSERT_PARTS(f.isnormal(), "xrpl::root2(Number)", "f is normalized");

    auto const D = 105;  // NOLINT(readability-identifier-naming)
    auto const a0 = 18;
    auto const a1 = 144;
    auto const a2 = -60;
    Number r = ((Number{a2} * f + Number{a1}) * f + Number{a0}) / Number{D};

    Number rm1{};
    Number rm2{};
    do
    {
        rm2 = rm1;
        rm1 = r;
        r = (r + f / r) / Number(2);
    } while (r != rm1 && r != rm2);

    auto const result = r.shiftExponent(e / 2);
    XRPL_ASSERT_PARTS(result.isnormal(), "xrpl::root2(Number)", "result is normalized");

    return result;
}

/** Raise `f` to the rational power `n/d`.
 *
 *  Reduces the fraction `n/d` by their GCD, then computes `root(power(f, n), d)`.
 *  Corner cases follow IEEE 754 Annex F / C standard Annex F conventions:
 *   - `f == one`: returns `one`.
 *   - `n == 0` (after GCD reduction): returns `one`.
 *   - `d == 0` after GCD reduction: infinite or NaN depending on `abs(f)`.
 *   - Odd numerator, even denominator, negative `f`: throws (NaN).
 *   - Both `n` and `d` zero (`gcd == 0`): throws (NaN).
 *
 *  @param f  Base value.
 *  @param n  Numerator of the exponent.
 *  @param d  Denominator of the exponent.
 *  @return   f^(n/d), rounded to the active mantissa range.
 *  @throws std::overflow_error for semantically infinite or NaN results.
 */
Number
power(Number const& f, unsigned n, unsigned d)
{
    constexpr Number kZERO = Number{};
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
            return kZERO;
        // abs(f) > one
        throw std::overflow_error("Number::power infinity");
    }
    if (n == 0)
        return one;
    n /= g;
    d /= g;
    if ((n % 2) == 1 && (d % 2) == 0 && f < kZERO)
        throw std::overflow_error("Number::power nan");
    return root(power(f, n), d);
}

}  // namespace xrpl
