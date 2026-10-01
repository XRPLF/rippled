#pragma once

#include <xrpl/basics/Number.h>

#include <benchmarks/libxrpl/number/NumberLike.h>

#include <array>
#include <cstdint>
#include <string>
#include <utility>

// The library must be built with the same three settings (see the
// bench_intel_dfp block in src/benchmarks/libxrpl/CMakeLists.txt): arguments
// by value, rounding mode passed to each call, status flags passed to each
// call. That is the configuration closest to Number, whose rounding mode is a
// per-thread setting rather than process-global state.
#define DECIMAL_CALL_BY_REFERENCE 0       // NOLINT(cppcoreguidelines-macro-usage)
#define DECIMAL_GLOBAL_ROUNDING 0         // NOLINT(cppcoreguidelines-macro-usage)
#define DECIMAL_GLOBAL_EXCEPTION_FLAGS 0  // NOLINT(cppcoreguidelines-macro-usage)
#include <bid_conf.h>
#include <bid_functions.h>

namespace xrpl::number_bench {

namespace detail {

/**
 * One function table per interchange width, so IntelBid is written once.
 */
struct Bid64Ops
{
    using Value = BID_UINT64;
    static constexpr unsigned kDigits = 16;
    static constexpr int kBias = 398;

    // clang-format off
    static Value add(Value x, Value y, _IDEC_round r, _IDEC_flags* f) { return bid64_add(x, y, r, f); }
    static Value sub(Value x, Value y, _IDEC_round r, _IDEC_flags* f) { return bid64_sub(x, y, r, f); }
    static Value mul(Value x, Value y, _IDEC_round r, _IDEC_flags* f) { return bid64_mul(x, y, r, f); }
    static Value div(Value x, Value y, _IDEC_round r, _IDEC_flags* f) { return bid64_div(x, y, r, f); }
    static Value sqrt(Value x, _IDEC_round r, _IDEC_flags* f) { return bid64_sqrt(x, r, f); }
    static Value pow(Value x, Value y, _IDEC_round r, _IDEC_flags* f) { return bid64_pow(x, y, r, f); }
    static Value fromInt64(std::int64_t x, _IDEC_round r, _IDEC_flags* f) { return bid64_from_int64(x, r, f); }
    static Value scalbn(Value x, int n, _IDEC_round r, _IDEC_flags* f) { return bid64_scalbn(x, n, r, f); }
    static Value negate(Value x) { return bid64_negate(x); }
    static Value abs(Value x) { return bid64_abs(x); }
    static bool less(Value x, Value y, _IDEC_flags* f) { return bid64_quiet_less(x, y, f) != 0; }
    static bool equal(Value x, Value y, _IDEC_flags* f) { return bid64_quiet_equal(x, y, f) != 0; }
    static std::int64_t toInt64Nearest(Value x, _IDEC_flags* f) { return bid64_to_int64_rnint(x, f); }
    static std::int64_t toInt64TowardZero(Value x, _IDEC_flags* f) { return bid64_to_int64_int(x, f); }
    static std::int64_t toInt64Floor(Value x, _IDEC_flags* f) { return bid64_to_int64_floor(x, f); }
    static std::int64_t toInt64Ceil(Value x, _IDEC_flags* f) { return bid64_to_int64_ceil(x, f); }
    static void toString(char* s, Value x, _IDEC_flags* f) { bid64_to_string(s, x, f); }
    // clang-format on

    /**
     * Decodes the BID64 layout. Precondition: finite and canonical.
     */
    static Decomposed
    decompose(Value x) noexcept
    {
        bool const negative = (x >> 63) != 0;
        bool const largeForm = ((x >> 61) & 3) == 3;
        auto const exponent = static_cast<int>(largeForm ? (x >> 51) & 0x3ff : (x >> 53) & 0x3ff);
        auto const coefficient = largeForm
            ? (x & 0x0007'ffff'ffff'ffffULL) | 0x0020'0000'0000'0000ULL
            : x & 0x001f'ffff'ffff'ffffULL;
        return {.negative = negative, .coefficient = coefficient, .exponent = exponent - kBias};
    }
};

struct Bid128Ops
{
    using Value = BID_UINT128;
    static constexpr unsigned kDigits = 34;
    static constexpr int kBias = 6176;

    // clang-format off
    static Value add(Value x, Value y, _IDEC_round r, _IDEC_flags* f) { return bid128_add(x, y, r, f); }
    static Value sub(Value x, Value y, _IDEC_round r, _IDEC_flags* f) { return bid128_sub(x, y, r, f); }
    static Value mul(Value x, Value y, _IDEC_round r, _IDEC_flags* f) { return bid128_mul(x, y, r, f); }
    static Value div(Value x, Value y, _IDEC_round r, _IDEC_flags* f) { return bid128_div(x, y, r, f); }
    static Value sqrt(Value x, _IDEC_round r, _IDEC_flags* f) { return bid128_sqrt(x, r, f); }
    static Value pow(Value x, Value y, _IDEC_round r, _IDEC_flags* f) { return bid128_pow(x, y, r, f); }
    static Value fromInt64(std::int64_t x, _IDEC_round, _IDEC_flags*) { return bid128_from_int64(x); }
    static Value scalbn(Value x, int n, _IDEC_round r, _IDEC_flags* f) { return bid128_scalbn(x, n, r, f); }
    static Value negate(Value x) { return bid128_negate(x); }
    static Value abs(Value x) { return bid128_abs(x); }
    static bool less(Value x, Value y, _IDEC_flags* f) { return bid128_quiet_less(x, y, f) != 0; }
    static bool equal(Value x, Value y, _IDEC_flags* f) { return bid128_quiet_equal(x, y, f) != 0; }
    static std::int64_t toInt64Nearest(Value x, _IDEC_flags* f) { return bid128_to_int64_rnint(x, f); }
    static std::int64_t toInt64TowardZero(Value x, _IDEC_flags* f) { return bid128_to_int64_int(x, f); }
    static std::int64_t toInt64Floor(Value x, _IDEC_flags* f) { return bid128_to_int64_floor(x, f); }
    static std::int64_t toInt64Ceil(Value x, _IDEC_flags* f) { return bid128_to_int64_ceil(x, f); }
    static void toString(char* s, Value x, _IDEC_flags* f) { bid128_to_string(s, x, f); }
    // clang-format on

    /**
     * Decodes the BID128 layout. Precondition: finite and canonical.
     */
    static Decomposed
    decompose(Value x) noexcept
    {
        auto const hi = x.w[BID_HIGH_128W];
        auto const lo = x.w[BID_LOW_128W];
        bool const negative = (hi >> 63) != 0;
        auto const exponent = static_cast<int>((hi >> 49) & 0x3fff);
        auto const coefficient =
            (static_cast<unsigned __int128>(hi & 0x0001'ffff'ffff'ffffULL) << 64) | lo;
        return {.negative = negative, .coefficient = coefficient, .exponent = exponent - kBias};
    }
};

}  // namespace detail

/**
 * Intel's Decimal Floating-Point Math Library (BID encoding) behind Number's
 * interface.
 *
 * The rounding mode is thread-local, like Number's, and passed to each call.
 * Status flags from every call accumulate in a thread-local; read and clear
 * them with `takeStatus()`.
 *
 * Like the other IEEE types, overflow produces infinity and division by zero
 * produces inf/NaN instead of throwing; decimal64 rounds 19-digit int64
 * mantissas to 16 digits on construction.
 */
template <class Ops>
class IntelBid final
{
    using Value = typename Ops::Value;

    Value value_{};

    struct State
    {
        _IDEC_round round = BID_ROUNDING_TO_NEAREST;
        _IDEC_flags flags = 0;
    };

    static State&
    state() noexcept
    {
        static thread_local State s;
        return s;
    }

    static IntelBid
    wrap(Value v) noexcept
    {
        IntelBid result;
        result.value_ = v;
        return result;
    }

public:
    using rep = std::int64_t;

    IntelBid() : value_{Ops::fromInt64(0, BID_ROUNDING_TO_NEAREST, &state().flags)}
    {
    }

    // Implicit, like Number(rep).
    IntelBid(rep mantissa) : IntelBid(mantissa, 0)  // NOLINT
    {
    }

    explicit IntelBid(rep mantissa, int exponent)
    {
        auto& s = state();
        value_ = Ops::fromInt64(mantissa, s.round, &s.flags);
        if (exponent != 0)
            value_ = Ops::scalbn(value_, exponent, s.round, &s.flags);
    }

    IntelBid
    operator-() const noexcept
    {
        return wrap(Ops::negate(value_));
    }

    friend IntelBid
    operator+(IntelBid const& x, IntelBid const& y) noexcept
    {
        auto& s = state();
        return wrap(Ops::add(x.value_, y.value_, s.round, &s.flags));
    }

    friend IntelBid
    operator-(IntelBid const& x, IntelBid const& y) noexcept
    {
        auto& s = state();
        return wrap(Ops::sub(x.value_, y.value_, s.round, &s.flags));
    }

    friend IntelBid
    operator*(IntelBid const& x, IntelBid const& y) noexcept
    {
        auto& s = state();
        return wrap(Ops::mul(x.value_, y.value_, s.round, &s.flags));
    }

    friend IntelBid
    operator/(IntelBid const& x, IntelBid const& y) noexcept
    {
        auto& s = state();
        return wrap(Ops::div(x.value_, y.value_, s.round, &s.flags));
    }

    IntelBid&
    operator+=(IntelBid const& x) noexcept
    {
        return *this = *this + x;
    }

    IntelBid&
    operator-=(IntelBid const& x) noexcept
    {
        return *this = *this - x;
    }

    IntelBid&
    operator*=(IntelBid const& x) noexcept
    {
        return *this = *this * x;
    }

    IntelBid&
    operator/=(IntelBid const& x) noexcept
    {
        return *this = *this / x;
    }

    friend bool
    operator==(IntelBid const& x, IntelBid const& y) noexcept
    {
        return Ops::equal(x.value_, y.value_, &state().flags);
    }

    friend bool
    operator<(IntelBid const& x, IntelBid const& y) noexcept
    {
        return Ops::less(x.value_, y.value_, &state().flags);
    }

    friend bool
    operator>(IntelBid const& x, IntelBid const& y) noexcept
    {
        return y < x;
    }

    friend bool
    operator<=(IntelBid const& x, IntelBid const& y) noexcept
    {
        return !(y < x);
    }

    friend bool
    operator>=(IntelBid const& x, IntelBid const& y) noexcept
    {
        return !(x < y);
    }

    // Round to an integer using the current rounding mode, like Number.
    explicit
    operator rep() const noexcept
    {
        auto& s = state();
        switch (s.round)
        {
            case BID_ROUNDING_TO_ZERO:
                return Ops::toInt64TowardZero(value_, &s.flags);
            case BID_ROUNDING_DOWN:
                return Ops::toInt64Floor(value_, &s.flags);
            case BID_ROUNDING_UP:
                return Ops::toInt64Ceil(value_, &s.flags);
            default:
                return Ops::toInt64Nearest(value_, &s.flags);
        }
    }

    static Number::RoundingMode
    getround() noexcept
    {
        using enum Number::RoundingMode;
        switch (state().round)
        {
            case BID_ROUNDING_TO_ZERO:
                return TowardsZero;
            case BID_ROUNDING_DOWN:
                return Downward;
            case BID_ROUNDING_UP:
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
        auto& round = state().round;
        switch (mode)
        {
            case ToNearest:
                round = BID_ROUNDING_TO_NEAREST;
                break;
            case TowardsZero:
                round = BID_ROUNDING_TO_ZERO;
                break;
            case Downward:
                round = BID_ROUNDING_DOWN;
                break;
            case Upward:
                round = BID_ROUNDING_UP;
                break;
        }
        return old;
    }

    /**
     * Returns the status flags raised since the last call, and clears them.
     */
    static _IDEC_flags
    takeStatus() noexcept
    {
        return std::exchange(state().flags, 0);
    }

    friend std::string
    to_string(IntelBid const& x)
    {
        std::array<char, 64> buffer{};
        Ops::toString(buffer.data(), x.value_, &state().flags);
        return buffer.data();
    }

    friend IntelBid
    abs(IntelBid const& x) noexcept
    {
        return wrap(Ops::abs(x.value_));
    }

    friend IntelBid
    power(IntelBid const& x, unsigned n)
    {
        auto& s = state();
        IntelBid const exponent{static_cast<rep>(n)};
        return wrap(Ops::pow(x.value_, exponent.value_, s.round, &s.flags));
    }

    friend IntelBid
    root2(IntelBid const& x) noexcept
    {
        auto& s = state();
        return wrap(Ops::sqrt(x.value_, s.round, &s.flags));
    }

    /**
     * Precondition: x is finite.
     */
    friend Decomposed
    decompose(IntelBid const& x) noexcept
    {
        return Ops::decompose(x.value_);
    }
};

using IntelBid64 = IntelBid<detail::Bid64Ops>;
using IntelBid128 = IntelBid<detail::Bid128Ops>;

static_assert(DecomposableNumber<IntelBid64>);
static_assert(DecomposableNumber<IntelBid128>);

}  // namespace xrpl::number_bench
