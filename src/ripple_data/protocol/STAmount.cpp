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

SETUP_LOG (STAmount)

uint64  STAmount::uRateOne  = STAmount::getRate (STAmount (1), STAmount (1));

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
        WriteLog (lsINFO, STAmount)
                << boost::str (boost::format ("bSetJson(): caught: %s")
                               % e.what ());

        return false;
    }
}

STAmount::STAmount (SField::ref n, const Json::Value& v)
    : SerializedType (n), mValue (0), mOffset (0), mIsNegative (false)
{
    Json::Value value, currency, issuer;

    if (v.isObject ())
    {
        WriteLog (lsTRACE, STAmount)
                << boost::str (boost::format ("value='%s', currency='%s', issuer='%s'")
                               % v["value"].asString ()
                               % v["currency"].asString ()
                               % v["issuer"].asString ());

        value       = v["value"];
        currency    = v["currency"];
        issuer      = v["issuer"];
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
            int64 val = lexicalCastThrow <int64> (value.asString ());

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
    std::string sCurrency;
    static uint160 sFiatBits("FFFFFFFFFFFFFFFFFFFFFFFF0000000000000000");

    if (uCurrency.isZero ())
    {
        return SYSTEM_CURRENCY_CODE;
    }
    else if (CURRENCY_ONE == uCurrency)
    {
        return "1";
    }
    else if (CURRENCY_BAD == uCurrency)
    {
        return uCurrency.ToString ();
    }
    else if ((uCurrency & sFiatBits).isZero ())
    {
        Serializer  s (160 / 8);

        s.add160 (uCurrency);

        SerializerIterator  sit (s);

        Blob    vucZeros    = sit.getRaw (96 / 8);
        Blob    vucIso      = sit.getRaw (24 / 8);
        Blob    vucVersion  = sit.getRaw (16 / 8);
        Blob    vucReserved = sit.getRaw (24 / 8);

        bool    bIso    =    isZeroFilled (vucZeros.begin (), vucZeros.size ())            // Leading zeros
                          && isZeroFilled (vucVersion.begin (), vucVersion.size ())   // Zero version
                          && isZeroFilled (vucReserved.begin (), vucReserved.size ()); // Reserved is zero.

        if (bIso)
        {
            sCurrency.assign (vucIso.begin (), vucIso.end ());
        }
        else
        {
            sCurrency   = uCurrency.ToString ();
        }
    }
    else
       sCurrency = uCurrency.GetHex ();

    return sCurrency;
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
            mValue = lexicalCast <uint64> (std::string (smMatch[2]));
            mOffset = 0;
        }
        else
        {
            // integer and fraction
            mValue = lexicalCast <uint64> (smMatch[2] + smMatch[4]);
            mOffset = - (smMatch[4].length ());
        }

        if (smMatch[5].matched)
        {
            // we have an exponent
            if (smMatch[6].matched && (smMatch[6] == "-"))
                mOffset -= lexicalCast <int> (std::string (smMatch[7]));
            else
                mOffset += lexicalCast <int> (std::string (smMatch[7]));
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
        if (isZero ())
            s.add64 (cNotNative);
        else if (mIsNegative) // 512 = not native
            s.add64 (mValue | (static_cast<uint64> (mOffset + 512 + 97) << (64 - 10)));
        else // 256 = positive
            s.add64 (mValue | (static_cast<uint64> (mOffset + 512 + 256 + 97) << (64 - 10)));

        s.add160 (mCurrency);
        s.add160 (mIssuer);
    }
}

STAmount STAmount::createFromInt64 (SField::ref name, int64 value)
{
    return value >= 0
           ? STAmount (name, static_cast<uint64> (value), false)
           : STAmount (name, static_cast<uint64> (-value), true);
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
    uint64 value = sit.get64 ();

    if ((value & cNotNative) == 0)
    {
        // native
        if ((value & cPosNative) != 0)
            return new STAmount (name, value & ~cPosNative, false); // positive
        else if (value == 0)
            throw std::runtime_error ("negative zero is not canonical");

        return new STAmount (name, value, true); // negative
    }

    uint160 uCurrencyID = sit.get160 ();

    if (!uCurrencyID)
        throw std::runtime_error ("invalid non-native currency");

    uint160 uIssuerID = sit.get160 ();

    int offset = static_cast<int> (value >> (64 - 10)); // 10 bits for the offset, sign and "not native" flag
    value &= ~ (1023ull << (64 - 10));

    if (value)
    {
        bool isNegative = (offset & 256) == 0;
        offset = (offset & 255) - 97; // center the range

        if ((value < cMinValue) || (value > cMaxValue) || (offset < cMinOffset) || (offset > cMaxOffset))
            throw std::runtime_error ("invalid currency value");

        return new STAmount (name, uCurrencyID, uIssuerID, value, offset, isNegative);
    }

    if (offset != 512)
        throw std::runtime_error ("invalid currency value");

    return new STAmount (name, uCurrencyID, uIssuerID);
}

int64 STAmount::getSNValue () const
{
    // signed native value
    if (!mIsNative) throw std::runtime_error ("not native");

    if (mIsNegative) return - static_cast<int64> (mValue);

    return static_cast<int64> (mValue);
}

void STAmount::setSNValue (int64 v)
{
    if (!mIsNative) throw std::runtime_error ("not native");

    if (v > 0)
    {
        mIsNegative = false;
        mValue = static_cast<uint64> (v);
    }
    else
    {
        mIsNegative = true;
        mValue = static_cast<uint64> (-v);
    }
}

std::string STAmount::getRaw () const
{
    // show raw internal form
    if (mValue == 0) return "0";

    if (mIsNative)
    {
        if (mIsNegative) return std::string ("-") + lexicalCast <std::string> (mValue);
        else return lexicalCast <std::string> (mValue);
    }

    if (mIsNegative)
        return mCurrency.GetHex () + ": -" +
               lexicalCast <std::string> (mValue) + "e" + lexicalCast <std::string> (mOffset);
    else return mCurrency.GetHex () + ": " +
                    lexicalCast <std::string> (mValue) + "e" + lexicalCast <std::string> (mOffset);
}

std::string STAmount::getText () const
{
    // keep full internal accuracy, but make more human friendly if posible
    if (isZero ()) return "0";

    if (mIsNative)
    {
        if (mIsNegative)
            return std::string ("-") +  lexicalCast <std::string> (mValue);
        else return lexicalCast <std::string> (mValue);
    }

    if ((mOffset != 0) && ((mOffset < -25) || (mOffset > -5)))
    {
        if (mIsNegative)
            return std::string ("-") + lexicalCast <std::string> (mValue) +
                   "e" + lexicalCast <std::string> (mOffset);
        else
            return lexicalCast <std::string> (mValue) + "e" + lexicalCast <std::string> (mOffset);
    }

    std::string val = "000000000000000000000000000";
    val += lexicalCast <std::string> (mValue);
    val += "00000000000000000000000";

    std::string pre = val.substr (0, mOffset + 43);
    std::string post = val.substr (mOffset + 43);

    size_t s_pre = pre.find_first_not_of ('0');

    if (s_pre == std::string::npos)
        pre = "0";
    else
        pre = pre.substr (s_pre);

    size_t s_post = post.find_last_not_of ('0');

    if (mIsNegative) pre = std::string ("-") + pre;

    if (s_post == std::string::npos)
        return pre;
    else
        return pre + "." + post.substr (0, s_post + 1);
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

STAmount& STAmount::operator= (uint64 v)
{
    // does not copy name, does not change currency type
    mOffset = 0;
    mValue = v;
    mIsNegative = false;

    if (!mIsNative) canonicalize ();

    return *this;
}

STAmount& STAmount::operator+= (uint64 v)
{
    if (mIsNative)
        setSNValue (getSNValue () + static_cast<int64> (v));
    else *this += STAmount (mCurrency, v);

    return *this;
}

STAmount& STAmount::operator-= (uint64 v)
{
    if (mIsNative)
        setSNValue (getSNValue () - static_cast<int64> (v));
    else *this -= STAmount (mCurrency, v);

    return *this;
}

bool STAmount::operator< (uint64 v) const
{
    return getSNValue () < static_cast<int64> (v);
}

bool STAmount::operator> (uint64 v) const
{
    return getSNValue () > static_cast<int64> (v);
}

bool STAmount::operator<= (uint64 v) const
{
    return getSNValue () <= static_cast<int64> (v);
}

bool STAmount::operator>= (uint64 v) const
{
    return getSNValue () >= static_cast<int64> (v);
}

STAmount STAmount::operator+ (uint64 v) const
{
    return STAmount (getFName (), getSNValue () + static_cast<int64> (v));
}

STAmount STAmount::operator- (uint64 v) const
{
    return STAmount (getFName (), getSNValue () - static_cast<int64> (v));
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

    if (v2.isZero ()) return v1;

    if (v1.isZero ())
    {
        // Result must be in terms of v1 currency and issuer.
        return STAmount (v1.getFName (), v1.mCurrency, v1.mIssuer, v2.mValue, v2.mOffset, v2.mIsNegative);
    }

    if (v1.mIsNative)
        return STAmount (v1.getFName (), v1.getSNValue () + v2.getSNValue ());

    int ov1 = v1.mOffset, ov2 = v2.mOffset;
    int64 vv1 = static_cast<int64> (v1.mValue), vv2 = static_cast<int64> (v2.mValue);

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

    // this addition cannot overflow an int64, it can overflow an STAmount and the constructor will throw

    int64 fv = vv1 + vv2;

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

    if (v2.isZero ()) return v1;

    if (v2.mIsNative)
    {
        // XXX This could be better, check for overflow and that maximum range is covered.
        return STAmount::createFromInt64 (v1.getFName (), v1.getSNValue () - v2.getSNValue ());
    }

    int ov1 = v1.mOffset, ov2 = v2.mOffset;
    int64 vv1 = static_cast<int64> (v1.mValue), vv2 = static_cast<int64> (v2.mValue);

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

    // this subtraction cannot overflow an int64, it can overflow an STAmount and the constructor will throw

    int64 fv = vv1 - vv2;

    if ((fv >= -10) && (fv <= 10))
        return STAmount (v1.getFName (), v1.mCurrency, v1.mIssuer);
    else if (fv >= 0)
        return STAmount (v1.getFName (), v1.mCurrency, v1.mIssuer, fv, ov1, false);
    else
        return STAmount (v1.getFName (), v1.mCurrency, v1.mIssuer, -fv, ov1, true);
}

STAmount STAmount::divide (const STAmount& num, const STAmount& den, const uint160& uCurrencyID, const uint160& uIssuerID)
{
    if (den.isZero ())
        throw std::runtime_error ("division by zero");

    if (num.isZero ())
        return STAmount (uCurrencyID, uIssuerID);

    uint64 numVal = num.mValue, denVal = den.mValue;
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
            (BN_div_word64 (&v, denVal) == ((uint64) - 1)))
    {
        throw std::runtime_error ("internal bn error");
    }

    // 10^16 <= quotient <= 10^18
    assert (BN_num_bytes (&v) <= 64);

    return STAmount (uCurrencyID, uIssuerID, v.getuint64 () + 5,
                     numOffset - denOffset - 17, num.mIsNegative != den.mIsNegative);
}

STAmount STAmount::multiply (const STAmount& v1, const STAmount& v2, const uint160& uCurrencyID, const uint160& uIssuerID)
{
    if (v1.isZero () || v2.isZero ())
        return STAmount (uCurrencyID, uIssuerID);

    if (v1.mIsNative && v2.mIsNative && uCurrencyID.isZero ())
    {
        uint64 minV = (v1.getSNValue () < v2.getSNValue ()) ? v1.getSNValue () : v2.getSNValue ();
        uint64 maxV = (v1.getSNValue () < v2.getSNValue ()) ? v2.getSNValue () : v1.getSNValue ();

        if (minV > 3000000000ull) // sqrt(cMaxNative)
            throw std::runtime_error ("Native value overflow");

        if (((maxV >> 32) * minV) > 2095475792ull) // cMaxNative / 2^32
            throw std::runtime_error ("Native value overflow");

        return STAmount (v1.getFName (), minV * maxV);
    }

    uint64 value1 = v1.mValue, value2 = v2.mValue;
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
            (BN_div_word64 (&v, tenTo14) == ((uint64) - 1)))
    {
        throw std::runtime_error ("internal bn error");
    }

    // 10^16 <= product <= 10^18
    assert (BN_num_bytes (&v) <= 64);

    return STAmount (uCurrencyID, uIssuerID, v.getuint64 () + 7, offset1 + offset2 + 14,
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
uint64 STAmount::getRate (const STAmount& offerOut, const STAmount& offerIn)
{
    if (offerOut.isZero ())
        return 0;

    try
    {
        STAmount r = divide (offerIn, offerOut, CURRENCY_ONE, ACCOUNT_ONE);

        if (r.isZero ()) // offer is too good
            return 0;

        assert ((r.getExponent () >= -100) && (r.getExponent () <= 155));

        uint64 ret = r.getExponent () + 100;

        return (ret << (64 - 8)) | r.getMantissa ();
    }
    catch (...)
    {
        // overflow -- very bad offer
        return 0;
    }
}

STAmount STAmount::setRate (uint64 rate)
{
    if (rate == 0)
        return STAmount (CURRENCY_ONE, ACCOUNT_ONE);

    uint64 mantissa = rate & ~ (255ull << (64 - 8));
    int exponent = static_cast<int> (rate >> (64 - 8)) - 100;

    return STAmount (CURRENCY_ONE, ACCOUNT_ONE, mantissa, exponent);
}

// Existing offer is on the books.
// Price is offer owner's, which might be better for taker.
// Taker pays what they can.
// Taker gets all taker can pay for with saTakerFunds/uTakerPaysRate, limited by saOfferPays and saOfferFunds/uOfferPaysRate.
// If taker is an offer, taker is spending at same or better rate than they wanted.
// Taker should consider themselves as wanting to buy X amount.
// Taker is willing to pay at most the rate of Y/X each.
// Buy semantics:
// - After having some part of their offer fulfilled at a better rate their offer should be reduced accordingly.
//
// There are no quality costs for offer vs offer taking.
//
// -->            bSell: True for sell semantics.
// -->   uTakerPaysRate: >= QUALITY_ONE | TransferRate for third party IOUs paid by taker.
// -->   uOfferPaysRate: >= QUALITY_ONE | TransferRate for third party IOUs paid by offer owner.
// -->      saOfferRate: Original saOfferGets/saOfferPays, when offer was made.
// -->     saOfferFunds: Limit for saOfferPays : How much can pay including fees.
// -->     saTakerFunds: Limit for saOfferGets : How much can pay including fees.
// -->      saOfferPays: Request : this should be reduced as the offer is fullfilled.
// -->      saOfferGets: Request : this should be reduced as the offer is fullfilled.
// -->      saTakerPays: Limit for taker to pay.
// -->      saTakerGets: Limit for taker to get.
// <--      saTakerPaid: Actual
// <--       saTakerGot: Actual
// <-- saTakerIssuerFee: Actual
// <-- saOfferIssuerFee: Actual
bool STAmount::applyOffer (
    const bool bSell,
    const uint32 uTakerPaysRate, const uint32 uOfferPaysRate,
    const STAmount& saOfferRate,
    const STAmount& saOfferFunds, const STAmount& saTakerFunds,
    const STAmount& saOfferPays, const STAmount& saOfferGets,
    const STAmount& saTakerPays, const STAmount& saTakerGets,
    STAmount& saTakerPaid, STAmount& saTakerGot,
    STAmount& saTakerIssuerFee, STAmount& saOfferIssuerFee)
{
    saOfferGets.throwComparable (saTakerFunds);

    assert (saOfferFunds.isPositive () && saTakerFunds.isPositive ()); // Both must have funds.
    assert (saOfferGets.isPositive () && saOfferPays.isPositive ()); // Must not be a null offer.

    // Available = limited by funds.
    // Limit offerer funds available, by transfer fees.
    STAmount    saOfferFundsAvailable   = QUALITY_ONE == uOfferPaysRate
                                          ? saOfferFunds          // As is.
                                          : STAmount::divide (saOfferFunds, STAmount (CURRENCY_ONE, ACCOUNT_ONE, uOfferPaysRate, -9)); // Reduce by offer fees.

    WriteLog (lsINFO, STAmount) << "applyOffer: uOfferPaysRate=" << uOfferPaysRate;
    WriteLog (lsINFO, STAmount) << "applyOffer: saOfferFundsAvailable=" << saOfferFundsAvailable.getFullText ();

    // Limit taker funds available, by transfer fees.
    STAmount    saTakerFundsAvailable   = QUALITY_ONE == uTakerPaysRate
                                          ? saTakerFunds          // As is.
                                          : STAmount::divide (saTakerFunds, STAmount (CURRENCY_ONE, ACCOUNT_ONE, uTakerPaysRate, -9)); // Reduce by taker fees.

    WriteLog (lsINFO, STAmount) << "applyOffer: TAKER_FEES=" << STAmount (CURRENCY_ONE, ACCOUNT_ONE, uTakerPaysRate, -9).getFullText ();
    WriteLog (lsINFO, STAmount) << "applyOffer: uTakerPaysRate=" << uTakerPaysRate;
    WriteLog (lsINFO, STAmount) << "applyOffer: saTakerFundsAvailable=" << saTakerFundsAvailable.getFullText ();

    STAmount    saOfferPaysAvailable;   // Amount offer can pay out, limited by offer and offerer funds.
    STAmount    saOfferGetsAvailable;   // Amount offer would get, limited by offer funds.

    if (saOfferFundsAvailable >= saOfferPays)
    {
        // Offer was fully funded, avoid math shenanigans.

        saOfferPaysAvailable    = saOfferPays;
        saOfferGetsAvailable    = saOfferGets;
    }
    else
    {
        // Offer has limited funding, limit offer gets and pays by funds available.

        saOfferPaysAvailable    = saOfferFundsAvailable;
        saOfferGetsAvailable    = std::min (saOfferGets, mulRound (saOfferPaysAvailable, saOfferRate, saOfferGets, true));
    }

    WriteLog (lsINFO, STAmount) << "applyOffer: saOfferPaysAvailable=" << saOfferPaysAvailable.getFullText ();
    WriteLog (lsINFO, STAmount) << "applyOffer: saOfferGetsAvailable=" << saOfferGetsAvailable.getFullText ();

    STAmount    saTakerPaysAvailable    = std::min (saTakerPays, saTakerFundsAvailable);
    WriteLog (lsINFO, STAmount) << "applyOffer: saTakerPaysAvailable=" << saTakerPaysAvailable.getFullText ();

    // Limited = limited by other sides raw numbers.
    // Taker can't pay more to offer than offer can get.
    STAmount    saTakerPaysLimited      = std::min (saTakerPaysAvailable, saOfferGetsAvailable);
    WriteLog (lsINFO, STAmount) << "applyOffer: saTakerPaysLimited=" << saTakerPaysLimited.getFullText ();

    // Align saTakerGetsLimited with saTakerPaysLimited.
    STAmount    saTakerGetsLimited      = saTakerPaysLimited >= saOfferGetsAvailable              // Cannot actually be greater
                                          ? saOfferPaysAvailable                                  // Potentially take entire offer. Avoid math shenanigans.
                                          : std::min (saOfferPaysAvailable, divRound (saTakerPaysLimited, saOfferRate, saTakerGets, true)); // Take a portion of offer.

    WriteLog (lsINFO, STAmount) << "applyOffer: saOfferRate=" << saOfferRate.getFullText ();
    WriteLog (lsINFO, STAmount) << "applyOffer: saTakerGetsLimited=" << saTakerGetsLimited.getFullText ();

    // Got & Paid = Calculated by price and transfered without fees.
    // Compute from got as when !bSell, we want got to be exact to finish off offer if possible.

    saTakerGot  = bSell
                  ? saTakerGetsLimited                            // Get all available that are paid for.
                  : std::min (saTakerGets, saTakerGetsLimited);   // Limit by wanted.
    saTakerPaid = saTakerGot >= saTakerGetsLimited
                  ? saTakerPaysLimited
                  : std::min (saTakerPaysLimited, mulRound (saTakerGot, saOfferRate, saTakerFunds, true));

    WriteLog (lsINFO, STAmount) << "applyOffer: saTakerGot=" << saTakerGot.getFullText ();
    WriteLog (lsINFO, STAmount) << "applyOffer: saTakerPaid=" << saTakerPaid.getFullText ();

    if (uTakerPaysRate == QUALITY_ONE)
    {
        saTakerIssuerFee    = STAmount (saTakerPaid.getCurrency (), saTakerPaid.getIssuer ());
    }
    else
    {
        // Compute fees in a rounding safe way.

        STAmount    saTransferRate  = STAmount (CURRENCY_ONE, ACCOUNT_ONE, uTakerPaysRate, -9);
        WriteLog (lsINFO, STAmount) << "applyOffer: saTransferRate=" << saTransferRate.getFullText ();

        // TakerCost includes transfer fees.
        STAmount    saTakerCost     = STAmount::mulRound (saTakerPaid, saTransferRate, true);

        WriteLog (lsINFO, STAmount) << "applyOffer: saTakerCost=" << saTakerCost.getFullText ();
        WriteLog (lsINFO, STAmount) << "applyOffer: saTakerFunds=" << saTakerFunds.getFullText ();
        saTakerIssuerFee    = saTakerCost > saTakerFunds
                              ? saTakerFunds - saTakerPaid // Not enough funds to cover fee, stiff issuer the rounding error.
                              : saTakerCost - saTakerPaid;
        WriteLog (lsINFO, STAmount) << "applyOffer: saTakerIssuerFee=" << saTakerIssuerFee.getFullText ();
        assert (!saTakerIssuerFee.isNegative ());
    }

    if (uOfferPaysRate == QUALITY_ONE)
    {
        saOfferIssuerFee    = STAmount (saTakerGot.getCurrency (), saTakerGot.getIssuer ());
    }
    else
    {
        // Compute fees in a rounding safe way.
        STAmount    saOfferCost = STAmount::mulRound (saTakerGot, STAmount (CURRENCY_ONE, ACCOUNT_ONE, uOfferPaysRate, -9), true);

        saOfferIssuerFee    = saOfferCost > saOfferFunds
                              ? saOfferFunds - saTakerGot // Not enough funds to cover fee, stiff issuer the rounding error.
                              : saOfferCost - saTakerGot;
    }

    WriteLog (lsINFO, STAmount) << "applyOffer: saTakerGot=" << saTakerGot.getFullText ();

    return saTakerGot >= saOfferPaysAvailable;              // True, if consumed offer.
}

STAmount STAmount::getPay (const STAmount& offerOut, const STAmount& offerIn, const STAmount& needed)
{
    // Someone wants to get (needed) out of the offer, how much should they pay in?
    if (offerOut.isZero ())
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
    static const boost::format nativeFormat ("%s/" SYSTEM_CURRENCY_CODE);
    static const boost::format noIssuer ("%s/%s/0");
    static const boost::format issuerOne ("%s/%s/1");
    static const boost::format normal ("%s/%s/%s");

    if (mIsNative)
    {
        return str (boost::format (nativeFormat) % getText ());
    }
    else if (!mIssuer)
    {
        return str (boost::format (noIssuer) % getText () % getHumanCurrency ());
    }
    else if (mIssuer == ACCOUNT_ONE)
    {
        return str (boost::format (issuerOne) % getText () % getHumanCurrency ());
    }
    else
    {
        return str (boost::format (normal)
                    % getText ()
                    % getHumanCurrency ()
                    % RippleAddress::createHumanAccountID (mIssuer));
    }
}

STAmount STAmount::getRound () const
{
    if (mIsNative)
        return *this;

    uint64 valueDigits = mValue % 1000000000ull;

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

    uint64 valueDigits = mValue % 1000000000ull;

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

        elem["value"]       = getText ();
        elem["currency"]    = getHumanCurrency ();
        elem["issuer"]      = RippleAddress::createHumanAccountID (mIssuer);
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

class STAmountTests : public UnitTest
{
public:
    STAmountTests () : UnitTest ("STAmount", "ripple")
    {
    }

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

        STAmount prod2 (CURRENCY_ONE, ACCOUNT_ONE, static_cast<uint64> (a) * static_cast<uint64> (b));

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
        beginTestCase ("set value");

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
        beginTestCase ("native currency");

        STAmount zero, one (1), hundred (100);

        unexpected (serializeAndDeserialize (zero) != zero, "STAmount fail");

        unexpected (serializeAndDeserialize (one) != one, "STAmount fail");

        unexpected (serializeAndDeserialize (hundred) != hundred, "STAmount fail");

        unexpected (!zero.isNative (), "STAmount fail");

        unexpected (!hundred.isNative (), "STAmount fail");

        unexpected (!zero.isZero (), "STAmount fail");

        unexpected (one.isZero (), "STAmount fail");

        unexpected (hundred.isZero (), "STAmount fail");

        unexpected ((zero < zero), "STAmount fail");

        unexpected (! (zero < one), "STAmount fail");

        unexpected (! (zero < hundred), "STAmount fail");

        unexpected ((one < zero), "STAmount fail");

        unexpected ((one < one), "STAmount fail");

        unexpected (! (one < hundred), "STAmount fail");

        unexpected ((hundred < zero), "STAmount fail");

        unexpected ((hundred < one), "STAmount fail");

        unexpected ((hundred < hundred), "STAmount fail");

        unexpected ((zero > zero), "STAmount fail");

        unexpected ((zero > one), "STAmount fail");

        unexpected ((zero > hundred), "STAmount fail");

        unexpected (! (one > zero), "STAmount fail");

        unexpected ((one > one), "STAmount fail");

        unexpected ((one > hundred), "STAmount fail");

        unexpected (! (hundred > zero), "STAmount fail");

        unexpected (! (hundred > one), "STAmount fail");

        unexpected ((hundred > hundred), "STAmount fail");

        unexpected (! (zero <= zero), "STAmount fail");

        unexpected (! (zero <= one), "STAmount fail");

        unexpected (! (zero <= hundred), "STAmount fail");

        unexpected ((one <= zero), "STAmount fail");

        unexpected (! (one <= one), "STAmount fail");

        unexpected (! (one <= hundred), "STAmount fail");

        unexpected ((hundred <= zero), "STAmount fail");

        unexpected ((hundred <= one), "STAmount fail");

        unexpected (! (hundred <= hundred), "STAmount fail");

        unexpected (! (zero >= zero), "STAmount fail");

        unexpected ((zero >= one), "STAmount fail");

        unexpected ((zero >= hundred), "STAmount fail");

        unexpected (! (one >= zero), "STAmount fail");

        unexpected (! (one >= one), "STAmount fail");

        unexpected ((one >= hundred), "STAmount fail");

        unexpected (! (hundred >= zero), "STAmount fail");

        unexpected (! (hundred >= one), "STAmount fail");

        unexpected (! (hundred >= hundred), "STAmount fail");

        unexpected (! (zero == zero), "STAmount fail");

        unexpected ((zero == one), "STAmount fail");

        unexpected ((zero == hundred), "STAmount fail");

        unexpected ((one == zero), "STAmount fail");

        unexpected (! (one == one), "STAmount fail");

        unexpected ((one == hundred), "STAmount fail");

        unexpected ((hundred == zero), "STAmount fail");

        unexpected ((hundred == one), "STAmount fail");

        unexpected (! (hundred == hundred), "STAmount fail");

        unexpected ((zero != zero), "STAmount fail");

        unexpected (! (zero != one), "STAmount fail");

        unexpected (! (zero != hundred), "STAmount fail");

        unexpected (! (one != zero), "STAmount fail");

        unexpected ((one != one), "STAmount fail");

        unexpected (! (one != hundred), "STAmount fail");

        unexpected (! (hundred != zero), "STAmount fail");

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
        beginTestCase ("custom currency");

        STAmount zero (CURRENCY_ONE, ACCOUNT_ONE), one (CURRENCY_ONE, ACCOUNT_ONE, 1), hundred (CURRENCY_ONE, ACCOUNT_ONE, 100);

        serializeAndDeserialize (one).getRaw ();

        unexpected (serializeAndDeserialize (zero) != zero, "STAmount fail");

        unexpected (serializeAndDeserialize (one) != one, "STAmount fail");

        unexpected (serializeAndDeserialize (hundred) != hundred, "STAmount fail");

        unexpected (zero.isNative (), "STAmount fail");

        unexpected (hundred.isNative (), "STAmount fail");

        unexpected (!zero.isZero (), "STAmount fail");

        unexpected (one.isZero (), "STAmount fail");

        unexpected (hundred.isZero (), "STAmount fail");

        unexpected ((zero < zero), "STAmount fail");

        unexpected (! (zero < one), "STAmount fail");

        unexpected (! (zero < hundred), "STAmount fail");

        unexpected ((one < zero), "STAmount fail");

        unexpected ((one < one), "STAmount fail");

        unexpected (! (one < hundred), "STAmount fail");

        unexpected ((hundred < zero), "STAmount fail");

        unexpected ((hundred < one), "STAmount fail");

        unexpected ((hundred < hundred), "STAmount fail");

        unexpected ((zero > zero), "STAmount fail");

        unexpected ((zero > one), "STAmount fail");

        unexpected ((zero > hundred), "STAmount fail");

        unexpected (! (one > zero), "STAmount fail");

        unexpected ((one > one), "STAmount fail");

        unexpected ((one > hundred), "STAmount fail");

        unexpected (! (hundred > zero), "STAmount fail");

        unexpected (! (hundred > one), "STAmount fail");

        unexpected ((hundred > hundred), "STAmount fail");

        unexpected (! (zero <= zero), "STAmount fail");

        unexpected (! (zero <= one), "STAmount fail");

        unexpected (! (zero <= hundred), "STAmount fail");

        unexpected ((one <= zero), "STAmount fail");

        unexpected (! (one <= one), "STAmount fail");

        unexpected (! (one <= hundred), "STAmount fail");

        unexpected ((hundred <= zero), "STAmount fail");

        unexpected ((hundred <= one), "STAmount fail");

        unexpected (! (hundred <= hundred), "STAmount fail");

        unexpected (! (zero >= zero), "STAmount fail");

        unexpected ((zero >= one), "STAmount fail");

        unexpected ((zero >= hundred), "STAmount fail");

        unexpected (! (one >= zero), "STAmount fail");

        unexpected (! (one >= one), "STAmount fail");

        unexpected ((one >= hundred), "STAmount fail");

        unexpected (! (hundred >= zero), "STAmount fail");

        unexpected (! (hundred >= one), "STAmount fail");

        unexpected (! (hundred >= hundred), "STAmount fail");

        unexpected (! (zero == zero), "STAmount fail");

        unexpected ((zero == one), "STAmount fail");

        unexpected ((zero == hundred), "STAmount fail");

        unexpected ((one == zero), "STAmount fail");

        unexpected (! (one == one), "STAmount fail");

        unexpected ((one == hundred), "STAmount fail");

        unexpected ((hundred == zero), "STAmount fail");

        unexpected ((hundred == one), "STAmount fail");

        unexpected (! (hundred == hundred), "STAmount fail");

        unexpected ((zero != zero), "STAmount fail");

        unexpected (! (zero != one), "STAmount fail");

        unexpected (! (zero != hundred), "STAmount fail");

        unexpected (! (one != zero), "STAmount fail");

        unexpected ((one != one), "STAmount fail");

        unexpected (! (one != hundred), "STAmount fail");

        unexpected (! (hundred != zero), "STAmount fail");

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
        beginTestCase ("arithmetic");

        CBigNum b;

        for (int i = 0; i < 16; ++i)
        {
            uint64 r = rand ();
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

    void testUnderflow ()
    {
        beginTestCase ("underflow");

        STAmount bigNative (STAmount::cMaxNative / 2);
        STAmount bigValue (CURRENCY_ONE, ACCOUNT_ONE,
                           (STAmount::cMinValue + STAmount::cMaxValue) / 2, STAmount::cMaxOffset - 1);
        STAmount smallValue (CURRENCY_ONE, ACCOUNT_ONE,
                             (STAmount::cMinValue + STAmount::cMaxValue) / 2, STAmount::cMinOffset + 1);
        STAmount zero (CURRENCY_ONE, ACCOUNT_ONE, 0);

        STAmount smallXsmall = STAmount::multiply (smallValue, smallValue, CURRENCY_ONE, ACCOUNT_ONE);

        expect (smallXsmall.isZero (), "smallXsmall != 0");

        STAmount bigDsmall = STAmount::divide (smallValue, bigValue, CURRENCY_ONE, ACCOUNT_ONE);

        expect (bigDsmall.isZero (), String ("small/big != 0: ") + bigDsmall.getText ());

        bigDsmall = STAmount::divide (smallValue, bigNative, CURRENCY_ONE, uint160 ());

        expect (bigDsmall.isZero (), String ("small/bigNative != 0: ") + bigDsmall.getText ());

        bigDsmall = STAmount::divide (smallValue, bigValue, uint160 (), uint160 ());

        expect (bigDsmall.isZero (), String ("(small/big)->N != 0: ") + bigDsmall.getText ());

        bigDsmall = STAmount::divide (smallValue, bigNative, uint160 (), uint160 ());

        expect (bigDsmall.isZero (), String ("(small/bigNative)->N != 0: ") + bigDsmall.getText ());

        // very bad offer
        uint64 r = STAmount::getRate (smallValue, bigValue);

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

        uint64 value = 25000000000000000ull;
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

    void runTest ()
    {
        testSetValue ();
        testNativeCurrency ();
        testCustomCurrency ();
        testArithmetic ();
        testUnderflow ();
        testRounding ();
    }
};

static STAmountTests stAmountTests;
