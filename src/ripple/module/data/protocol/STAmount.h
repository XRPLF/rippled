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

#ifndef RIPPLE_STAMOUNT_H
#define RIPPLE_STAMOUNT_H

#include <ripple/module/data/protocol/FieldNames.h>
#include <ripple/module/data/protocol/Serializer.h>
#include <ripple/module/data/protocol/SerializedType.h>

namespace ripple {

// Internal form:
// 1: If amount is zero, then value is zero and offset is -100
// 2: Otherwise:
//   legal offset range is -96 to +80 inclusive
//   value range is 10^15 to (10^16 - 1) inclusive
//  amount = value * [10 ^ offset]

// Wire form:
// High 8 bits are (offset+142), legal range is, 80 to 22 inclusive
// Low 56 bits are value, legal range is 10^15 to (10^16 - 1) inclusive
class STAmount : public SerializedType
{
public:
    static const int cMinOffset            = -96, cMaxOffset = 80;
    static const std::uint64_t cMinValue   = 1000000000000000ull, cMaxValue = 9999999999999999ull;
    static const std::uint64_t cMaxNative  = 9000000000000000000ull;
    static const std::uint64_t cMaxNativeN = 100000000000000000ull; // max native value on network
    static const std::uint64_t cNotNative  = 0x8000000000000000ull;
    static const std::uint64_t cPosNative  = 0x4000000000000000ull;

    static std::uint64_t   uRateOne;

    STAmount (std::uint64_t v = 0, bool isNeg = false)
            : mValue (v), mOffset (0), mIsNative (true), mIsNegative (isNeg)
    {
        if (v == 0) mIsNegative = false;
    }

    STAmount (SField::ref n, std::uint64_t v = 0, bool isNeg = false)
        : SerializedType (n), mValue (v), mOffset (0), mIsNative (true), mIsNegative (isNeg)
    {
        ;
    }

    STAmount (SField::ref n, std::int64_t v) : SerializedType (n), mOffset (0), mIsNative (true)
    {
        set (v);
    }

    STAmount (Currency const& currency, Account const& issuer,
              std::uint64_t uV = 0, int iOff = 0, bool bNegative = false)
        : mCurrency (currency), mIssuer (issuer), mValue (uV), mOffset (iOff), mIsNegative (bNegative)
    {
        canonicalize ();
    }

    STAmount (Currency const& currency, Account const& issuer,
              std::uint32_t uV, int iOff = 0, bool bNegative = false)
        : mCurrency (currency), mIssuer (issuer), mValue (uV), mOffset (iOff), mIsNegative (bNegative)
    {
        canonicalize ();
    }

    STAmount (SField::ref n, Currency const& currency, Account const& issuer,
              std::uint64_t v = 0, int off = 0, bool isNeg = false) :
        SerializedType (n), mCurrency (currency), mIssuer (issuer), mValue (v), mOffset (off), mIsNegative (isNeg)
    {
        canonicalize ();
    }

    STAmount (Currency const& currency, Account const& issuer, std::int64_t v, int iOff = 0)
        : mCurrency (currency), mIssuer (issuer), mOffset (iOff)
    {
        set (v);
        canonicalize ();
    }

    STAmount (SField::ref n, Currency const& currency, Account const& issuer, std::int64_t v, int off = 0)
        : SerializedType (n), mCurrency (currency), mIssuer (issuer), mOffset (off)
    {
        set (v);
        canonicalize ();
    }

    STAmount (Currency const& currency, Account const& issuer, int v, int iOff = 0)
        : mCurrency (currency), mIssuer (issuer), mOffset (iOff)
    {
        set (v);
        canonicalize ();
    }

    STAmount (SField::ref n, Currency const& currency, Account const& issuer, int v, int off = 0)
        : SerializedType (n), mCurrency (currency), mIssuer (issuer), mOffset (off)
    {
        set (v);
        canonicalize ();
    }

    STAmount (SField::ref, const Json::Value&);

    static STAmount createFromInt64 (SField::ref n, std::int64_t v);

    static std::unique_ptr<SerializedType> deserialize (SerializerIterator& sit, SField::ref name)
    {
        return std::unique_ptr<SerializedType> (construct (sit, name));
    }

    bool bSetJson (const Json::Value& jvSource);

    static STAmount saFromRate (std::uint64_t uRate = 0)
    {
        return STAmount (noCurrency(), noAccount(), uRate, -9, false);
    }

    SerializedTypeID getSType () const
    {
        return STI_AMOUNT;
    }
    std::string getText () const;
    std::string getFullText () const;
    void add (Serializer& s) const;

    int getExponent () const
    {
        return mOffset;
    }
    std::uint64_t getMantissa () const
    {
        return mValue;
    }

    int signum () const
    {
        return mValue ? (mIsNegative ? -1 : 1) : 0;
    }

    // When the currency is XRP, the value in raw units. S=signed
    std::uint64_t getNValue () const
    {
        if (!mIsNative) throw std::runtime_error ("not native");

        return mValue;
    }
    void setNValue (std::uint64_t v)
    {
        if (!mIsNative) throw std::runtime_error ("not native");

        mValue = v;
    }
    std::int64_t getSNValue () const;
    void setSNValue (std::int64_t);

    std::string getHumanCurrency () const;

    bool isNative () const
    {
        return mIsNative;
    }
    bool isLegalNet () const
    {
        return !mIsNative || (mValue <= cMaxNativeN);
    }

    explicit
    operator bool () const noexcept
    {
        return *this != zero;
    }

    void negate ()
    {
        if (*this != zero)
            mIsNegative = !mIsNegative;
    }

    // Return a copy of amount with the same Issuer and Currency but zero value.
    STAmount zeroed() const
    {
        STAmount c(mCurrency, mIssuer);
        c = zero;
        // See https://ripplelabs.atlassian.net/browse/WC-1847?jql=
        return c;
    }

    void clear ()
    {
        // VFALCO: Why -100?
        mOffset = mIsNative ? 0 : -100;
        mValue = 0;
        mIsNegative = false;
    }

    // Zero while copying currency and issuer.
    void clear (const STAmount& saTmpl)
    {
        mCurrency = saTmpl.mCurrency;
        mIssuer = saTmpl.mIssuer;
        mIsNative = saTmpl.mIsNative;
        clear ();
    }
    void clear (Currency const& currency, Account const& issuer)
    {
        mCurrency = currency;
        mIssuer = issuer;
        mIsNative = !currency;
        clear ();
    }

    STAmount& operator=(beast::Zero)
    {
        clear ();
        return *this;
    }

    int compare (const STAmount&) const;

    Account const& getIssuer () const
    {
        return mIssuer;
    }
    STAmount* setIssuer (Account const& uIssuer)
    {
        mIssuer   = uIssuer;
        return this;
    }

    Currency const& getCurrency () const
    {
        return mCurrency;
    }
    bool setValue (const std::string& sAmount);
    bool setFullValue (
        const std::string& sAmount, const std::string& sCurrency = "",
        const std::string& sIssuer = "");
    void setValue (const STAmount&);

    virtual bool isEquivalent (const SerializedType& t) const;
    virtual bool isDefault () const
    {
        return (mValue == 0) && mIssuer.isZero () && mCurrency.isZero ();
    }

    bool operator== (const STAmount&) const;
    bool operator!= (const STAmount&) const;
    bool operator< (const STAmount&) const;
    bool operator> (const STAmount&) const;
    bool operator<= (const STAmount&) const;
    bool operator>= (const STAmount&) const;
    bool isComparable (const STAmount&) const;
    void throwComparable (const STAmount&) const;

    // native currency only
    bool operator< (std::uint64_t) const;
    bool operator> (std::uint64_t) const;
    bool operator<= (std::uint64_t) const;
    bool operator>= (std::uint64_t) const;
    STAmount operator+ (std::uint64_t) const;
    STAmount operator- (std::uint64_t) const;
    STAmount operator- (void) const;

    STAmount& operator+= (const STAmount&);
    STAmount& operator-= (const STAmount&);
    STAmount& operator+= (std::uint64_t);
    STAmount& operator-= (std::uint64_t);
    STAmount& operator= (std::uint64_t);

    operator double () const;

    friend STAmount operator+ (const STAmount& v1, const STAmount& v2);
    friend STAmount operator- (const STAmount& v1, const STAmount& v2);

    static STAmount divide (
        const STAmount& v1, const STAmount& v2,
        Currency const& currency, Account const& issuer);

    static STAmount divide (
        const STAmount& v1, const STAmount& v2, const STAmount& saUnit)
    {
        return divide (v1, v2, saUnit.getCurrency (), saUnit.getIssuer ());
    }
    static STAmount divide (const STAmount& v1, const STAmount& v2)
    {
        return divide (v1, v2, v1);
    }

    static STAmount multiply (
        const STAmount& v1, const STAmount& v2,
        Currency const& currency, Account const& issuer);

    static STAmount multiply (
        const STAmount& v1, const STAmount& v2, const STAmount& saUnit)
    {
        return multiply (v1, v2, saUnit.getCurrency (), saUnit.getIssuer ());
    }
    static STAmount multiply (const STAmount& v1, const STAmount& v2)
    {
        return multiply (v1, v2, v1);
    }

    /* addRound, subRound can end up rounding if the amount subtracted is too small
       to make a change. Consder (X-d) where d is very small relative to X.
       If you ask to round down, then (X-d) should not be X unless d is zero.
       If you ask to round up, (X+d) should never be X unless d is zero. (Assuming X and d are positive).
    */
    // Add, subtract, multiply, or divide rounding result in specified direction
    static STAmount addRound (const STAmount& v1, const STAmount& v2, bool roundUp);
    static STAmount subRound (const STAmount& v1, const STAmount& v2, bool roundUp);
    static STAmount mulRound (const STAmount& v1, const STAmount& v2,
                              Currency const& currency, Account const& issuer, bool roundUp);
    static STAmount divRound (const STAmount& v1, const STAmount& v2,
                              Currency const& currency, Account const& issuer, bool roundUp);

    static STAmount mulRound (const STAmount& v1, const STAmount& v2, const STAmount& saUnit, bool roundUp)
    {
        return mulRound (v1, v2, saUnit.getCurrency (), saUnit.getIssuer (), roundUp);
    }
    static STAmount mulRound (const STAmount& v1, const STAmount& v2, bool roundUp)
    {
        return mulRound (v1, v2, v1.getCurrency (), v1.getIssuer (), roundUp);
    }
    static STAmount divRound (const STAmount& v1, const STAmount& v2, const STAmount& saUnit, bool roundUp)
    {
        return divRound (v1, v2, saUnit.getCurrency (), saUnit.getIssuer (), roundUp);
    }
    static STAmount divRound (const STAmount& v1, const STAmount& v2, bool roundUp)
    {
        return divRound (v1, v2, v1.getCurrency (), v1.getIssuer (), roundUp);
    }

    // Someone is offering X for Y, what is the rate?
    // Rate: smaller is better, the taker wants the most out: in/out
    static std::uint64_t getRate (const STAmount& offerOut, const STAmount& offerIn);
    static STAmount setRate (std::uint64_t rate);

    // Someone is offering X for Y, I need Z, how much do I pay
    static STAmount getPay (
        const STAmount& offerOut, const STAmount& offerIn, const STAmount& needed);

    static STAmount deserialize (SerializerIterator&);

    Json::Value getJson (int) const;
    void setJson (Json::Value&) const;

    STAmount getRound () const;
    void roundSelf ();

    static void canonicalizeRound (
        bool isNative, std::uint64_t& value, int& offset, bool roundUp);

private:
    Currency mCurrency;      // Compared by ==. Always update mIsNative.
    Account mIssuer;        // Not compared by ==. 0 for XRP.

    std::uint64_t  mValue;
    int            mOffset;
    bool           mIsNative;      // Always !mCurrency. Native is XRP.
    bool           mIsNegative;

    void canonicalize ();
    STAmount* duplicate () const
    {
        return new STAmount (*this);
    }
    static STAmount* construct (SerializerIterator&, SField::ref name);

    STAmount (SField::ref name, Currency const& cur, Account const& iss,
              std::uint64_t val, int off, bool isNat, bool isNeg)
        : SerializedType (name), mCurrency (cur), mIssuer (iss),  mValue (val),
          mOffset (off), mIsNative (isNat), mIsNegative (isNeg)
    {
        ;
    }

    void set (std::int64_t v)
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

    void set (int v)
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
};

// VFALCO TODO Make static member accessors for these in STAmount
extern const STAmount saZero;
extern const STAmount saOne;

} // ripple

#endif
