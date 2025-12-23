#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/STAmount.h>

#include <cstdint>
#include <limits>

namespace ripple {

Quality::Quality(std::uint64_t value) : m_value(value)
{
}

Quality::Quality(Amounts const& amount)
    : m_value(getRate(amount.out, amount.in))
{
}

Quality&
Quality::operator++()
{
    XRPL_ASSERT(m_value > 0, "ripple::Quality::operator++() : minimum value");
    --m_value;
    return *this;
}

Quality
Quality::operator++(int)
{
    Quality prev(*this);
    ++*this;
    return prev;
}

Quality&
Quality::operator--()
{
    XRPL_ASSERT(
        m_value < std::numeric_limits<value_type>::max(),
        "ripple::Quality::operator--() : maximum value");
    ++m_value;
    return *this;
}

Quality
Quality::operator--(int)
{
    Quality prev(*this);
    --*this;
    return prev;
}

template <STAmount (
    *DivRoundFunc)(STAmount const&, STAmount const&, Asset const&, bool)>
static Amounts
ceil_in_impl(
    Amounts const& amount,
    STAmount const& limit,
    bool roundUp,
    Quality const& quality)
{
    if (amount.in > limit)
    {
        Amounts result(
            limit,
            DivRoundFunc(limit, quality.rate(), amount.out.asset(), roundUp));
        // Clamp out
        if (result.out > amount.out)
            result.out = amount.out;
        XRPL_ASSERT(
            result.in == limit, "ripple::ceil_in_impl : result matches limit");
        return result;
    }
    XRPL_ASSERT(
        amount.in <= limit, "ripple::ceil_in_impl : result inside limit");
    return amount;
}

Amounts
Quality::ceil_in(Amounts const& amount, STAmount const& limit) const
{
    return ceil_in_impl<divRound>(amount, limit, /* roundUp */ true, *this);
}

Amounts
Quality::ceil_in_strict(
    Amounts const& amount,
    STAmount const& limit,
    bool roundUp) const
{
    return ceil_in_impl<divRoundStrict>(amount, limit, roundUp, *this);
}

template <STAmount (
    *MulRoundFunc)(STAmount const&, STAmount const&, Asset const&, bool)>
static Amounts
ceil_out_impl(
    Amounts const& amount,
    STAmount const& limit,
    bool roundUp,
    Quality const& quality)
{
    if (amount.out > limit)
    {
        Amounts result(
            MulRoundFunc(limit, quality.rate(), amount.in.asset(), roundUp),
            limit);
        // Clamp in
        if (result.in > amount.in)
            result.in = amount.in;
        XRPL_ASSERT(
            result.out == limit,
            "ripple::ceil_out_impl : result matches limit");
        return result;
    }
    XRPL_ASSERT(
        amount.out <= limit, "ripple::ceil_out_impl : result inside limit");
    return amount;
}

Amounts
Quality::ceil_out(Amounts const& amount, STAmount const& limit) const
{
    return ceil_out_impl<mulRound>(amount, limit, /* roundUp */ true, *this);
}

Amounts
Quality::ceil_out_strict(
    Amounts const& amount,
    STAmount const& limit,
    bool roundUp) const
{
    return ceil_out_impl<mulRoundStrict>(amount, limit, roundUp, *this);
}

Quality
composed_quality(Quality const& lhs, Quality const& rhs)
{
    STAmount const lhs_rate(lhs.rate());
    XRPL_ASSERT(
        lhs_rate != beast::zero,
        "ripple::composed_quality : nonzero left input");

    STAmount const rhs_rate(rhs.rate());
    XRPL_ASSERT(
        rhs_rate != beast::zero,
        "ripple::composed_quality : nonzero right input");

    STAmount const rate(mulRound(lhs_rate, rhs_rate, lhs_rate.asset(), true));

    std::uint64_t const stored_exponent(rate.exponent() + 100);
    std::uint64_t const stored_mantissa(rate.mantissa());

    XRPL_ASSERT(
        (stored_exponent > 0) && (stored_exponent <= 255),
        "ripple::composed_quality : valid exponent");

    return Quality((stored_exponent << (64 - 8)) | stored_mantissa);
}

Quality
Quality::round(int digits) const
{
    // Modulus for mantissa
    static std::uint64_t const mod[17] = {
        /* 0 */ 10000000000000000,
        /* 1 */ 1000000000000000,
        /* 2 */ 100000000000000,
        /* 3 */ 10000000000000,
        /* 4 */ 1000000000000,
        /* 5 */ 100000000000,
        /* 6 */ 10000000000,
        /* 7 */ 1000000000,
        /* 8 */ 100000000,
        /* 9 */ 10000000,
        /* 10 */ 1000000,
        /* 11 */ 100000,
        /* 12 */ 10000,
        /* 13 */ 1000,
        /* 14 */ 100,
        /* 15 */ 10,
        /* 16 */ 1,
    };

    auto exponent = m_value >> (64 - 8);
    auto mantissa = m_value & 0x00ffffffffffffffULL;
    mantissa += mod[digits] - 1;
    mantissa -= (mantissa % mod[digits]);

    return Quality{(exponent << (64 - 8)) | mantissa};
}

}  // namespace ripple
