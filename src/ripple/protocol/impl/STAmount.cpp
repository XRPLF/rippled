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

#include <ripple/basics/Log.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/crypto/CBigNum.h>
#include <ripple/core/SystemParameters.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/UintTypes.h>
#include <beast/module/core/text/LexicalCast.h>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include <beast/cxx14/iterator.h> // <iterator>

namespace ripple {

static const std::uint64_t tenTo14 = 100000000000000ull;
static const std::uint64_t tenTo14m1 = tenTo14 - 1;
static const std::uint64_t tenTo17 = tenTo14 * 1000;

//------------------------------------------------------------------------------

STAmount::STAmount (SField::ref name, Issue const& issue,
        mantissa_type mantissa, exponent_type exponent,
            bool native, bool negative)
    : STBase (name)
    , mIssue (issue)
    , mValue (mantissa)
    , mOffset (exponent)
    , mIsNative (native)
    , mIsNegative (negative)
{
    canonicalize();
}

STAmount::STAmount (SField::ref name, Issue const& issue,
        mantissa_type mantissa, exponent_type exponent,
            bool native, bool negative, unchecked)
    : STBase (name)
    , mIssue (issue)
    , mValue (mantissa)
    , mOffset (exponent)
    , mIsNative (native)
    , mIsNegative (negative)
{
}

STAmount::STAmount (SField::ref name, std::int64_t mantissa)
    : STBase (name)
    , mOffset (0)
    , mIsNative (true)
{
    set (mantissa);
}

STAmount::STAmount (SField::ref name,
        std::uint64_t mantissa, bool negative)
    : STBase (name)
    , mValue (mantissa)
    , mOffset (0)
    , mIsNative (true)
    , mIsNegative (negative)
{
}

STAmount::STAmount (SField::ref name, Issue const& issue,
        std::uint64_t mantissa, int exponent, bool negative)
    : STBase (name)
    , mIssue (issue)
    , mValue (mantissa)
    , mOffset (exponent)
    , mIsNegative (negative)
{
    canonicalize ();
}

//------------------------------------------------------------------------------

STAmount::STAmount (std::uint64_t mantissa, bool negative)
    : mValue (mantissa)
    , mOffset (0)
    , mIsNative (true)
    , mIsNegative (mantissa != 0 && negative)
{
}

STAmount::STAmount (Issue const& issue,
    std::uint64_t mantissa, int exponent, bool negative)
    : mIssue (issue)
    , mValue (mantissa)
    , mOffset (exponent)
    , mIsNegative (negative)
{
    canonicalize ();
}

STAmount::STAmount (Issue const& issue,
        std::int64_t mantissa, int exponent)
    : mIssue (issue)
    , mOffset (exponent)
{
    set (mantissa);
    canonicalize ();
}

STAmount::STAmount (Issue const& issue,
        std::uint32_t mantissa, int exponent, bool negative)
    : STAmount (issue, static_cast<std::uint64_t>(mantissa), exponent, negative)
{
}

STAmount::STAmount (Issue const& issue,
        int mantissa, int exponent)
    : STAmount (issue, static_cast<std::int64_t>(mantissa), exponent)
{
}

std::unique_ptr<STAmount>
STAmount::construct (SerializerIterator& sit, SField::ref name)
{
    std::uint64_t value = sit.get64 ();

    // native
    if ((value & cNotNative) == 0)
    {
        // positive
        if ((value & cPosNative) != 0)
            return std::make_unique<STAmount> (name, value & ~cPosNative, false);

        // negative
        if (value == 0)
            throw std::runtime_error ("negative zero is not canonical");

        return std::make_unique<STAmount> (name, value, true);
    }

    Issue issue;
    issue.currency.copyFrom (sit.get160 ());

    if (isXRP (issue.currency))
        throw std::runtime_error ("invalid native currency");

    issue.account.copyFrom (sit.get160 ());

    if (isXRP (issue.account))
        throw std::runtime_error ("invalid native account");

    // 10 bits for the offset, sign and "not native" flag
    int offset = static_cast<int> (value >> (64 - 10));

    value &= ~ (1023ull << (64 - 10));

    if (value)
    {
        bool isNegative = (offset & 256) == 0;
        offset = (offset & 255) - 97; // center the range

        if (value < cMinValue ||
            value > cMaxValue ||
            offset < cMinOffset ||
            offset > cMaxOffset)
        {
            throw std::runtime_error ("invalid currency value");
        }

        return std::make_unique<STAmount> (name, issue, value, offset, isNegative);
    }

    if (offset != 512)
        throw std::runtime_error ("invalid currency value");

    return std::make_unique<STAmount> (name, issue);
}

STAmount
STAmount::createFromInt64 (SField::ref name, std::int64_t value)
{
    return value >= 0
           ? STAmount (name, static_cast<std::uint64_t> (value), false)
           : STAmount (name, static_cast<std::uint64_t> (-value), true);
}

STAmount STAmount::deserialize (SerializerIterator& it)
{
    auto s = construct (it, sfGeneric);

    if (!s)
        throw std::runtime_error("Deserialization error");

    return STAmount (*s);
}

//------------------------------------------------------------------------------
//
// Operators
//
//------------------------------------------------------------------------------

bool STAmount::isComparable (STAmount const& t) const
{
    // are these two STAmount instances in the same currency
    if (mIsNative) return t.mIsNative;

    if (t.mIsNative) return false;

    return mIssue.currency == t.mIssue.currency;
}

void STAmount::throwComparable (STAmount const& t) const
{
    // throw an exception if these two STAmount instances are incomparable
    if (!isComparable (t))
        throw std::runtime_error ("amounts are not comparable");
}

STAmount& STAmount::operator+= (STAmount const& a)
{
    *this = *this + a;
    return *this;
}

STAmount& STAmount::operator-= (STAmount const& a)
{
    *this = *this - a;
    return *this;
}

STAmount& STAmount::operator+= (std::uint64_t v)
{
    assert (mIsNative);
    if (!mIsNative)
        throw std::runtime_error ("not native");
    // VFALCO TODO The cast looks dangerous, is it needed?
    setSNValue (getSNValue () + static_cast<std::int64_t> (v));
    return *this;
}

STAmount& STAmount::operator-= (std::uint64_t v)
{
    assert (mIsNative);

    if (!mIsNative)
        throw std::runtime_error ("not native");

    // VFALCO TODO The cast looks dangerous, is it needed?
    setSNValue (getSNValue () - static_cast<std::int64_t> (v));
    return *this;
}

STAmount& STAmount::operator= (std::uint64_t v)
{
    // Does not copy name, does not change currency type.
    mOffset = 0;
    mValue = v;
    mIsNegative = false;
    if (!mIsNative)
        canonicalize ();
    return *this;
}



STAmount operator+ (STAmount const& v1, STAmount const& v2)
{
    v1.throwComparable (v2);

    if (v2 == zero)
        return v1;

    if (v1 == zero)
    {
        // Result must be in terms of v1 currency and issuer.
        return STAmount (v1.getFName (), v1.mIssue,
                         v2.mValue, v2.mOffset, v2.mIsNegative);
    }

    if (v1.mIsNative)
        return STAmount (v1.getFName (), v1.getSNValue () + v2.getSNValue ());

    int ov1 = v1.mOffset, ov2 = v2.mOffset;
    std::int64_t vv1 = static_cast<std::int64_t> (v1.mValue);
    std::int64_t vv2 = static_cast<std::int64_t> (v2.mValue);

    if (v1.mIsNegative)
        vv1 = -vv1;

    if (v2.mIsNegative)
        vv2 = -vv2;

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

    // This addition cannot overflow an std::int64_t. It can overflow an
    // STAmount and the constructor will throw.

    std::int64_t fv = vv1 + vv2;

    if ((fv >= -10) && (fv <= 10))
        return STAmount (v1.getFName (), v1.mIssue);
    if (fv >= 0)
        return STAmount (v1.getFName (), v1.mIssue, fv, ov1, false);
    return STAmount (v1.getFName (), v1.mIssue, -fv, ov1, true);
}

STAmount operator- (STAmount const& v1, STAmount const& v2)
{
    v1.throwComparable (v2);

    if (v2 == zero)
        return v1;

    if (v2.mIsNative)
    {
        // XXX This could be better, check for overflow and that maximum range
        // is covered.
        return STAmount::createFromInt64 (
                v1.getFName (), v1.getSNValue () - v2.getSNValue ());
    }

    int ov1 = v1.mOffset, ov2 = v2.mOffset;
    auto vv1 = static_cast<std::int64_t> (v1.mValue);
    auto vv2 = static_cast<std::int64_t> (v2.mValue);

    if (v1.mIsNegative)
        vv1 = -vv1;

    if (v2.mIsNegative)
        vv2 = -vv2;

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
        return STAmount (v1.getFName (), v1.mIssue);
    if (fv >= 0)
        return STAmount (v1.getFName (), v1.mIssue, fv, ov1, false);
    return STAmount (v1.getFName (), v1.mIssue, -fv, ov1, true);
}

//------------------------------------------------------------------------------

std::uint64_t const STAmount::uRateOne = getRate (STAmount (1), STAmount (1));

// Note: mIsNative and mIssue.currency must be set already!
bool
STAmount::setValue (std::string const& amount)
{
    static boost::regex const reNumber (
        "^"                       // the beginning of the string
        "([-+]?)"                 // (optional) + or - character
        "(0|[1-9][0-9]*)"         // a number (no leading zeroes, unless 0)
        "(\\.([0-9]+))?"          // (optional) period followed by any number
        "([eE]([+-]?)([0-9]+))?"  // (optional) E, optional + or -, any number
        "$",
        boost::regex_constants::optimize);

    boost::smatch match;

    if (!boost::regex_match (amount, match, reNumber))
    {
        WriteLog (lsWARNING, STAmount) <<
            "Number not valid: \"" << amount << "\"";
        return false;
    }

    // Match fields:
    //   0 = whole input
    //   1 = sign
    //   2 = integer portion
    //   3 = whole fraction (with '.')
    //   4 = fraction (without '.')
    //   5 = whole exponent (with 'e')
    //   6 = exponent sign
    //   7 = exponent number

    try
    {
        // CHECKME: Why 32? Shouldn't this be 16?
        if ((match[2].length () + match[4].length ()) > 32)
        {
            WriteLog (lsWARNING, STAmount) << "Overlong number: " << amount;
            return false;
        }

        mIsNegative = (match[1].matched && (match[1] == "-"));

        // Can't specify XRP using fractional representation
        if (mIsNative && match[3].matched)
            return false;

        if (!match[4].matched) // integer only
        {
            mValue = beast::lexicalCastThrow <std::uint64_t> (std::string (match[2]));
            mOffset = 0;
        }
        else
        {
            // integer and fraction
            mValue = beast::lexicalCastThrow <std::uint64_t> (match[2] + match[4]);
            mOffset = -(match[4].length ());
        }

        if (match[5].matched)
        {
            // we have an exponent
            if (match[6].matched && (match[6] == "-"))
                mOffset -= beast::lexicalCastThrow <int> (std::string (match[7]));
            else
                mOffset += beast::lexicalCastThrow <int> (std::string (match[7]));
        }

        canonicalize ();

        WriteLog (lsTRACE, STAmount) <<
            "Canonicalized \"" << amount << "\" to " << mValue << " : " << mOffset;
    }
    catch (...)
    {
        return false;
    }

    return true;
}

void
STAmount::setIssue (Issue const& issue)
{
    mIssue = std::move(issue);
    mIsNative = isXRP (*this);
}

std::uint64_t
STAmount::getNValue () const
{
    if (!mIsNative)
        throw std::runtime_error ("not native");
    return mValue;
}

std::int64_t
STAmount::getSNValue () const
{
    // signed native value
    if (!mIsNative)
        throw std::runtime_error ("not native");

    if (mIsNegative)
        return - static_cast<std::int64_t> (mValue);

    return static_cast<std::int64_t> (mValue);
}

std::string STAmount::getHumanCurrency () const
{
    return to_string (mIssue.currency);
}

void
STAmount::setNValue (std::uint64_t v)
{
    if (!mIsNative)
        throw std::runtime_error ("not native");
    mValue = v;
}

void
STAmount::setSNValue (std::int64_t v)
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

// Convert an offer into an index amount so they sort by rate.
// A taker will take the best, lowest, rate first.
// (e.g. a taker will prefer pay 1 get 3 over pay 1 get 2.
// --> offerOut: takerGets: How much the offerer is selling to the taker.
// -->  offerIn: takerPays: How much the offerer is receiving from the taker.
// <--    uRate: normalize(offerIn/offerOut)
//             A lower rate is better for the person taking the order.
//             The taker gets more for less with a lower rate.
// Zero is returned if the offer is worthless.
std::uint64_t
getRate (STAmount const& offerOut, STAmount const& offerIn)
{
    if (offerOut == zero)
        return 0;
    try
    {
        STAmount r = divide (offerIn, offerOut, noIssue());
        if (r == zero) // offer is too good
            return 0;
        assert ((r.exponent() >= -100) && (r.exponent() <= 155));
        std::uint64_t ret = r.exponent() + 100;
        return (ret << (64 - 8)) | r.mantissa();
    }
    catch (...)
    {
    }

    // overflow -- very bad offer
    return 0;
}

void STAmount::setJson (Json::Value& elem) const
{
    elem = Json::objectValue;

    if (!mIsNative)
    {
        // It is an error for currency or issuer not to be specified for valid
        // json.
        elem[jss::value]      = getText ();
        elem[jss::currency]   = getHumanCurrency ();
        elem[jss::issuer]     = to_string (mIssue.account);
    }
    else
    {
        elem = getText ();
    }
}

// VFALCO What does this perplexing function do?
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

//------------------------------------------------------------------------------
//
// STBase
//
//------------------------------------------------------------------------------

std::string
STAmount::getFullText () const
{
    std::string ret;

    ret.reserve(64);
    ret = getText () + "/" + getHumanCurrency ();

    if (!mIsNative)
    {
        ret += "/";

        if (isXRP (*this))
            ret += "0";
        else if (mIssue.account == noAccount())
            ret += "1";
        else
            ret += to_string (mIssue.account);
    }

    return ret;
}

std::string
STAmount::getText () const
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

        if (scientific)
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

Json::Value
STAmount::getJson (int) const
{
    Json::Value elem;
    setJson (elem);
    return elem;
}

void
STAmount::add (Serializer& s) const
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

        s.add160 (mIssue.currency);
        s.add160 (mIssue.account);
    }
}

bool
STAmount::isEquivalent (const STBase& t) const
{
    const STAmount* v = dynamic_cast<const STAmount*> (&t);
    return v && (*v == *this);
}

STAmount*
STAmount::duplicate () const
{
    return new STAmount (*this);
}

//------------------------------------------------------------------------------

// amount = value * [10 ^ offset]
// Representation range is 10^80 - 10^(-80).
// On the wire, high 8 bits are (offset+142), low 56 bits are value.
//
// Value is zero if amount is zero, otherwise value is 10^15 to (10^16 - 1)
// inclusive.
void STAmount::canonicalize ()
{
    if (isXRP (*this))
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

        if (mValue > cMaxNativeN)
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
    }

    if (mOffset > cMaxOffset)
        throw std::runtime_error ("value overflow");

    assert ((mValue == 0) || ((mValue >= cMinValue) && (mValue <= cMaxValue)));
    assert ((mValue == 0) || ((mOffset >= cMinOffset) && (mOffset <= cMaxOffset)));
    assert ((mValue != 0) || (mOffset != -100));
}

void STAmount::set (std::int64_t v)
{
    if (v < 0)
    {
        mIsNegative = true;
        mValue = static_cast<std::uint64_t> (-v);
    }
    else
    {
        mIsNegative = false;
        mValue = static_cast<std::uint64_t> (v);
    }
}

//------------------------------------------------------------------------------

STAmount
amountFromRate (std::uint64_t uRate)
{
    return STAmount (noIssue(), uRate, -9, false);
}

STAmount
amountFromQuality (std::uint64_t rate)
{
    if (rate == 0)
        return STAmount (noIssue());

    std::uint64_t mantissa = rate & ~ (255ull << (64 - 8));
    int exponent = static_cast<int> (rate >> (64 - 8)) - 100;

    return STAmount (noIssue(), mantissa, exponent);
}

STAmount
amountFromJson (SField::ref name, Json::Value const& v)
{
    STAmount::mantissa_type mantissa = 0;
    STAmount::exponent_type exponent = 0;
    bool negative = false;
    Issue issue;

    Json::Value value;
    Json::Value currency;
    Json::Value issuer;

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
    {
        value = v;
    }

    bool const native = ! currency.isString () ||
        currency.asString ().empty () ||
        (currency.asString () == systemCurrencyCode());

    if (native)
    {
        if (v.isObject ())
            throw std::runtime_error ("XRP may not be specified as an object");
    }
    else
    {
        // non-XRP
        if (! to_currency (issue.currency, currency.asString ()))
            throw std::runtime_error ("invalid currency");

        if (! issuer.isString ()
                || !to_issuer (issue.account, issuer.asString ()))
            throw std::runtime_error ("invalid issuer");

        if (isXRP (issue.currency))
            throw std::runtime_error ("invalid issuer");
    }

    if (value.isInt ())
    {
        if (value.asInt () >= 0)
        {
            mantissa = value.asInt ();
        }
        else
        {
            mantissa = -value.asInt ();
            negative = true;
        }
    }
    else if (value.isUInt ())
    {
        mantissa = v.asUInt ();
    }
    else if (value.isString ())
    {
        if (native)
        {
            std::int64_t val = beast::lexicalCastThrow <std::int64_t> (
                value.asString ());

            if (val >= 0)
            {
                mantissa = val;
            }
            else
            {
                mantissa = -val;
                negative = true;
            }
        }
        else
        {
            STAmount amount (name, issue, mantissa, exponent,
                native, negative, STAmount::unchecked{});
            amount.setValue (value.asString ());
            return amount;
        }
    }
    else
    {
        throw std::runtime_error ("invalid amount type");
    }

    return STAmount (name, issue, mantissa, exponent, native, negative);
}

bool
amountFromJsonNoThrow (STAmount& result, Json::Value const& jvSource)
{
    try
    {
        result = amountFromJson (sfGeneric, jvSource);
        return true;
    }
    catch (const std::exception& e)
    {
        WriteLog (lsDEBUG, STAmount) <<
            "amountFromJsonNoThrow: caught: " << e.what ();
    }
    return false;
}

//------------------------------------------------------------------------------
//
// Operators
//
//------------------------------------------------------------------------------

static
int
compare (STAmount const& lhs, STAmount const& rhs)
{
    // Compares the value of a to the value of this STAmount, amounts must be comparable
    if (lhs.negative() != rhs.negative())
        return lhs.negative() ? -1 : 1;
    if (lhs.mantissa() == 0)
    {
        if (rhs.negative())
            return 1;
        return (rhs.mantissa() != 0) ? -1 : 0;
    }
    if (rhs.mantissa() == 0) return 1;
    if (lhs.exponent() > rhs.exponent()) return lhs.negative() ? -1 : 1;
    if (lhs.exponent() < rhs.exponent()) return lhs.negative() ? 1 : -1;
    if (lhs.mantissa() > rhs.mantissa()) return lhs.negative() ? -1 : 1;
    if (lhs.mantissa() < rhs.mantissa()) return lhs.negative() ? 1 : -1;
    return 0;
}

bool
operator== (STAmount const& lhs, STAmount const& rhs)
{
    return lhs.isComparable (rhs) && lhs.negative() == rhs.negative() &&
        lhs.exponent() == rhs.exponent() && lhs.mantissa() == rhs.mantissa();
}

bool
operator!= (STAmount const& lhs, STAmount const& rhs)
{
    return lhs.exponent() != rhs.exponent() ||
        lhs.mantissa() != rhs.mantissa() ||
        lhs.negative() != rhs.negative() || ! lhs.isComparable (rhs);
}

bool
operator< (STAmount const& lhs, STAmount const& rhs)
{
    lhs.throwComparable (rhs);
    return compare (lhs, rhs) < 0;
}

bool
operator> (STAmount const& lhs, STAmount const& rhs)
{
    lhs.throwComparable (rhs);
    return compare (lhs, rhs) > 0;
}

bool
operator<= (STAmount const& lhs, STAmount const& rhs)
{
    lhs.throwComparable (rhs);
    return compare (lhs, rhs) <= 0;
}

bool
operator>= (STAmount const& lhs, STAmount const& rhs)
{
    lhs.throwComparable (rhs);
    return compare (lhs, rhs) >= 0;
}

// native currency only

bool
operator< (STAmount const& lhs, std::uint64_t rhs)
{
    // VFALCO Why the cast?
    return lhs.getSNValue() < static_cast <std::int64_t> (rhs);
}

bool
operator> (STAmount const& lhs, std::uint64_t rhs)
{
    // VFALCO Why the cast?
    return lhs.getSNValue() > static_cast <std::int64_t> (rhs);
}

bool
operator<= (STAmount const& lhs, std::uint64_t rhs)
{
    // VFALCO TODO The cast looks dangerous, is it needed?
    return lhs.getSNValue () <= static_cast <std::int64_t> (rhs);
}

bool
operator>= (STAmount const& lhs, std::uint64_t rhs)
{
    // VFALCO TODO The cast looks dangerous, is it needed?
    return lhs.getSNValue() >= static_cast<std::int64_t> (rhs);
}

STAmount
operator+ (STAmount const& lhs, std::uint64_t rhs)
{
    // VFALCO TODO The cast looks dangerous, is it needed?
    return STAmount (lhs.getFName (),
        lhs.getSNValue () + static_cast <std::int64_t> (rhs));
}

STAmount
operator- (STAmount const& lhs, std::uint64_t rhs)
{
    // VFALCO TODO The cast looks dangerous, is it needed?
    return STAmount (lhs.getFName (),
        lhs.getSNValue () - static_cast <std::int64_t> (rhs));
}

STAmount
operator- (STAmount const& value)
{
    if (value.mantissa() == 0)
        return value;
    return STAmount (value.getFName (),
        value.issue(), value.mantissa(), value.exponent(),
            value.native(), ! value.negative(), STAmount::unchecked{});
}

//------------------------------------------------------------------------------
//
// Arithmetic
//
//------------------------------------------------------------------------------

STAmount
divide (STAmount const& num, STAmount const& den, Issue const& issue)
{
    if (den == zero)
        throw std::runtime_error ("division by zero");

    if (num == zero)
        return {issue};

    std::uint64_t numVal = num.mantissa();
    std::uint64_t denVal = den.mantissa();
    int numOffset = num.exponent();
    int denOffset = den.exponent();

    if (num.native())
    {
        while (numVal < STAmount::cMinValue)
        {
            // Need to bring into range
            numVal *= 10;
            --numOffset;
        }
    }

    if (den.native())
    {
        while (denVal < STAmount::cMinValue)
        {
            denVal *= 10;
            --denOffset;
        }
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

    // TODO(tom): where do 5 and 17 come from?
    return STAmount (issue, v.getuint64 () + 5,
                     numOffset - denOffset - 17,
                     num.negative() != den.negative());
}

STAmount
multiply (STAmount const& v1, STAmount const& v2, Issue const& issue)
{
    if (v1 == zero || v2 == zero)
        return STAmount (issue);

    if (v1.native() && v2.native() && isXRP (issue))
    {
        std::uint64_t const minV = v1.getSNValue () < v2.getSNValue ()
                ? v1.getSNValue () : v2.getSNValue ();
        std::uint64_t const maxV = v1.getSNValue () < v2.getSNValue ()
                ? v2.getSNValue () : v1.getSNValue ();

        if (minV > 3000000000ull) // sqrt(cMaxNative)
            throw std::runtime_error ("Native value overflow");

        if (((maxV >> 32) * minV) > 2095475792ull) // cMaxNative / 2^32
            throw std::runtime_error ("Native value overflow");

        return STAmount (v1.getFName (), minV * maxV);
    }

    std::uint64_t value1 = v1.mantissa();
    std::uint64_t value2 = v2.mantissa();
    int offset1 = v1.exponent();
    int offset2 = v2.exponent();

    if (v1.native())
    {
        while (value1 < STAmount::cMinValue)
        {
            value1 *= 10;
            --offset1;
        }
    }

    if (v2.native())
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

    // TODO(tom): where do 7 and 14 come from?
    return STAmount (issue, v.getuint64 () + 7,
        offset1 + offset2 + 14, v1.negative() != v2.negative());
}

void
canonicalizeRound (bool isNative, std::uint64_t& value,
    int& offset, bool roundUp)
{
    if (!roundUp) // canonicalize already rounds down
        return;

    WriteLog (lsTRACE, STAmount)
        << "canonicalizeRound< " << value << ":" << offset;

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

    WriteLog (lsTRACE, STAmount)
        << "canonicalizeRound> " << value << ":" << offset;
}

STAmount
addRound (STAmount const& v1, STAmount const& v2, bool roundUp)
{
    v1.throwComparable (v2);

    if (v2.mantissa() == 0)
        return v1;

    if (v1.mantissa() == 0)
        return STAmount (v1.getFName (), v1.issue(), v2.mantissa(),
                         v2.exponent(), v2.negative());

    if (v1.native())
        return STAmount (v1.getFName (), v1.getSNValue () + v2.getSNValue ());

    int ov1 = v1.exponent(), ov2 = v2.exponent();
    auto vv1 = static_cast<std::int64_t> (v1.mantissa());
    auto vv2 = static_cast<std::int64_t> (v2.mantissa());

    if (v1.negative())
        vv1 = -vv1;

    if (v2.negative())
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
        return STAmount (v1.getFName (), v1.issue());
    else if (fv >= 0)
    {
        std::uint64_t v = static_cast<std::uint64_t> (fv);
        canonicalizeRound (false, v, ov1, roundUp);
        return STAmount (v1.getFName (), v1.issue(), v, ov1, false);
    }
    else
    {
        std::uint64_t v = static_cast<std::uint64_t> (-fv);
        canonicalizeRound (false, v, ov1, !roundUp);
        return STAmount (v1.getFName (), v1.issue(), v, ov1, true);
    }
}

STAmount
subRound (STAmount const& v1, STAmount const& v2, bool roundUp)
{
    v1.throwComparable (v2);

    if (v2.mantissa() == 0)
        return v1;

    if (v1.mantissa() == 0)
        return STAmount (v1.getFName (), v1.issue(), v2.mantissa(),
                         v2.exponent(), !v2.negative());

    if (v1.native())
        return STAmount (v1.getFName (), v1.getSNValue () - v2.getSNValue ());

    int ov1 = v1.exponent(), ov2 = v2.exponent();
    auto vv1 = static_cast<std::int64_t> (v1.mantissa());
    auto vv2 = static_cast<std::int64_t> (v2.mantissa());

    if (v1.negative())
        vv1 = -vv1;

    if (!v2.negative())
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
        return STAmount (v1.getFName (), v1.issue());

    if (fv >= 0)
    {
        std::uint64_t v = static_cast<std::uint64_t> (fv);
        canonicalizeRound (false, v, ov1, roundUp);
        return STAmount (v1.getFName (), v1.issue(), v, ov1, false);
    }
    else
    {
        std::uint64_t v = static_cast<std::uint64_t> (-fv);
        canonicalizeRound (false, v, ov1, !roundUp);
        return STAmount (v1.getFName (), v1.issue(), v, ov1, true);
    }
}

STAmount
mulRound (STAmount const& v1, STAmount const& v2,
    Issue const& issue, bool roundUp)
{
    if (v1 == zero || v2 == zero)
        return {issue};

    if (v1.native() && v2.native() && isXRP (issue))
    {
        std::uint64_t minV = (v1.getSNValue () < v2.getSNValue ()) ?
                v1.getSNValue () : v2.getSNValue ();
        std::uint64_t maxV = (v1.getSNValue () < v2.getSNValue ()) ?
                v2.getSNValue () : v1.getSNValue ();

        if (minV > 3000000000ull) // sqrt(cMaxNative)
            throw std::runtime_error ("Native value overflow");

        if (((maxV >> 32) * minV) > 2095475792ull) // cMaxNative / 2^32
            throw std::runtime_error ("Native value overflow");

        return STAmount (v1.getFName (), minV * maxV);
    }

    std::uint64_t value1 = v1.mantissa(), value2 = v2.mantissa();
    int offset1 = v1.exponent(), offset2 = v2.exponent();

    if (v1.native())
    {
        while (value1 < STAmount::cMinValue)
        {
            value1 *= 10;
            --offset1;
        }
    }

    if (v2.native())
    {
        while (value2 < STAmount::cMinValue)
        {
            value2 *= 10;
            --offset2;
        }
    }

    bool resultNegative = v1.negative() != v2.negative();
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
    canonicalizeRound (
        isXRP (issue), amount, offset, resultNegative != roundUp);
    return STAmount (issue, amount, offset, resultNegative);
}

STAmount
divRound (STAmount const& num, STAmount const& den,
    Issue const& issue, bool roundUp)
{
    if (den == zero)
        throw std::runtime_error ("division by zero");

    if (num == zero)
        return {issue};

    std::uint64_t numVal = num.mantissa(), denVal = den.mantissa();
    int numOffset = num.exponent(), denOffset = den.exponent();

    if (num.native())
        while (numVal < STAmount::cMinValue)
        {
            // Need to bring into range
            numVal *= 10;
            --numOffset;
        }

    if (den.native())
        while (denVal < STAmount::cMinValue)
        {
            denVal *= 10;
            --denOffset;
        }

    bool resultNegative = num.negative() != den.negative();
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
    canonicalizeRound (
        isXRP (issue), amount, offset, resultNegative != roundUp);
    return STAmount (issue, amount, offset, resultNegative);
}

} // ripple
