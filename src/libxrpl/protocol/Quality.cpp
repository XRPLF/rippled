#include <xrpl/protocol/Quality.h>

#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/STAmount.h>

#include <cstdint>
#include <limits>

namespace xrpl {

Quality::Quality(std::uint64_t value) : value_(value)
{
}

Quality::Quality(Amounts const& amount) : value_(getRate(amount.out, amount.in))
{
}

Quality&
Quality::operator++()
{
    XRPL_ASSERT(value_ > 0, "xrpl::Quality::operator++() : minimum value");
    --value_;
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
        value_ < std::numeric_limits<value_type>::max(),
        "xrpl::Quality::operator--() : maximum value");
    ++value_;
    return *this;
}

Quality
Quality::operator--(int)
{
    Quality prev(*this);
    --*this;
    return prev;
}

template <STAmount (*DivRoundFunc)(STAmount const&, STAmount const&, Asset const&, bool)>
static Amounts
ceilInImpl(Amounts const& amount, STAmount const& limit, bool roundUp, Quality const& quality)
{
    if (amount.in > limit)
    {
        Amounts result(limit, DivRoundFunc(limit, quality.rate(), amount.out.asset(), roundUp));
        // Clamp out
        if (result.out > amount.out)
            result.out = amount.out;
        XRPL_ASSERT(result.in == limit, "xrpl::ceilInImpl : result matches limit");
        return result;
    }
    XRPL_ASSERT(amount.in <= limit, "xrpl::ceilInImpl : result inside limit");
    return amount;
}

Amounts
Quality::ceilIn(Amounts const& amount, STAmount const& limit) const
{
    return ceilInImpl<divRound>(amount, limit, /* roundUp */ true, *this);
}

Amounts
Quality::ceilInStrict(Amounts const& amount, STAmount const& limit, bool roundUp) const
{
    return ceilInImpl<divRoundStrict>(amount, limit, roundUp, *this);
}

template <STAmount (*MulRoundFunc)(STAmount const&, STAmount const&, Asset const&, bool)>
static Amounts
ceilOutImpl(Amounts const& amount, STAmount const& limit, bool roundUp, Quality const& quality)
{
    if (amount.out > limit)
    {
        Amounts result(MulRoundFunc(limit, quality.rate(), amount.in.asset(), roundUp), limit);
        // Clamp in
        if (result.in > amount.in)
            result.in = amount.in;
        XRPL_ASSERT(result.out == limit, "xrpl::ceilOutImpl : result matches limit");
        return result;
    }
    XRPL_ASSERT(amount.out <= limit, "xrpl::ceilOutImpl : result inside limit");
    return amount;
}

Amounts
Quality::ceilOut(Amounts const& amount, STAmount const& limit) const
{
    return ceilOutImpl<mulRound>(amount, limit, /* roundUp */ true, *this);
}

Amounts
Quality::ceilOutStrict(Amounts const& amount, STAmount const& limit, bool roundUp) const
{
    return ceilOutImpl<mulRoundStrict>(amount, limit, roundUp, *this);
}

Quality
composedQuality(Quality const& lhs, Quality const& rhs)
{
    STAmount const lhsRate(lhs.rate());
    XRPL_ASSERT(lhsRate != beast::kZERO, "xrpl::composedQuality : nonzero left input");

    STAmount const rhsRate(rhs.rate());
    XRPL_ASSERT(rhsRate != beast::kZERO, "xrpl::composedQuality : nonzero right input");

    STAmount const rate(mulRound(lhsRate, rhsRate, lhsRate.asset(), true));

    std::uint64_t const storedExponent(rate.exponent() + 100);
    std::uint64_t const storedMantissa(rate.mantissa());

    XRPL_ASSERT(
        (storedExponent > 0) && (storedExponent <= 255), "xrpl::composedQuality : valid exponent");

    return Quality((storedExponent << (64 - 8)) | storedMantissa);
}

Quality
Quality::round(int digits) const
{
    // Modulus for mantissa
    static std::uint64_t const kMOD[17] = {
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

    auto exponent = value_ >> (64 - 8);
    auto mantissa = value_ & 0x00ffffffffffffffULL;
    mantissa += kMOD[digits] - 1;
    mantissa -= (mantissa % kMOD[digits]);

    return Quality{(exponent << (64 - 8)) | mantissa};
}

}  // namespace xrpl
