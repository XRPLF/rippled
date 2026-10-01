#pragma once

#include <xrpl/basics/Number.h>

#include <benchmarks/libxrpl/number/NumberLike.h>

#include <mpdecimal.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace xrpl::number_bench {

/**
 * mpdecimal (libmpdec) at a fixed precision, behind Number's interface.
 *
 * This is the correctly-rounded reference for the divergence harness: with
 * `allcr` set, every operation, including `power`, is correctly rounded in
 * the active rounding mode.
 *
 * `Digits` is the coefficient precision: 19 to mirror today's Number, 34 for
 * IEEE decimal128, 38 for a full-uint128 Number. The exponent range follows
 * Number's convention: value = coefficient * 10^e with e in
 * [Number::kMinExponent, Number::kMaxExponent] for a `Digits`-digit
 * coefficient. mpdecimal uses adjusted exponents (the exponent of the leading
 * digit), hence the `Digits - 1` offset.
 *
 * Coefficients live in inline storage sized for `Digits`, so copying never
 * allocates. Library temporaries may still allocate; that is part of
 * mpdecimal's cost. Precisions above 38 digits exist for the divergence
 * harness, which needs a near-exact result to round from; `decompose` is only
 * available up to 38 digits.
 *
 * Unlike Number, results that overflow become infinity and underflow becomes
 * subnormal or zero. Each operation ORs its mpdecimal status into a
 * thread-local accumulator; read and clear it with `takeStatus()`.
 */
template <unsigned Digits>
class MpDecimal final
{
    static_assert(Digits >= 16 && Digits <= 114);

    // Enough 19-digit limbs for a Digits-digit coefficient, and never fewer
    // than MPD_MINALLOC_MIN.
    static constexpr mpd_ssize_t kLimbs = (Digits / MPD_RDIGITS) + 2;

    mpd_uint_t data_[kLimbs]{};
    // Zero: one limb holding 0.
    mpd_t dec_{MPD_STATIC | MPD_STATIC_DATA, 0, 1, 1, kLimbs, data_};

    struct Context
    {
        mpd_context_t ctx{};
        uint32_t status = 0;

        Context()
        {
            mpd_defaultcontext(&ctx);
            ctx.prec = Digits;
            ctx.emax = Number::kMaxExponent + static_cast<int>(Digits) - 1;
            ctx.emin = Number::kMinExponent + static_cast<int>(Digits) - 1;
            ctx.round = MPD_ROUND_HALF_EVEN;
            ctx.traps = 0;
            ctx.clamp = 0;
            ctx.allcr = 1;
        }
    };

    static Context&
    context() noexcept
    {
        static thread_local Context c;
        return c;
    }

    template <class F>
    static MpDecimal
    compute(F f)
    {
        MpDecimal result;
        auto& c = context();
        f(&result.dec_, &c.ctx, &c.status);
        return result;
    }

public:
    using rep = std::int64_t;

    static constexpr unsigned kDigits = Digits;

    MpDecimal() noexcept = default;

    ~MpDecimal()
    {
        // Frees the coefficient only if an operation outgrew inline storage.
        mpd_del(&dec_);
    }

    MpDecimal(MpDecimal const& other) noexcept
    {
        mpd_qcopy(&dec_, &other.dec_, &context().status);
    }

    MpDecimal&
    operator=(MpDecimal const& other) noexcept
    {
        if (this != &other)
            mpd_qcopy(&dec_, &other.dec_, &context().status);
        return *this;
    }

    // Implicit, like Number(rep).
    MpDecimal(rep mantissa) : MpDecimal(mantissa, 0)  // NOLINT
    {
    }

    explicit MpDecimal(rep mantissa, int exponent)
    {
        auto& c = context();
        mpd_qset_i64_exact(&dec_, mantissa, &c.status);
        dec_.exp += exponent;
        mpd_qfinalize(&dec_, &c.ctx, &c.status);
    }

    MpDecimal
    operator-() const
    {
        return compute(
            [this](mpd_t* r, mpd_context_t* ctx, uint32_t* s) { mpd_qminus(r, &dec_, ctx, s); });
    }

    friend MpDecimal
    operator+(MpDecimal const& x, MpDecimal const& y)
    {
        return compute([&](mpd_t* r, mpd_context_t* ctx, uint32_t* s) {
            mpd_qadd(r, &x.dec_, &y.dec_, ctx, s);
        });
    }

    friend MpDecimal
    operator-(MpDecimal const& x, MpDecimal const& y)
    {
        return compute([&](mpd_t* r, mpd_context_t* ctx, uint32_t* s) {
            mpd_qsub(r, &x.dec_, &y.dec_, ctx, s);
        });
    }

    friend MpDecimal
    operator*(MpDecimal const& x, MpDecimal const& y)
    {
        return compute([&](mpd_t* r, mpd_context_t* ctx, uint32_t* s) {
            mpd_qmul(r, &x.dec_, &y.dec_, ctx, s);
        });
    }

    friend MpDecimal
    operator/(MpDecimal const& x, MpDecimal const& y)
    {
        return compute([&](mpd_t* r, mpd_context_t* ctx, uint32_t* s) {
            mpd_qdiv(r, &x.dec_, &y.dec_, ctx, s);
        });
    }

    MpDecimal&
    operator+=(MpDecimal const& x)
    {
        return *this = *this + x;
    }

    MpDecimal&
    operator-=(MpDecimal const& x)
    {
        return *this = *this - x;
    }

    MpDecimal&
    operator*=(MpDecimal const& x)
    {
        return *this = *this * x;
    }

    MpDecimal&
    operator/=(MpDecimal const& x)
    {
        return *this = *this / x;
    }

    friend bool
    operator==(MpDecimal const& x, MpDecimal const& y) noexcept
    {
        return mpd_qcmp(&x.dec_, &y.dec_, &context().status) == 0;
    }

    friend bool
    operator<(MpDecimal const& x, MpDecimal const& y) noexcept
    {
        return mpd_qcmp(&x.dec_, &y.dec_, &context().status) == -1;
    }

    friend bool
    operator>(MpDecimal const& x, MpDecimal const& y) noexcept
    {
        return y < x;
    }

    friend bool
    operator<=(MpDecimal const& x, MpDecimal const& y) noexcept
    {
        return x < y || x == y;
    }

    friend bool
    operator>=(MpDecimal const& x, MpDecimal const& y) noexcept
    {
        return y <= x;
    }

    // Round to an integer using the current rounding mode, like Number.
    explicit
    operator rep() const
    {
        auto const rounded = compute([this](mpd_t* r, mpd_context_t* ctx, uint32_t* s) {
            mpd_qround_to_int(r, &dec_, ctx, s);
        });
        return mpd_qget_i64(&rounded.dec_, &context().status);
    }

    static Number::RoundingMode
    getround() noexcept
    {
        using enum Number::RoundingMode;
        switch (context().ctx.round)
        {
            case MPD_ROUND_DOWN:
                return TowardsZero;
            case MPD_ROUND_FLOOR:
                return Downward;
            case MPD_ROUND_CEILING:
                return Upward;
            default:
                return ToNearest;
        }
    }

    static Number::RoundingMode
    setround(Number::RoundingMode mode) noexcept
    {
        using enum Number::RoundingMode;
        auto const old = getround();
        auto& round = context().ctx.round;
        switch (mode)
        {
            case ToNearest:
                round = MPD_ROUND_HALF_EVEN;
                break;
            case TowardsZero:
                round = MPD_ROUND_DOWN;
                break;
            case Downward:
                round = MPD_ROUND_FLOOR;
                break;
            case Upward:
                round = MPD_ROUND_CEILING;
                break;
        }
        return old;
    }

    /**
     * Returns the mpdecimal status flags raised since the last call, and clears them.
     */
    static uint32_t
    takeStatus() noexcept
    {
        return std::exchange(context().status, 0);
    }

    friend std::string
    to_string(MpDecimal const& x)
    {
        char* raw = nullptr;
        auto const size = mpd_to_sci_size(&raw, &x.dec_, 0);
        std::unique_ptr<char, void (*)(void*)> const owned{raw, mpd_free};
        return size < 0 ? std::string{} : std::string{raw, static_cast<std::size_t>(size)};
    }

    friend MpDecimal
    abs(MpDecimal const& x)
    {
        return compute(
            [&](mpd_t* r, mpd_context_t* ctx, uint32_t* s) { mpd_qabs(r, &x.dec_, ctx, s); });
    }

    friend MpDecimal
    power(MpDecimal const& x, unsigned n)
    {
        MpDecimal const exponent{static_cast<rep>(n)};
        return compute([&](mpd_t* r, mpd_context_t* ctx, uint32_t* s) {
            mpd_qpow(r, &x.dec_, &exponent.dec_, ctx, s);
        });
    }

    friend MpDecimal
    root2(MpDecimal const& x)
    {
        return compute(
            [&](mpd_t* r, mpd_context_t* ctx, uint32_t* s) { mpd_qsqrt(r, &x.dec_, ctx, s); });
    }

    /**
     * The underlying libmpdec value, for the divergence harness.
     */
    [[nodiscard]] mpd_t const*
    get() const noexcept
    {
        return &dec_;
    }

    [[nodiscard]] mpd_t*
    get() noexcept
    {
        return &dec_;
    }

    /**
     * Rounds `x` to this type's precision in the current rounding mode.
     */
    template <unsigned OtherDigits>
    static MpDecimal
    roundFrom(MpDecimal<OtherDigits> const& x)
    {
        MpDecimal result;
        auto& c = context();
        mpd_qcopy(&result.dec_, x.get(), &c.status);
        mpd_qfinalize(&result.dec_, &c.ctx, &c.status);
        return result;
    }

    /**
     * Exact conversion; rounds only if `d` has more than Digits digits.
     */
    static MpDecimal
    fromDecomposed(Decomposed const& d)
    {
        auto& c = context();
        mpd_context_t exact;
        mpd_maxcontext(&exact);
        exact.traps = 0;

        // coefficient = hi * 2^64 + lo, assembled at unlimited precision.
        MpDecimal result;
        MpDecimal lo;
        MpDecimal shift;
        mpd_qset_u64_exact(
            &result.dec_, static_cast<std::uint64_t>(d.coefficient >> 64), &c.status);
        mpd_qset_u64_exact(&lo.dec_, static_cast<std::uint64_t>(d.coefficient), &c.status);
        mpd_qset_u64_exact(&shift.dec_, std::uint64_t{1} << 32, &c.status);
        mpd_qmul(&shift.dec_, &shift.dec_, &shift.dec_, &exact, &c.status);
        mpd_qmul(&result.dec_, &result.dec_, &shift.dec_, &exact, &c.status);
        mpd_qadd(&result.dec_, &result.dec_, &lo.dec_, &exact, &c.status);
        if (d.negative)
            mpd_set_negative(&result.dec_);
        result.dec_.exp += d.exponent;
        mpd_qfinalize(&result.dec_, &c.ctx, &c.status);
        return result;
    }

    /**
     * Precondition: x is finite (check takeStatus() for overflow/invalid first).
     */
    friend Decomposed
    decompose(MpDecimal const& x) noexcept
        requires(Digits <= 38)
    {
        unsigned __int128 coefficient = 0;
        for (auto i = x.dec_.len; i-- > 0;)
            coefficient = (coefficient * MPD_RADIX) + x.dec_.data[i];
        return {
            .negative = (x.dec_.flags & MPD_NEG) != 0,
            .coefficient = coefficient,
            .exponent = static_cast<int>(x.dec_.exp)};
    }
};

using MpDecimal19 = MpDecimal<19>;
using MpDecimal34 = MpDecimal<34>;
using MpDecimal38 = MpDecimal<38>;

static_assert(DecomposableNumber<MpDecimal19>);
static_assert(DecomposableNumber<MpDecimal34>);
static_assert(DecomposableNumber<MpDecimal38>);

}  // namespace xrpl::number_bench
