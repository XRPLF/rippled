//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

namespace ripple {

void STAmount::canonicalizeRound (bool isNative, std::uint64_t& value, int& offset, bool roundUp)
{
    if (!roundUp) // canonicalize already rounds down
        return;

    WriteLog (lsTRACE, STAmount) << "canonicalize< " << value << ":" << offset << (roundUp ? " up" : " down");

    if (isNative)
    {
        if (offset < 0)
        {
            int loops = 0;

            while (offset < -1)
            {
                value /= 10;
                ++offset;
                ++loops;
            }

            value += (loops >= 2) ? 9 : 10; // add before last divide
            value /= 10;
            ++offset;
        }
    }
    else if (value > STAmount::cMaxValue)
    {
        while (value > (10 * STAmount::cMaxValue))
        {
            value /= 10;
            ++offset;
        }

        value += 9;     // add before last divide
        value /= 10;
        ++offset;
    }

    WriteLog (lsTRACE, STAmount) << "canonicalize> " << value << ":" << offset << (roundUp ? " up" : " down");
}

STAmount STAmount::addRound (const STAmount& v1, const STAmount& v2, bool roundUp)
{
    v1.throwComparable (v2);

    if (v2.mValue == 0)
        return v1;

    if (v1.mValue == 0)
        return STAmount (v1.getFName (), v1.mCurrency, v1.mIssuer, v2.mValue, v2.mOffset, v2.mIsNegative);

    if (v1.mIsNative)
        return STAmount (v1.getFName (), v1.getSNValue () + v2.getSNValue ());

    int ov1 = v1.mOffset, ov2 = v2.mOffset;
    std::int64_t vv1 = static_cast<std::int64_t> (v1.mValue), vv2 = static_cast<std::uint64_t> (v2.mValue);

    if (v1.mIsNegative)
        vv1 = -vv1;

    if (v2.mIsNegative)
        vv2 = -vv2;

    if (ov1 < ov2)
    {
        while (ov1 < (ov2 - 1))
        {
            vv1 /= 10;
            ++ov1;
        }

        if (roundUp)
            vv1 += 9;

        vv1 /= 10;
        ++ov1;
    }

    if (ov2 < ov1)
    {
        while (ov2 < (ov1 - 1))
        {
            vv2 /= 10;
            ++ov2;
        }

        if (roundUp)
            vv2 += 9;

        vv2 /= 10;
        ++ov2;
    }

    std::int64_t fv = vv1 + vv2;

    if ((fv >= -10) && (fv <= 10))
        return STAmount (v1.getFName (), v1.mCurrency, v1.mIssuer);
    else if (fv >= 0)
    {
        std::uint64_t v = static_cast<std::uint64_t> (fv);
        canonicalizeRound (false, v, ov1, roundUp);
        return STAmount (v1.getFName (), v1.mCurrency, v1.mIssuer, v, ov1, false);
    }
    else
    {
        std::uint64_t v = static_cast<std::uint64_t> (-fv);
        canonicalizeRound (false, v, ov1, !roundUp);
        return STAmount (v1.getFName (), v1.mCurrency, v1.mIssuer, v, ov1, true);
    }
}

STAmount STAmount::subRound (const STAmount& v1, const STAmount& v2, bool roundUp)
{
    v1.throwComparable (v2);

    if (v2.mValue == 0)
        return v1;

    if (v1.mValue == 0)
        return STAmount (v1.getFName (), v1.mCurrency, v1.mIssuer, v2.mValue, v2.mOffset, !v2.mIsNegative);

    if (v1.mIsNative)
        return STAmount (v1.getFName (), v1.getSNValue () - v2.getSNValue ());

    int ov1 = v1.mOffset, ov2 = v2.mOffset;
    std::int64_t vv1 = static_cast<std::int64_t> (v1.mValue), vv2 = static_cast<std::uint64_t> (v2.mValue);

    if (v1.mIsNegative)
        vv1 = -vv1;

    if (!v2.mIsNegative)
        vv2 = -vv2;

    if (ov1 < ov2)
    {
        while (ov1 < (ov2 - 1))
        {
            vv1 /= 10;
            ++ov1;
        }

        if (roundUp)
            vv1 += 9;

        vv1 /= 10;
        ++ov1;
    }

    if (ov2 < ov1)
    {
        while (ov2 < (ov1 - 1))
        {
            vv2 /= 10;
            ++ov2;
        }

        if (roundUp)
            vv2 += 9;

        vv2 /= 10;
        ++ov2;
    }

    std::int64_t fv = vv1 + vv2;

    if ((fv >= -10) && (fv <= 10))
        return STAmount (v1.getFName (), v1.mCurrency, v1.mIssuer);
    else if (fv >= 0)
    {
        std::uint64_t v = static_cast<std::uint64_t> (fv);
        canonicalizeRound (false, v, ov1, roundUp);
        return STAmount (v1.getFName (), v1.mCurrency, v1.mIssuer, v, ov1, false);
    }
    else
    {
        std::uint64_t v = static_cast<std::uint64_t> (-fv);
        canonicalizeRound (false, v, ov1, !roundUp);
        return STAmount (v1.getFName (), v1.mCurrency, v1.mIssuer, v, ov1, true);
    }
}

STAmount STAmount::mulRound (
    const STAmount& v1, const STAmount& v2, const uint160& currency,
    const uint160& issuer, bool roundUp)
{
    if (v1 == zero || v2 == zero)
        return STAmount (currency, issuer);

    if (v1.mIsNative && v2.mIsNative && currency.isZero ())
    {
        std::uint64_t minV = (v1.getSNValue () < v2.getSNValue ()) ? v1.getSNValue () : v2.getSNValue ();
        std::uint64_t maxV = (v1.getSNValue () < v2.getSNValue ()) ? v2.getSNValue () : v1.getSNValue ();

        if (minV > 3000000000ull) // sqrt(cMaxNative)
            throw std::runtime_error ("Native value overflow");

        if (((maxV >> 32) * minV) > 2095475792ull) // cMaxNative / 2^32
            throw std::runtime_error ("Native value overflow");

        return STAmount (v1.getFName (), minV * maxV);
    }

    std::uint64_t value1 = v1.mValue, value2 = v2.mValue;
    int offset1 = v1.mOffset, offset2 = v2.mOffset;

    if (v1.mIsNative)
    {
        while (value1 < STAmount::cMinValue)
        {
            value1 *= 10;
            --offset1;
        }
    }

    if (v2.mIsNative)
    {
        while (value2 < STAmount::cMinValue)
        {
            value2 *= 10;
            --offset2;
        }
    }

    bool resultNegative = v1.mIsNegative != v2.mIsNegative;
    // Compute (numerator * denominator) / 10^14 with rounding
    // 10^16 <= result <= 10^18
    CBigNum v;

    if ((BN_add_word64 (&v, value1) != 1) || (BN_mul_word64 (&v, value2) != 1))
        throw std::runtime_error ("internal bn error");

    if (resultNegative != roundUp) // rounding down is automatic when we divide
        BN_add_word64 (&v, tenTo14m1);

    if  (BN_div_word64 (&v, tenTo14) == ((std::uint64_t) - 1))
        throw std::runtime_error ("internal bn error");

    // 10^16 <= product <= 10^18
    assert (BN_num_bytes (&v) <= 64);

    std::uint64_t amount = v.getuint64 ();
    int offset = offset1 + offset2 + 14;
    canonicalizeRound (currency.isZero (), amount, offset, resultNegative != roundUp);
    return STAmount (currency, issuer, amount, offset, resultNegative);
}

STAmount STAmount::divRound (
    const STAmount& num, const STAmount& den,
    const uint160& currency, const uint160& issuer, bool roundUp)
{
    if (den == zero)
        throw std::runtime_error ("division by zero");

    if (num == zero)
        return STAmount (currency, issuer);

    std::uint64_t numVal = num.mValue, denVal = den.mValue;
    int numOffset = num.mOffset, denOffset = den.mOffset;

    if (num.mIsNative)
        while (numVal < STAmount::cMinValue)
        {
            // Need to bring into range
            numVal *= 10;
            --numOffset;
        }

    if (den.mIsNative)
        while (denVal < STAmount::cMinValue)
        {
            denVal *= 10;
            --denOffset;
        }

    bool resultNegative = num.mIsNegative != den.mIsNegative;
    // Compute (numerator * 10^17) / denominator
    CBigNum v;

    if ((BN_add_word64 (&v, numVal) != 1) || (BN_mul_word64 (&v, tenTo17) != 1))
        throw std::runtime_error ("internal bn error");

    if (resultNegative != roundUp) // Rounding down is automatic when we divide
        BN_add_word64 (&v, denVal - 1);

    if (BN_div_word64 (&v, denVal) == ((std::uint64_t) - 1))
        throw std::runtime_error ("internal bn error");

    // 10^16 <= quotient <= 10^18
    assert (BN_num_bytes (&v) <= 64);

    std::uint64_t amount = v.getuint64 ();
    int offset = numOffset - denOffset - 17;
    canonicalizeRound (currency.isZero (), amount, offset, resultNegative != roundUp);
    return STAmount (currency, issuer, amount, offset, resultNegative);
}

} // ripple
