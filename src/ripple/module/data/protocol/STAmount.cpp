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

#include <beast/cxx14/iterator.h>

namespace ripple {

SETUP_LOG (STAmount)

std::uint64_t STAmount::uRateOne  = STAmount::getRate (STAmount (1), STAmount (1));

bool STAmount::issuerFromString (uint160& uDstIssuer, const std::string& sIssuer)
{
    bool    bSuccess    = true;

    if (sIssuer.size () == (160 / 4))
    {
        uDstIssuer.SetHex (sIssuer);
    }
    else
    {
        RippleAddress raIssuer;

        if (raIssuer.setAccountID (sIssuer))
        {
            uDstIssuer  = raIssuer.getAccountID ();
        }
        else
        {
            bSuccess    = false;
        }
    }

    return bSuccess;
}

// --> sCurrency: "", "XRP", or three letter ISO code.
bool STAmount::currencyFromString (uint160& uDstCurrency, const std::string& sCurrency)
{
    bool    bSuccess    = true;

    if (sCurrency.empty () || !sCurrency.compare (SYSTEM_CURRENCY_CODE))
    {
        uDstCurrency.zero ();
    }
    else if (3 == sCurrency.size ())
    {
        Blob    vucIso (3);

        std::transform (sCurrency.begin (), sCurrency.end (), vucIso.begin (), ::toupper);

        // std::string  sIso;
        // sIso.assign(vucIso.begin(), vucIso.end());
        // Log::out() << "currency: " << sIso;

        Serializer  s;

        s.addZeros (96 / 8);
        s.addRaw (vucIso);
        s.addZeros (16 / 8);
        s.addZeros (24 / 8);

        s.get160 (uDstCurrency, 0);
    }
    else if (40 == sCurrency.size ())
    {
        bSuccess    = uDstCurrency.SetHex (sCurrency);
    }
    else
    {
        bSuccess    = false;
    }

    return bSuccess;
}

std::string STAmount::getHumanCurrency () const
{
    return createHumanCurrency (mCurrency);
}

bool STAmount::bSetJson (const Json::Value& jvSource)
{
    try
    {
        STAmount    saParsed (sfGeneric, jvSource);

        *this   = saParsed;

        return true;
    }
    catch (const std::exception& e)
    {
        WriteLog (lsINFO, STAmount) << "bSetJson(): caught: " << e.what ();
        return false;
    }
}

STAmount::STAmount (SField::ref n, const Json::Value& v)
    : SerializedType (n), mValue (0), mOffset (0), mIsNegative (false)
{
    Json::Value value, currency, issuer;

    if (v.isObject ())
    {
        WriteLog (lsTRACE, STAmount) <<
            "value='" << v["value"].asString () <<
            "', currency='" << v["currency"].asString () <<
            "', issuer='" << v["issuer"].asString () <<
            "')";

        value       = v[jss::value];
        currency    = v[jss::currency];
        issuer      = v[jss::issuer];
    }
    else if (v.isArray ())
    {
        value = v.get (Json::UInt (0), 0);
        currency = v.get (Json::UInt (1), Json::nullValue);
        issuer = v.get (Json::UInt (2), Json::nullValue);
    }
    else if (v.isString ())
    {
        std::string val = v.asString ();
        std::vector<std::string> elements;
        boost::split (elements, val, boost::is_any_of ("\t\n\r ,/"));

        if (elements.size () > 3)
            throw std::runtime_error ("invalid amount string");

        value = elements[0];

        if (elements.size () > 1)
            currency = elements[1];

        if (elements.size () > 2)
            issuer = elements[2];
    }
    else
        value = v;

    mIsNative = !currency.isString () || currency.asString ().empty () || (currency.asString () == SYSTEM_CURRENCY_CODE);

    if (mIsNative)
    {
        if (v.isObject ())
            throw std::runtime_error ("XRP may not be specified as an object");
    }
    else
    {
        // non-XRP
        if (!currencyFromString (mCurrency, currency.asString ()))
            throw std::runtime_error ("invalid currency");

        if (!issuer.isString ()
                || !issuerFromString (mIssuer, issuer.asString ()))
            throw std::runtime_error ("invalid issuer");

        if (mIssuer.isZero ())
            throw std::runtime_error ("invalid issuer");
    }

    if (value.isInt ())
    {
        if (value.asInt () >= 0)
            mValue = value.asInt ();
        else
        {
            mValue = -value.asInt ();
            mIsNegative = true;
        }

        canonicalize ();
    }
    else if (value.isUInt ())
    {
        mValue = v.asUInt ();

        canonicalize ();
    }
    else if (value.isString ())
    {
        if (mIsNative)
        {
            std::int64_t val = beast::lexicalCastThrow <std::int64_t> (value.asString ());

            if (val >= 0)
                mValue = val;
            else
            {
                mValue = -val;
                mIsNegative = true;
            }

            canonicalize ();
        }
        else
        {
            setValue (value.asString ());
        }
    }
    else
        throw std::runtime_error ("invalid amount type");
}

std::string STAmount::createHumanCurrency (const uint160& uCurrency)
{
    static uint160 const sIsoBits ("FFFFFFFFFFFFFFFFFFFFFFFF000000FFFFFFFFFF");

    if (uCurrency.isZero ())
    {
        return SYSTEM_CURRENCY_CODE;
    }

    if (CURRENCY_ONE == uCurrency)
    {
        return "1";
    }

    if ((uCurrency & sIsoBits).isZero ())
    {
        // The offset of the 3 character ISO code in the currency descriptor
        int const isoOffset = 12;

        std::string const iso(
            uCurrency.data () + isoOffset,
            uCurrency.data () + isoOffset + 3);

        // Specifying the system currency code using ISO-style representation
        // is not allowed.
        if (iso != SYSTEM_CURRENCY_CODE)
            return iso;
    }

    return to_string (uCurrency);
}

bool STAmount::setValue (const std::string& sAmount)
{
    // Note: mIsNative and mCurrency must be set already!

    static boost::regex reNumber ("\\`([+-]?)(\\d*)(\\.(\\d*))?([eE]([+-]?)(\\d+))?\\'");
    boost::smatch smMatch;

    if (!boost::regex_match (sAmount, smMatch, reNumber))
    {
        WriteLog (lsWARNING, STAmount) << "Number not valid: \"" << sAmount << "\"";
        return false;
    }

    // Match fields: 0 = whole input, 1 = sign, 2 = integer portion, 3 = whole fraction (with '.')
    // 4 = fraction (without '.'), 5 = whole exponent (with 'e'), 6 = exponent sign, 7 = exponent number

    try
    {
        if ((smMatch[2].length () + smMatch[4].length ()) > 32)
        {
            WriteLog (lsWARNING, STAmount) << "Overlong number: " << sAmount;
            return false;
        }

        mIsNegative = (smMatch[1].matched && (smMatch[1] == "-"));

        if (!smMatch[4].matched) // integer only
        {
            mValue = beast::lexicalCast <std::uint64_t> (std::string (smMatch[2]));
            mOffset = 0;
        }
        else
        {
            // integer and fraction
            mValue = beast::lexicalCast <std::uint64_t> (smMatch[2] + smMatch[4]);
            mOffset = - (smMatch[4].length ());
        }

        if (smMatch[5].matched)
        {
            // we have an exponent
            if (smMatch[6].matched && (smMatch[6] == "-"))
                mOffset -= beast::lexicalCast <int> (std::string (smMatch[7]));
            else
                mOffset += beast::lexicalCast <int> (std::string (smMatch[7]));
        }
    }
    catch (...)
    {
        WriteLog (lsWARNING, STAmount) << "Number not parsed: \"" << sAmount << "\"";
        return false;
    }

    WriteLog (lsTRACE, STAmount) << "Float \"" << sAmount << "\" parsed to " << mValue << " : " << mOffset;

    if (mIsNative)
    {
        if (smMatch[3].matched)
            mOffset -= SYSTEM_CURRENCY_PRECISION;

        while (mOffset > 0)
        {
            mValue  *= 10;
            --mOffset;
        }

        while (mOffset < 0)
        {
            mValue  /= 10;
            ++mOffset;
        }
    }
    else
        canonicalize ();

    return true;
}

// Not meant to be the ultimate parser.  For use by RPC which is supposed to be sane and trusted.
// Native has special handling:
// - Integer values are in base units.
// - Float values are in float units.
// - To avoid a mistake float value for native are specified with a "^" in place of a "."
// <-- bValid: true = valid
bool STAmount::setFullValue (const std::string& sAmount, const std::string& sCurrency, const std::string& sIssuer)
{
    //
    // Figure out the currency.
    //
    if (!currencyFromString (mCurrency, sCurrency))
    {
        WriteLog (lsINFO, STAmount) << "Currency malformed: " << sCurrency;

        return false;
    }

    mIsNative   = !mCurrency;

    //
    // Figure out the issuer.
    //
    RippleAddress   naIssuerID;

    // Issuer must be "" or a valid account string.
    if (!naIssuerID.setAccountID (sIssuer))
    {
        WriteLog (lsINFO, STAmount) << "Issuer malformed: " << sIssuer;

        return false;
    }

    mIssuer = naIssuerID.getAccountID ();

    // Stamps not must have an issuer.
    if (mIsNative && !mIssuer.isZero ())
    {
        WriteLog (lsINFO, STAmount) << "Issuer specified for XRP: " << sIssuer;

        return false;
    }

    return setValue (sAmount);
}

// amount = value * [10 ^ offset]
// representation range is 10^80 - 10^(-80)
// on the wire, high 8 bits are (offset+142), low 56 bits are value
// value is zero if amount is zero, otherwise value is 10^15 to (10^16 - 1) inclusive

void STAmount::canonicalize ()
{
    if (mCurrency.isZero ())
    {
        // native currency amounts should always have an offset of zero
        mIsNative = true;

        if (mValue == 0)
        {
            mOffset = 0;
            mIsNegative = false;
            return;
        }

        while (mOffset < 0)
        {
            mValue /= 10;
            ++mOffset;
        }

        while (mOffset > 0)
        {
            mValue *= 10;
            --mOffset;
        }

        if (mValue > cMaxNative)
            throw std::runtime_error ("Native currency amount out of range");

        return;
    }

    mIsNative = false;

    if (mValue == 0)
    {
        mOffset = -100;
        mIsNegative = false;
        return;
    }

    while ((mValue < cMinValue) && (mOffset > cMinOffset))
    {
        mValue *= 10;
        --mOffset;
    }

    while (mValue > cMaxValue)
    {
        if (mOffset >= cMaxOffset)
            throw std::runtime_error ("value overflow");

        mValue /= 10;
        ++mOffset;
    }

    if ((mOffset < cMinOffset) || (mValue < cMinValue))
    {
        mValue = 0;
        mOffset = 0;
        mIsNegative = false;
    }

    if (mOffset > cMaxOffset)
        throw std::runtime_error ("value overflow");

    assert ((mValue == 0) || ((mValue >= cMinValue) && (mValue <= cMaxValue)));
    assert ((mValue == 0) || ((mOffset >= cMinOffset) && (mOffset <= cMaxOffset)));
    assert ((mValue != 0) || (mOffset != -100));
}

void STAmount::add (Serializer& s) const
{
    if (mIsNative)
    {
        assert (mOffset == 0);

        if (!mIsNegative)
            s.add64 (mValue | cPosNative);
        else
            s.add64 (mValue);
    }
    else
    {
        if (*this == zero)
            s.add64 (cNotNative);
        else if (mIsNegative) // 512 = not native
            s.add64 (mValue | (static_cast<std::uint64_t> (mOffset + 512 + 97) << (64 - 10)));
        else // 256 = positive
            s.add64 (mValue | (static_cast<std::uint64_t> (mOffset + 512 + 256 + 97) << (64 - 10)));

        s.add160 (mCurrency);
        s.add160 (mIssuer);
    }
}

STAmount STAmount::createFromInt64 (SField::ref name, std::int64_t value)
{
    return value >= 0
           ? STAmount (name, static_cast<std::uint64_t> (value), false)
           : STAmount (name, static_cast<std::uint64_t> (-value), true);
}

void STAmount::setValue (const STAmount& a)
{
    mCurrency   = a.mCurrency;
    mIssuer     = a.mIssuer;
    mValue      = a.mValue;
    mOffset     = a.mOffset;
    mIsNative   = a.mIsNative;
    mIsNegative = a.mIsNegative;
}

int STAmount::compare (const STAmount& a) const
{
    // Compares the value of a to the value of this STAmount, amounts must be comparable
    if (mIsNegative != a.mIsNegative) return mIsNegative ? -1 : 1;

    if (!mValue)
    {
        if (a.mIsNegative) return 1;

        return a.mValue ? -1 : 0;
    }

    if (!a.mValue) return 1;

    if (mOffset > a.mOffset) return mIsNegative ? -1 : 1;

    if (mOffset < a.mOffset) return mIsNegative ? 1 : -1;

    if (mValue > a.mValue) return mIsNegative ? -1 : 1;

    if (mValue < a.mValue) return mIsNegative ? 1 : -1;

    return 0;
}

STAmount* STAmount::construct (SerializerIterator& sit, SField::ref name)
{
    std::uint64_t value = sit.get64 ();

    if ((value & cNotNative) == 0)
    {
        // native
        if ((value & cPosNative) != 0)
            return new STAmount (name, value & ~cPosNative, false); // positive
        else if (value == 0)
            throw std::runtime_error ("negative zero is not canonical");

        return new STAmount (name, value, true); // negative
    }

    uint160 currency = sit.get160 ();

    if (!currency)
        throw std::runtime_error ("invalid non-native currency");

    uint160 issuer = sit.get160 ();

    int offset = static_cast<int> (value >> (64 - 10)); // 10 bits for the offset, sign and "not native" flag
    value &= ~ (1023ull << (64 - 10));

    if (value)
    {
        bool isNegative = (offset & 256) == 0;
        offset = (offset & 255) - 97; // center the range

        if ((value < cMinValue) || (value > cMaxValue) || (offset < cMinOffset) || (offset > cMaxOffset))
            throw std::runtime_error ("invalid currency value");

        return new STAmount (name, currency, issuer, value, offset, isNegative);
    }

    if (offset != 512)
        throw std::runtime_error ("invalid currency value");

    return new STAmount (name, currency, issuer);
}

std::int64_t STAmount::getSNValue () const
{
    // signed native value
    if (!mIsNative) throw std::runtime_error ("not native");

    if (mIsNegative) return - static_cast<std::int64_t> (mValue);

    return static_cast<std::int64_t> (mValue);
}

void STAmount::setSNValue (std::int64_t v)
{
    if (!mIsNative) throw std::runtime_error ("not native");

    if (v > 0)
    {
        mIsNegative = false;
        mValue = static_cast<std::uint64_t> (v);
    }
    else
    {
        mIsNegative = true;
        mValue = static_cast<std::uint64_t> (-v);
    }
}

std::string STAmount::getText () const
{
    // keep full internal accuracy, but make more human friendly if posible
    if (*this == zero)
        return "0";

    std::string const raw_value (std::to_string (mValue));
    std::string ret;

    if (mIsNegative)
        ret.append (1, '-');

    bool const scientific ((mOffset != 0) && ((mOffset < -25) || (mOffset > -5)));

    if (mIsNative || scientific)
    {
        ret.append (raw_value);

        if(scientific)
        {
            ret.append (1, 'e');
            ret.append (std::to_string (mOffset));
        }

        return ret;
    }

    assert (mOffset + 43 > 0);

    size_t const pad_prefix = 27;
    size_t const pad_suffix = 23;

    std::string val;
    val.reserve (raw_value.length () + pad_prefix + pad_suffix);
    val.append (pad_prefix, '0');
    val.append (raw_value);
    val.append (pad_suffix, '0');

    size_t const offset (mOffset + 43);

    auto pre_from (val.begin ());
    auto const pre_to (val.begin () + offset);

    auto const post_from (val.begin () + offset);
    auto post_to (val.end ());

    // Crop leading zeroes. Take advantage of the fact that there's always a
    // fixed amount of leading zeroes and skip them.
    if (std::distance (pre_from, pre_to) > pad_prefix)
        pre_from += pad_prefix;

    assert (post_to >= post_from);

    pre_from = std::find_if (pre_from, pre_to,
        [](char c)
        {
            return c != '0';
        });

    // Crop trailing zeroes. Take advantage of the fact that there's always a
    // fixed amount of trailing zeroes and skip them.
    if (std::distance (post_from, post_to) > pad_suffix)
        post_to -= pad_suffix;

    assert (post_to >= post_from);

    post_to = std::find_if(
        std::make_reverse_iterator (post_to),
        std::make_reverse_iterator (post_from),
        [](char c)
        {
            return c != '0';
        }).base();

    // Assemble the output:
    if (pre_from == pre_to)
        ret.append (1, '0');
    else
        ret.append(pre_from, pre_to);

    if (post_to != post_from)
    {
        ret.append (1, '.');
        ret.append (post_from, post_to);
    }

    return ret;
}

bool STAmount::isComparable (const STAmount& t) const
{
    // are these two STAmount instances in the same currency
    if (mIsNative) return t.mIsNative;

    if (t.mIsNative) return false;

    return mCurrency == t.mCurrency;
}

bool STAmount::isEquivalent (const SerializedType& t) const
{
    const STAmount* v = dynamic_cast<const STAmount*> (&t);

    if (!v) return false;

    return isComparable (*v) && (mIsNegative == v->mIsNegative) && (mValue == v->mValue) && (mOffset == v->mOffset);
}

void STAmount::throwComparable (const STAmount& t) const
{
    // throw an exception if these two STAmount instances are incomparable
    if (!isComparable (t))
        throw std::runtime_error ("amounts are not comparable");
}

bool STAmount::operator== (const STAmount& a) const
{
    return isComparable (a) && (mIsNegative == a.mIsNegative) && (mOffset == a.mOffset) && (mValue == a.mValue);
}

bool STAmount::operator!= (const STAmount& a) const
{
    return (mOffset != a.mOffset) || (mValue != a.mValue) || (mIsNegative != a.mIsNegative) || !isComparable (a);
}

bool STAmount::operator< (const STAmount& a) const
{
    throwComparable (a);
    return compare (a) < 0;
}

bool STAmount::operator> (const STAmount& a) const
{
    throwComparable (a);
    return compare (a) > 0;
}

bool STAmount::operator<= (const STAmount& a) const
{
    throwComparable (a);
    return compare (a) <= 0;
}

bool STAmount::operator>= (const STAmount& a) const
{
    throwComparable (a);
    return compare (a) >= 0;
}

STAmount& STAmount::operator+= (const STAmount& a)
{
    *this = *this + a;
    return *this;
}

STAmount& STAmount::operator-= (const STAmount& a)
{
    *this = *this - a;
    return *this;
}

STAmount STAmount::operator- (void) const
{
    if (mValue == 0) return *this;

    return STAmount (getFName (), mCurrency, mIssuer, mValue, mOffset, mIsNative, !mIsNegative);
}

STAmount& STAmount::operator= (std::uint64_t v)
{
    // does not copy name, does not change currency type
    mOffset = 0;
    mValue = v;
    mIsNegative = false;

    if (!mIsNative) canonicalize ();

    return *this;
}

STAmount& STAmount::operator+= (std::uint64_t v)
{
    assert (mIsNative);

    if (!mIsNative)
        throw std::runtime_error ("not native");

    setSNValue (getSNValue () + static_cast<std::int64_t> (v));
    return *this;
}

STAmount& STAmount::operator-= (std::uint64_t v)
{
    assert (mIsNative);

    if (!mIsNative)
        throw std::runtime_error ("not native");

    setSNValue (getSNValue () - static_cast<std::int64_t> (v));
    return *this;
}

bool STAmount::operator< (std::uint64_t v) const
{
    return getSNValue () < static_cast<std::int64_t> (v);
}

bool STAmount::operator> (std::uint64_t v) const
{
    return getSNValue () > static_cast<std::int64_t> (v);
}

bool STAmount::operator<= (std::uint64_t v) const
{
    return getSNValue () <= static_cast<std::int64_t> (v);
}

bool STAmount::operator>= (std::uint64_t v) const
{
    return getSNValue () >= static_cast<std::int64_t> (v);
}

STAmount STAmount::operator+ (std::uint64_t v) const
{
    return STAmount (getFName (), getSNValue () + static_cast<std::int64_t> (v));
}

STAmount STAmount::operator- (std::uint64_t v) const
{
    return STAmount (getFName (), getSNValue () - static_cast<std::int64_t> (v));
}

STAmount::operator double () const
{
    // Does not keep the precise value. Not recommended
    if (!mValue)
        return 0.0;

    if (mIsNegative) return -1.0 * static_cast<double> (mValue) * pow (10.0, mOffset);

    return static_cast<double> (mValue) * pow (10.0, mOffset);
}

STAmount operator+ (const STAmount& v1, const STAmount& v2)
{
    v1.throwComparable (v2);

    if (v2 == zero)
        return v1;

    if (v1 == zero)
    {
        // Result must be in terms of v1 currency and issuer.
        return STAmount (v1.getFName (), v1.mCurrency, v1.mIssuer, v2.mValue, v2.mOffset, v2.mIsNegative);
    }

    if (v1.mIsNative)
        return STAmount (v1.getFName (), v1.getSNValue () + v2.getSNValue ());

    int ov1 = v1.mOffset, ov2 = v2.mOffset;
    std::int64_t vv1 = static_cast<std::int64_t> (v1.mValue), vv2 = static_cast<std::int64_t> (v2.mValue);

    if (v1.mIsNegative) vv1 = -vv1;

    if (v2.mIsNegative) vv2 = -vv2;

    while (ov1 < ov2)
    {
        vv1 /= 10;
        ++ov1;
    }

    while (ov2 < ov1)
    {
        vv2 /= 10;
        ++ov2;
    }

    // this addition cannot overflow an std::int64_t, it can overflow an STAmount and the constructor will throw

    std::int64_t fv = vv1 + vv2;

    if ((fv >= -10) && (fv <= 10))
        return STAmount (v1.getFName (), v1.mCurrency, v1.mIssuer);
    else if (fv >= 0)
        return STAmount (v1.getFName (), v1.mCurrency, v1.mIssuer, fv, ov1, false);
    else
        return STAmount (v1.getFName (), v1.mCurrency, v1.mIssuer, -fv, ov1, true);
}

STAmount operator- (const STAmount& v1, const STAmount& v2)
{
    v1.throwComparable (v2);

    if (v2 == zero)
        return v1;

    if (v2.mIsNative)
    {
        // XXX This could be better, check for overflow and that maximum range is covered.
        return STAmount::createFromInt64 (v1.getFName (), v1.getSNValue () - v2.getSNValue ());
    }

    int ov1 = v1.mOffset, ov2 = v2.mOffset;
    std::int64_t vv1 = static_cast<std::int64_t> (v1.mValue), vv2 = static_cast<std::int64_t> (v2.mValue);

    if (v1.mIsNegative) vv1 = -vv1;

    if (v2.mIsNegative) vv2 = -vv2;

    while (ov1 < ov2)
    {
        vv1 /= 10;
        ++ov1;
    }

    while (ov2 < ov1)
    {
        vv2 /= 10;
        ++ov2;
    }

    // this subtraction cannot overflow an std::int64_t, it can overflow an STAmount and the constructor will throw

    std::int64_t fv = vv1 - vv2;

    if ((fv >= -10) && (fv <= 10))
        return STAmount (v1.getFName (), v1.mCurrency, v1.mIssuer);
    else if (fv >= 0)
        return STAmount (v1.getFName (), v1.mCurrency, v1.mIssuer, fv, ov1, false);
    else
        return STAmount (v1.getFName (), v1.mCurrency, v1.mIssuer, -fv, ov1, true);
}

// NIKB TODO Make Amount::divide skip math if den == QUALITY_ONE
STAmount STAmount::divide (
    const STAmount& num, const STAmount& den, const uint160& currency,
    const uint160& issuer)
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

    // Compute (numerator * 10^17) / denominator
    CBigNum v;

    if ((BN_add_word64 (&v, numVal) != 1) ||
            (BN_mul_word64 (&v, tenTo17) != 1) ||
            (BN_div_word64 (&v, denVal) == ((std::uint64_t) - 1)))
    {
        throw std::runtime_error ("internal bn error");
    }

    // 10^16 <= quotient <= 10^18
    assert (BN_num_bytes (&v) <= 64);

    return STAmount (currency, issuer, v.getuint64 () + 5,
                     numOffset - denOffset - 17, num.mIsNegative != den.mIsNegative);
}

STAmount STAmount::multiply (
    const STAmount& v1, const STAmount& v2, const uint160& currency,
    const uint160& issuer)
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

    // Compute (numerator * denominator) / 10^14 with rounding
    // 10^16 <= result <= 10^18
    CBigNum v;

    if ((BN_add_word64 (&v, value1) != 1) ||
            (BN_mul_word64 (&v, value2) != 1) ||
            (BN_div_word64 (&v, tenTo14) == ((std::uint64_t) - 1)))
    {
        throw std::runtime_error ("internal bn error");
    }

    // 10^16 <= product <= 10^18
    assert (BN_num_bytes (&v) <= 64);

    return STAmount (currency, issuer, v.getuint64 () + 7, offset1 + offset2 + 14,
                     v1.mIsNegative != v2.mIsNegative);
}

// Convert an offer into an index amount so they sort by rate.
// A taker will take the best, lowest, rate first.
// (e.g. a taker will prefer pay 1 get 3 over pay 1 get 2.
// --> offerOut: takerGets: How much the offerer is selling to the taker.
// -->  offerIn: takerPays: How much the offerer is receiving from the taker.
// <--    uRate: normalize(offerIn/offerOut)
//             A lower rate is better for the person taking the order.
//             The taker gets more for less with a lower rate.
// Zero is returned if the offer is worthless.
std::uint64_t STAmount::getRate (const STAmount& offerOut, const STAmount& offerIn)
{
    if (offerOut == zero)
        return 0;

    try
    {
        STAmount r = divide (offerIn, offerOut, CURRENCY_ONE, ACCOUNT_ONE);

        if (r == zero) // offer is too good
            return 0;

        assert ((r.getExponent () >= -100) && (r.getExponent () <= 155));

        std::uint64_t ret = r.getExponent () + 100;

        return (ret << (64 - 8)) | r.getMantissa ();
    }
    catch (...)
    {
        // overflow -- very bad offer
        return 0;
    }
}

STAmount STAmount::setRate (std::uint64_t rate)
{
    if (rate == 0)
        return STAmount (CURRENCY_ONE, ACCOUNT_ONE);

    std::uint64_t mantissa = rate & ~ (255ull << (64 - 8));
    int exponent = static_cast<int> (rate >> (64 - 8)) - 100;

    return STAmount (CURRENCY_ONE, ACCOUNT_ONE, mantissa, exponent);
}

STAmount STAmount::getPay (const STAmount& offerOut, const STAmount& offerIn, const STAmount& needed)
{
    // Someone wants to get (needed) out of the offer, how much should they pay in?
    if (offerOut == zero)
        return STAmount (offerIn.getCurrency (), offerIn.getIssuer ());

    if (needed >= offerOut)
    {
        // They need more than offered, pay full amount.
        return needed;
    }

    STAmount ret = divide (multiply (needed, offerIn, CURRENCY_ONE, ACCOUNT_ONE), offerOut, offerIn.getCurrency (), offerIn.getIssuer ());

    return (ret > offerIn) ? offerIn : ret;
}

STAmount STAmount::deserialize (SerializerIterator& it)
{
    std::unique_ptr<STAmount> s (dynamic_cast<STAmount*> (construct (it, sfGeneric)));
    STAmount ret (*s);
    return ret;
}

std::string STAmount::getFullText () const
{
    std::string ret;

    ret.reserve(64);
    ret = getText () + "/" + getHumanCurrency ();

    if (!mIsNative)
    {
        ret += "/";

        if (!mIssuer)
            ret += "0";
        else if (mIssuer == ACCOUNT_ONE)
            ret += "1";
        else
            ret += RippleAddress::createHumanAccountID (mIssuer);
    }

    return ret;
}

STAmount STAmount::getRound () const
{
    if (mIsNative)
        return *this;

    std::uint64_t valueDigits = mValue % 1000000000ull;

    if (valueDigits == 1)
        return STAmount (mCurrency, mIssuer, mValue - 1, mOffset, mIsNegative);
    else if (valueDigits == 999999999ull)
        return STAmount (mCurrency, mIssuer, mValue + 1, mOffset, mIsNegative);

    return *this;
}

void STAmount::roundSelf ()
{
    if (mIsNative)
        return;

    std::uint64_t valueDigits = mValue % 1000000000ull;

    if (valueDigits == 1)
    {
        mValue -= 1;

        if (mValue < cMinValue)
            canonicalize ();
    }
    else if (valueDigits == 999999999ull)
    {
        mValue += 1;

        if (mValue > cMaxValue)
            canonicalize ();
    }
}

void STAmount::setJson (Json::Value& elem) const
{
    elem = Json::objectValue;

    if (!mIsNative)
    {
        // It is an error for currency or issuer not to be specified for valid json.

        elem[jss::value]      = getText ();
        elem[jss::currency]   = getHumanCurrency ();
        elem[jss::issuer]     = RippleAddress::createHumanAccountID (mIssuer);
    }
    else
    {
        elem = getText ();
    }
}

Json::Value STAmount::getJson (int) const
{
    Json::Value elem;
    setJson (elem);
    return elem;
}

//------------------------------------------------------------------------------

class STAmount_test : public beast::unit_test::suite
{
public:
    static STAmount serializeAndDeserialize (const STAmount& s)
    {
        Serializer ser;

        s.add (ser);

        SerializerIterator sit (ser);

        return STAmount::deserialize (sit);
    }

    //--------------------------------------------------------------------------

    bool roundTest (int n, int d, int m)
    {
        // check STAmount rounding
        STAmount num (CURRENCY_ONE, ACCOUNT_ONE, n);
        STAmount den (CURRENCY_ONE, ACCOUNT_ONE, d);
        STAmount mul (CURRENCY_ONE, ACCOUNT_ONE, m);
        STAmount quot = STAmount::divide (n, d, CURRENCY_ONE, ACCOUNT_ONE);
        STAmount res = STAmount::multiply (quot, mul, CURRENCY_ONE, ACCOUNT_ONE);

        expect (! res.isNative (), "Product should not be native");

        res.roundSelf ();

        STAmount cmp (CURRENCY_ONE, ACCOUNT_ONE, (n * m) / d);

        expect (! cmp.isNative (), "Comparison amount should not be native");

        if (res != cmp)
        {
            cmp.throwComparable (res);

            WriteLog (lsWARNING, STAmount) << "(" << num.getText () << "/" << den.getText () << ") X " << mul.getText () << " = "
                                       << res.getText () << " not " << cmp.getText ();

            fail ("Rounding");

            return false;
        }
        else
        {
            pass ();
        }

        return true;
    }

    void mulTest (int a, int b)
    {
        STAmount aa (CURRENCY_ONE, ACCOUNT_ONE, a);
        STAmount bb (CURRENCY_ONE, ACCOUNT_ONE, b);
        STAmount prod1 (STAmount::multiply (aa, bb, CURRENCY_ONE, ACCOUNT_ONE));

        expect (! prod1.isNative ());

        STAmount prod2 (CURRENCY_ONE, ACCOUNT_ONE, static_cast<std::uint64_t> (a) * static_cast<std::uint64_t> (b));

        if (prod1 != prod2)
        {
            WriteLog (lsWARNING, STAmount) << "nn(" << aa.getFullText () << " * " << bb.getFullText () << ") = " << prod1.getFullText ()
                                           << " not " << prod2.getFullText ();

            fail ("Multiplication result is not exact");
        }
        else
        {
            pass ();
        }

        aa = a;
        prod1 = STAmount::multiply (aa, bb, CURRENCY_ONE, ACCOUNT_ONE);

        if (prod1 != prod2)
        {
            WriteLog (lsWARNING, STAmount) << "n(" << aa.getFullText () << " * " << bb.getFullText () << ") = " << prod1.getFullText ()
                                           << " not " << prod2.getFullText ();
            fail ("Multiplication result is not exact");
        }
        else
        {
            pass ();
        }
    }

    //--------------------------------------------------------------------------

    void testSetValue ()
    {
        testcase ("set value");

        STAmount    saTmp;

    #if 0
        // Check native floats
        saTmp.setFullValue ("1^0");
        BOOST_CHECK_MESSAGE (SYSTEM_CURRENCY_PARTS == saTmp.getNValue (), "float integer failed");
        saTmp.setFullValue ("0^1");
        BOOST_CHECK_MESSAGE (SYSTEM_CURRENCY_PARTS / 10 == saTmp.getNValue (), "float fraction failed");
        saTmp.setFullValue ("0^12");
        BOOST_CHECK_MESSAGE (12 * SYSTEM_CURRENCY_PARTS / 100 == saTmp.getNValue (), "float fraction failed");
        saTmp.setFullValue ("1^2");
        BOOST_CHECK_MESSAGE (SYSTEM_CURRENCY_PARTS + (2 * SYSTEM_CURRENCY_PARTS / 10) == saTmp.getNValue (), "float combined failed");
    #endif

        // Check native integer
        saTmp.setFullValue ("1");
        expect (1 == saTmp.getNValue (), "should be equal");
    }

    //--------------------------------------------------------------------------

    void testNativeCurrency ()
    {
        testcase ("native currency");

        STAmount zeroSt, one (1), hundred (100);

        unexpected (serializeAndDeserialize (zeroSt) != zeroSt, "STAmount fail");

        unexpected (serializeAndDeserialize (one) != one, "STAmount fail");

        unexpected (serializeAndDeserialize (hundred) != hundred, "STAmount fail");

        unexpected (!zeroSt.isNative (), "STAmount fail");

        unexpected (!hundred.isNative (), "STAmount fail");

        unexpected (zeroSt != zero, "STAmount fail");

        unexpected (one == zero, "STAmount fail");

        unexpected (hundred == zero, "STAmount fail");

        unexpected ((zeroSt < zeroSt), "STAmount fail");

        unexpected (! (zeroSt < one), "STAmount fail");

        unexpected (! (zeroSt < hundred), "STAmount fail");

        unexpected ((one < zeroSt), "STAmount fail");

        unexpected ((one < one), "STAmount fail");

        unexpected (! (one < hundred), "STAmount fail");

        unexpected ((hundred < zeroSt), "STAmount fail");

        unexpected ((hundred < one), "STAmount fail");

        unexpected ((hundred < hundred), "STAmount fail");

        unexpected ((zeroSt > zeroSt), "STAmount fail");

        unexpected ((zeroSt > one), "STAmount fail");

        unexpected ((zeroSt > hundred), "STAmount fail");

        unexpected (! (one > zeroSt), "STAmount fail");

        unexpected ((one > one), "STAmount fail");

        unexpected ((one > hundred), "STAmount fail");

        unexpected (! (hundred > zeroSt), "STAmount fail");

        unexpected (! (hundred > one), "STAmount fail");

        unexpected ((hundred > hundred), "STAmount fail");

        unexpected (! (zeroSt <= zeroSt), "STAmount fail");

        unexpected (! (zeroSt <= one), "STAmount fail");

        unexpected (! (zeroSt <= hundred), "STAmount fail");

        unexpected ((one <= zeroSt), "STAmount fail");

        unexpected (! (one <= one), "STAmount fail");

        unexpected (! (one <= hundred), "STAmount fail");

        unexpected ((hundred <= zeroSt), "STAmount fail");

        unexpected ((hundred <= one), "STAmount fail");

        unexpected (! (hundred <= hundred), "STAmount fail");

        unexpected (! (zeroSt >= zeroSt), "STAmount fail");

        unexpected ((zeroSt >= one), "STAmount fail");

        unexpected ((zeroSt >= hundred), "STAmount fail");

        unexpected (! (one >= zeroSt), "STAmount fail");

        unexpected (! (one >= one), "STAmount fail");

        unexpected ((one >= hundred), "STAmount fail");

        unexpected (! (hundred >= zeroSt), "STAmount fail");

        unexpected (! (hundred >= one), "STAmount fail");

        unexpected (! (hundred >= hundred), "STAmount fail");

        unexpected (! (zeroSt == zeroSt), "STAmount fail");

        unexpected ((zeroSt == one), "STAmount fail");

        unexpected ((zeroSt == hundred), "STAmount fail");

        unexpected ((one == zeroSt), "STAmount fail");

        unexpected (! (one == one), "STAmount fail");

        unexpected ((one == hundred), "STAmount fail");

        unexpected ((hundred == zeroSt), "STAmount fail");

        unexpected ((hundred == one), "STAmount fail");

        unexpected (! (hundred == hundred), "STAmount fail");

        unexpected ((zeroSt != zeroSt), "STAmount fail");

        unexpected (! (zeroSt != one), "STAmount fail");

        unexpected (! (zeroSt != hundred), "STAmount fail");

        unexpected (! (one != zeroSt), "STAmount fail");

        unexpected ((one != one), "STAmount fail");

        unexpected (! (one != hundred), "STAmount fail");

        unexpected (! (hundred != zeroSt), "STAmount fail");

        unexpected (! (hundred != one), "STAmount fail");

        unexpected ((hundred != hundred), "STAmount fail");

        unexpected (STAmount ().getText () != "0", "STAmount fail");

        unexpected (STAmount (31).getText () != "31", "STAmount fail");

        unexpected (STAmount (310).getText () != "310", "STAmount fail");

        unexpected (STAmount::createHumanCurrency (uint160 ()) != "XRP", "cHC(XRP)");

        uint160 c;
        unexpected (!STAmount::currencyFromString (c, "USD"), "create USD currency");
        unexpected (STAmount::createHumanCurrency (c) != "USD", "check USD currency");

        const std::string cur = "015841551A748AD2C1F76FF6ECB0CCCD00000000";
        unexpected (!STAmount::currencyFromString (c, cur), "create custom currency");
        unexpected (STAmount::createHumanCurrency (c) != cur, "check custom currency");
        unexpected (c != uint160 (cur), "check custom currency");
    }

    //--------------------------------------------------------------------------

    void testCustomCurrency ()
    {
        testcase ("custom currency");

        STAmount zeroSt (CURRENCY_ONE, ACCOUNT_ONE), one (CURRENCY_ONE, ACCOUNT_ONE, 1), hundred (CURRENCY_ONE, ACCOUNT_ONE, 100);

        unexpected (serializeAndDeserialize (zeroSt) != zeroSt, "STAmount fail");

        unexpected (serializeAndDeserialize (one) != one, "STAmount fail");

        unexpected (serializeAndDeserialize (hundred) != hundred, "STAmount fail");

        unexpected (zeroSt.isNative (), "STAmount fail");

        unexpected (hundred.isNative (), "STAmount fail");

        unexpected (zeroSt != zero, "STAmount fail");

        unexpected (one == zero, "STAmount fail");

        unexpected (hundred == zero, "STAmount fail");

        unexpected ((zeroSt < zeroSt), "STAmount fail");

        unexpected (! (zeroSt < one), "STAmount fail");

        unexpected (! (zeroSt < hundred), "STAmount fail");

        unexpected ((one < zeroSt), "STAmount fail");

        unexpected ((one < one), "STAmount fail");

        unexpected (! (one < hundred), "STAmount fail");

        unexpected ((hundred < zeroSt), "STAmount fail");

        unexpected ((hundred < one), "STAmount fail");

        unexpected ((hundred < hundred), "STAmount fail");

        unexpected ((zeroSt > zeroSt), "STAmount fail");

        unexpected ((zeroSt > one), "STAmount fail");

        unexpected ((zeroSt > hundred), "STAmount fail");

        unexpected (! (one > zeroSt), "STAmount fail");

        unexpected ((one > one), "STAmount fail");

        unexpected ((one > hundred), "STAmount fail");

        unexpected (! (hundred > zeroSt), "STAmount fail");

        unexpected (! (hundred > one), "STAmount fail");

        unexpected ((hundred > hundred), "STAmount fail");

        unexpected (! (zeroSt <= zeroSt), "STAmount fail");

        unexpected (! (zeroSt <= one), "STAmount fail");

        unexpected (! (zeroSt <= hundred), "STAmount fail");

        unexpected ((one <= zeroSt), "STAmount fail");

        unexpected (! (one <= one), "STAmount fail");

        unexpected (! (one <= hundred), "STAmount fail");

        unexpected ((hundred <= zeroSt), "STAmount fail");

        unexpected ((hundred <= one), "STAmount fail");

        unexpected (! (hundred <= hundred), "STAmount fail");

        unexpected (! (zeroSt >= zeroSt), "STAmount fail");

        unexpected ((zeroSt >= one), "STAmount fail");

        unexpected ((zeroSt >= hundred), "STAmount fail");

        unexpected (! (one >= zeroSt), "STAmount fail");

        unexpected (! (one >= one), "STAmount fail");

        unexpected ((one >= hundred), "STAmount fail");

        unexpected (! (hundred >= zeroSt), "STAmount fail");

        unexpected (! (hundred >= one), "STAmount fail");

        unexpected (! (hundred >= hundred), "STAmount fail");

        unexpected (! (zeroSt == zeroSt), "STAmount fail");

        unexpected ((zeroSt == one), "STAmount fail");

        unexpected ((zeroSt == hundred), "STAmount fail");

        unexpected ((one == zeroSt), "STAmount fail");

        unexpected (! (one == one), "STAmount fail");

        unexpected ((one == hundred), "STAmount fail");

        unexpected ((hundred == zeroSt), "STAmount fail");

        unexpected ((hundred == one), "STAmount fail");

        unexpected (! (hundred == hundred), "STAmount fail");

        unexpected ((zeroSt != zeroSt), "STAmount fail");

        unexpected (! (zeroSt != one), "STAmount fail");

        unexpected (! (zeroSt != hundred), "STAmount fail");

        unexpected (! (one != zeroSt), "STAmount fail");

        unexpected ((one != one), "STAmount fail");

        unexpected (! (one != hundred), "STAmount fail");

        unexpected (! (hundred != zeroSt), "STAmount fail");

        unexpected (! (hundred != one), "STAmount fail");

        unexpected ((hundred != hundred), "STAmount fail");

        unexpected (STAmount (CURRENCY_ONE, ACCOUNT_ONE).getText () != "0", "STAmount fail");

        unexpected (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 31).getText () != "31", "STAmount fail");

        unexpected (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 31, 1).getText () != "310", "STAmount fail");

        unexpected (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 31, -1).getText () != "3.1", "STAmount fail");

        unexpected (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 31, -2).getText () != "0.31", "STAmount fail");

        unexpected (STAmount::multiply (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 20), STAmount (3), CURRENCY_ONE, ACCOUNT_ONE).getText () != "60",
            "STAmount multiply fail 1");

        unexpected (STAmount::multiply (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 20), STAmount (3), uint160 (), ACCOUNT_XRP).getText () != "60",
            "STAmount multiply fail 2");

        unexpected (STAmount::multiply (STAmount (20), STAmount (3), CURRENCY_ONE, ACCOUNT_ONE).getText () != "60",
            "STAmount multiply fail 3");

        unexpected (STAmount::multiply (STAmount (20), STAmount (3), uint160 (), ACCOUNT_XRP).getText () != "60",
            "STAmount multiply fail 4");

        if (STAmount::divide (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 60), STAmount (3), CURRENCY_ONE, ACCOUNT_ONE).getText () != "20")
        {
            WriteLog (lsFATAL, STAmount) << "60/3 = " <<
                                         STAmount::divide (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 60),
                                                 STAmount (3), CURRENCY_ONE, ACCOUNT_ONE).getText ();
            fail ("STAmount divide fail");
        }
        else
        {
            pass ();
        }

        unexpected (STAmount::divide (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 60), STAmount (3), uint160 (), ACCOUNT_XRP).getText () != "20",
            "STAmount divide fail");

        unexpected (STAmount::divide (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 60), STAmount (CURRENCY_ONE, ACCOUNT_ONE, 3), CURRENCY_ONE, ACCOUNT_ONE).getText () != "20",
            "STAmount divide fail");

        unexpected (STAmount::divide (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 60), STAmount (CURRENCY_ONE, ACCOUNT_ONE, 3), uint160 (), ACCOUNT_XRP).getText () != "20",
            "STAmount divide fail");

        STAmount a1 (CURRENCY_ONE, ACCOUNT_ONE, 60), a2 (CURRENCY_ONE, ACCOUNT_ONE, 10, -1);

        unexpected (STAmount::divide (a2, a1, CURRENCY_ONE, ACCOUNT_ONE) != STAmount::setRate (STAmount::getRate (a1, a2)),
            "STAmount setRate(getRate) fail");

        unexpected (STAmount::divide (a1, a2, CURRENCY_ONE, ACCOUNT_ONE) != STAmount::setRate (STAmount::getRate (a2, a1)),
            "STAmount setRate(getRate) fail");
    }

    //--------------------------------------------------------------------------

    void testArithmetic ()
    {
        testcase ("arithmetic");

        CBigNum b;

        for (int i = 0; i < 16; ++i)
        {
            std::uint64_t r = rand ();
            r <<= 32;
            r |= rand ();
            b.setuint64 (r);

            if (b.getuint64 () != r)
            {
                WriteLog (lsFATAL, STAmount) << r << " != " << b.getuint64 () << " " << b.ToString (16);
                fail ("setull64/getull64 failure");
            }
            else
            {
                pass ();
            }
        }

        // Test currency multiplication and division operations such as
        // convertToDisplayAmount, convertToInternalAmount, getRate, getClaimed, and getNeeded

        unexpected (STAmount::getRate (STAmount (1), STAmount (10)) != (((100ull - 14) << (64 - 8)) | 1000000000000000ull),
            "STAmount getRate fail 1");

        unexpected (STAmount::getRate (STAmount (10), STAmount (1)) != (((100ull - 16) << (64 - 8)) | 1000000000000000ull),
            "STAmount getRate fail 2");

        unexpected (STAmount::getRate (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 1), STAmount (CURRENCY_ONE, ACCOUNT_ONE, 10)) != (((100ull - 14) << (64 - 8)) | 1000000000000000ull),
            "STAmount getRate fail 3");

        unexpected (STAmount::getRate (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 10), STAmount (CURRENCY_ONE, ACCOUNT_ONE, 1)) != (((100ull - 16) << (64 - 8)) | 1000000000000000ull),
            "STAmount getRate fail 4");

        unexpected (STAmount::getRate (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 1), STAmount (10)) != (((100ull - 14) << (64 - 8)) | 1000000000000000ull),
            "STAmount getRate fail 5");

        unexpected (STAmount::getRate (STAmount (CURRENCY_ONE, ACCOUNT_ONE, 10), STAmount (1)) != (((100ull - 16) << (64 - 8)) | 1000000000000000ull),
            "STAmount getRate fail 6");

        unexpected (STAmount::getRate (STAmount (1), STAmount (CURRENCY_ONE, ACCOUNT_ONE, 10)) != (((100ull - 14) << (64 - 8)) | 1000000000000000ull),
            "STAmount getRate fail 7");

        unexpected (STAmount::getRate (STAmount (10), STAmount (CURRENCY_ONE, ACCOUNT_ONE, 1)) != (((100ull - 16) << (64 - 8)) | 1000000000000000ull),
            "STAmount getRate fail 8");

        roundTest (1, 3, 3);
        roundTest (2, 3, 9);
        roundTest (1, 7, 21);
        roundTest (1, 2, 4);
        roundTest (3, 9, 18);
        roundTest (7, 11, 44);

        for (int i = 0; i <= 100000; ++i)
            mulTest (rand () % 10000000, rand () % 10000000);
    }

    //--------------------------------------------------------------------------

    template <class Cond>
    bool
    expect (Cond cond, beast::String const& s)
    {
        return suite::expect (cond, s.toStdString());
    }

    template <class Cond>
    bool
    expect (Cond cond)
    {
        return suite::expect (cond);
    }

    void testUnderflow ()
    {
        testcase ("underflow");

        STAmount bigNative (STAmount::cMaxNative / 2);
        STAmount bigValue (CURRENCY_ONE, ACCOUNT_ONE,
                           (STAmount::cMinValue + STAmount::cMaxValue) / 2, STAmount::cMaxOffset - 1);
        STAmount smallValue (CURRENCY_ONE, ACCOUNT_ONE,
                             (STAmount::cMinValue + STAmount::cMaxValue) / 2, STAmount::cMinOffset + 1);
        STAmount zeroSt (CURRENCY_ONE, ACCOUNT_ONE, 0);

        STAmount smallXsmall = STAmount::multiply (smallValue, smallValue, CURRENCY_ONE, ACCOUNT_ONE);

        expect (smallXsmall == zero, "smallXsmall != 0");

        STAmount bigDsmall = STAmount::divide (smallValue, bigValue, CURRENCY_ONE, ACCOUNT_ONE);

        expect (bigDsmall == zero, beast::String ("small/big != 0: ") + bigDsmall.getText ());

        bigDsmall = STAmount::divide (smallValue, bigNative, CURRENCY_ONE, uint160 ());

        expect (bigDsmall == zero, beast::String ("small/bigNative != 0: ") + bigDsmall.getText ());

        bigDsmall = STAmount::divide (smallValue, bigValue, uint160 (), uint160 ());

        expect (bigDsmall == zero, beast::String ("(small/big)->N != 0: ") + bigDsmall.getText ());

        bigDsmall = STAmount::divide (smallValue, bigNative, uint160 (), uint160 ());

        expect (bigDsmall == zero, beast::String ("(small/bigNative)->N != 0: ") + bigDsmall.getText ());

        // very bad offer
        std::uint64_t r = STAmount::getRate (smallValue, bigValue);

        expect (r == 0, "getRate(smallOut/bigIn) != 0");

        // very good offer
        r = STAmount::getRate (bigValue, smallValue);

        expect (r == 0, "getRate(smallIn/bigOUt) != 0");
    }

    //--------------------------------------------------------------------------

    void testRounding ()
    {
        // VFALCO TODO There are no actual tests here, just printed output?
        //             Change this to actually do something.

#if 0
        beginTestCase ("rounding ");

        std::uint64_t value = 25000000000000000ull;
        int offset = -14;
        STAmount::canonicalizeRound (false, value, offset, true);

        STAmount one (CURRENCY_ONE, ACCOUNT_ONE, 1);
        STAmount two (CURRENCY_ONE, ACCOUNT_ONE, 2);
        STAmount three (CURRENCY_ONE, ACCOUNT_ONE, 3);

        STAmount oneThird1 = STAmount::divRound (one, three, CURRENCY_ONE, ACCOUNT_ONE, false);
        STAmount oneThird2 = STAmount::divide (one, three, CURRENCY_ONE, ACCOUNT_ONE);
        STAmount oneThird3 = STAmount::divRound (one, three, CURRENCY_ONE, ACCOUNT_ONE, true);
        WriteLog (lsINFO, STAmount) << oneThird1;
        WriteLog (lsINFO, STAmount) << oneThird2;
        WriteLog (lsINFO, STAmount) << oneThird3;

        STAmount twoThird1 = STAmount::divRound (two, three, CURRENCY_ONE, ACCOUNT_ONE, false);
        STAmount twoThird2 = STAmount::divide (two, three, CURRENCY_ONE, ACCOUNT_ONE);
        STAmount twoThird3 = STAmount::divRound (two, three, CURRENCY_ONE, ACCOUNT_ONE, true);
        WriteLog (lsINFO, STAmount) << twoThird1;
        WriteLog (lsINFO, STAmount) << twoThird2;
        WriteLog (lsINFO, STAmount) << twoThird3;

        STAmount oneA = STAmount::mulRound (oneThird1, three, CURRENCY_ONE, ACCOUNT_ONE, false);
        STAmount oneB = STAmount::multiply (oneThird2, three, CURRENCY_ONE, ACCOUNT_ONE);
        STAmount oneC = STAmount::mulRound (oneThird3, three, CURRENCY_ONE, ACCOUNT_ONE, true);
        WriteLog (lsINFO, STAmount) << oneA;
        WriteLog (lsINFO, STAmount) << oneB;
        WriteLog (lsINFO, STAmount) << oneC;

        STAmount fourThirdsA = STAmount::addRound (twoThird2, twoThird2, false);
        STAmount fourThirdsB = twoThird2 + twoThird2;
        STAmount fourThirdsC = STAmount::addRound (twoThird2, twoThird2, true);
        WriteLog (lsINFO, STAmount) << fourThirdsA;
        WriteLog (lsINFO, STAmount) << fourThirdsB;
        WriteLog (lsINFO, STAmount) << fourThirdsC;

        STAmount dripTest1 = STAmount::mulRound (twoThird2, two, uint160 (), uint160 (), false);
        STAmount dripTest2 = STAmount::multiply (twoThird2, two, uint160 (), uint160 ());
        STAmount dripTest3 = STAmount::mulRound (twoThird2, two, uint160 (), uint160 (), true);
        WriteLog (lsINFO, STAmount) << dripTest1;
        WriteLog (lsINFO, STAmount) << dripTest2;
        WriteLog (lsINFO, STAmount) << dripTest3;
#endif
    }

    //--------------------------------------------------------------------------

    void run ()
    {
        testSetValue ();
        testNativeCurrency ();
        testCustomCurrency ();
        testArithmetic ();
        testUnderflow ();
        testRounding ();
    }
};

BEAST_DEFINE_TESTSUITE(STAmount,ripple_data,ripple);

} // ripple
