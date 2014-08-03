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
    static const int cMinOffset = -96;
    static const int cMaxOffset = 80;

    static const std::uint64_t cMinValue   = 1000000000000000ull;
    static const std::uint64_t cMaxValue   = 9999999999999999ull;
    static const std::uint64_t cMaxNative  = 9000000000000000000ull;

    // Max native value on network.
    static const std::uint64_t cMaxNativeN = 100000000000000000ull;
    static const std::uint64_t cNotNative  = 0x8000000000000000ull;
    static const std::uint64_t cPosNative  = 0x4000000000000000ull;

    static std::uint64_t   uRateOne;

    STAmount (std::uint64_t v = 0, bool negative = false)
            : mValue (v), mOffset (0), mIsNative (true), mIsNegative (negative)
    {
        if (v == 0) mIsNegative = false;
    }

    STAmount (SField::ref n, std::uint64_t v = 0, bool negative = false)
        : SerializedType (n), mValue (v), mOffset (0), mIsNative (true),
          mIsNegative (negative)
    {
    }

    STAmount (SField::ref n, std::int64_t v)
        : SerializedType (n), mOffset (0), mIsNative (true)
    {
        set (v);
    }

    STAmount (Issue const& issue,
              std::uint64_t uV = 0, int iOff = 0, bool negative = false)
            : mIssue(issue), mValue (uV), mOffset (iOff), mIsNegative (negative)
    {
        canonicalize ();
    }

    STAmount (Issue const& issue,
              std::uint32_t uV, int iOff = 0, bool negative = false)
        : mIssue(issue), mValue (uV), mOffset (iOff), mIsNegative (negative)
    {
        canonicalize ();
    }

    STAmount (SField::ref n, Issue const& issue,
              std::uint64_t v = 0, int off = 0, bool negative = false) :
        SerializedType (n), mIssue(issue), mValue (v), mOffset (off),
        mIsNegative (negative)
    {
        canonicalize ();
    }

    STAmount (Issue const& issue, std::int64_t v, int iOff = 0)
        : mIssue(issue), mOffset (iOff)
    {
        set (v);
        canonicalize ();
    }

    STAmount (SField::ref n, Issue const& issue, std::int64_t v, int off = 0)
        : SerializedType (n), mIssue(issue), mOffset (off)
    {
        set (v);
        canonicalize ();
    }

    STAmount (Issue const& issue, int v, int iOff = 0)
        : mIssue(issue), mOffset (iOff)
    {
        set (v);
        canonicalize ();
    }

    STAmount (SField::ref n, Issue const& issue, int v, int off = 0)
        : SerializedType (n), mIssue(issue), mOffset (off)
    {
        set (v);
        canonicalize ();
    }

    STAmount (SField::ref, Json::Value const&);

    static STAmount createFromInt64 (SField::ref n, std::int64_t v);

    static std::unique_ptr<SerializedType> deserialize (
        SerializerIterator& sit, SField::ref name)
    {
        return std::unique_ptr<SerializedType> (construct (sit, name));
    }

    bool bSetJson (Json::Value const& jvSource);

    static STAmount saFromRate (std::uint64_t uRate = 0)
    {
        return STAmount (noIssue(), uRate, -9, false);
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
        if (!mIsNative)
            throw std::runtime_error ("not native");

        return mValue;
    }
    void setNValue (std::uint64_t v)
    {
        if (!mIsNative)
            throw std::runtime_error ("not native");

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

    /** @return a copy of amount with the same Issuer and Currency but zero
        value. */
    STAmount zeroed() const
    {
        // TODO(tom): what does this next comment mean here?
        // See https://ripplelabs.atlassian.net/browse/WC-1847?jql=
        return STAmount (mIssue);
    }

    void clear ()
    {
        // VFALCO: Why -100?
        mOffset = mIsNative ? 0 : -100;
        mValue = 0;
        mIsNegative = false;
    }

    // Zero while copying currency and issuer.
    void clear (STAmount const& saTmpl)
    {
        clear(saTmpl.mIssue);
    }

    void clear (Issue const& issue)
    {
        setIssue(issue);
        clear ();
    }

    STAmount& operator=(beast::Zero)
    {
        clear ();
        return *this;
    }

    int compare (STAmount const&) const;

    Account const& getIssuer () const
    {
        return mIssue.account;
    }

    void setIssuer (Account const& uIssuer)
    {
        mIssue.account = uIssuer;
        setIssue(mIssue);
    }

    /** Set the Issue for this amount and update mIsNative. */
    void setIssue (Issue const& issue);

    Currency const& getCurrency () const
    {
        return mIssue.currency;
    }

    Issue const& issue () const
    {
        return mIssue;
    }

    bool setValue (std::string const& sAmount);
    bool setFullValue (
        std::string const& sAmount, std::string const& sCurrency = "",
        std::string const& sIssuer = "");
    void setValue (STAmount const&);

    virtual bool isEquivalent (const SerializedType& t) const;
    virtual bool isDefault () const
    {
        return (mValue == 0) && mIsNative;
    }

    bool operator== (STAmount const&) const;
    bool operator!= (STAmount const&) const;
    bool operator< (STAmount const&) const;
    bool operator> (STAmount const&) const;
    bool operator<= (STAmount const&) const;
    bool operator>= (STAmount const&) const;
    bool isComparable (STAmount const&) const;
    void throwComparable (STAmount const&) const;

    // native currency only
    bool operator< (std::uint64_t) const;
    bool operator> (std::uint64_t) const;
    bool operator<= (std::uint64_t) const;
    bool operator>= (std::uint64_t) const;
    STAmount operator+ (std::uint64_t) const;
    STAmount operator- (std::uint64_t) const;
    STAmount operator- (void) const;

    STAmount& operator+= (STAmount const&);
    STAmount& operator-= (STAmount const&);
    STAmount& operator+= (std::uint64_t);
    STAmount& operator-= (std::uint64_t);
    STAmount& operator= (std::uint64_t);

    operator double () const;

    friend STAmount operator+ (STAmount const& v1, STAmount const& v2);
    friend STAmount operator- (STAmount const& v1, STAmount const& v2);

    static STAmount divide (
        STAmount const& v1, STAmount const& v2, Issue const& issue);

    static STAmount divide (
        STAmount const& v1, STAmount const& v2, STAmount const& saUnit)
    {
        return divide (v1, v2, saUnit.issue ());
    }
    static STAmount divide (STAmount const& v1, STAmount const& v2)
    {
        return divide (v1, v2, v1);
    }

    static STAmount multiply (
        STAmount const& v1, STAmount const& v2, Issue const& issue);

    static STAmount multiply (
        STAmount const& v1, STAmount const& v2, STAmount const& saUnit)
    {
        return multiply (v1, v2, saUnit.issue());
    }
    static STAmount multiply (STAmount const& v1, STAmount const& v2)
    {
        return multiply (v1, v2, v1);
    }

    /* addRound, subRound can end up rounding if the amount subtracted is too small
       to make a change. Consder (X-d) where d is very small relative to X.
       If you ask to round down, then (X-d) should not be X unless d is zero.
       If you ask to round up, (X+d) should never be X unless d is zero. (Assuming X and d are positive).
    */
    // Add, subtract, multiply, or divide rounding result in specified direction
    static STAmount addRound (
        STAmount const& v1, STAmount const& v2, bool roundUp);
    static STAmount subRound (
        STAmount const& v1, STAmount const& v2, bool roundUp);
    static STAmount mulRound (
        STAmount const& v1, STAmount const& v2, Issue const& issue,
        bool roundUp);
    static STAmount divRound (
        STAmount const& v1, STAmount const& v2, Issue const& issue,
        bool roundUp);

    static STAmount mulRound (
        STAmount const& v1, STAmount const& v2, STAmount const& saUnit,
        bool roundUp)
    {
        return mulRound (v1, v2, saUnit.issue (), roundUp);
    }
    static STAmount mulRound (
        STAmount const& v1, STAmount const& v2, bool roundUp)
    {
        return mulRound (v1, v2, v1.issue (), roundUp);
    }
    static STAmount divRound (
        STAmount const& v1, STAmount const& v2, STAmount const& saUnit,
        bool roundUp)
    {
        return divRound (v1, v2, saUnit.issue (), roundUp);
    }
    static STAmount divRound (
        STAmount const& v1, STAmount const& v2, bool roundUp)
    {
        return divRound (v1, v2, v1.issue (), roundUp);
    }

    // Someone is offering X for Y, what is the rate?
    // Rate: smaller is better, the taker wants the most out: in/out
    static std::uint64_t getRate (
        STAmount const& offerOut, STAmount const& offerIn);
    static STAmount setRate (std::uint64_t rate);

    // Someone is offering X for Y, I need Z, how much do I pay

    // WARNING: most methods in rippled have parameters ordered "in, out" - this
    // one is ordered "out, in".
    static STAmount getPay (
        STAmount const& out, STAmount const& in, STAmount const& needed);

    static STAmount deserialize (SerializerIterator&);

    Json::Value getJson (int) const;
    void setJson (Json::Value&) const;

    STAmount getRound () const;
    void roundSelf ();

    static void canonicalizeRound (
        bool isNative, std::uint64_t& value, int& offset, bool roundUp);

private:
    Issue mIssue;

    std::uint64_t  mValue;
    int            mOffset;
    bool           mIsNative;      // A shorthand for isXRP(mIssue).
    bool           mIsNegative;

    void canonicalize ();
    STAmount* duplicate () const
    {
        return new STAmount (*this);
    }
    static STAmount* construct (SerializerIterator&, SField::ref name);

    STAmount (SField::ref name, Issue const& issue,
              std::uint64_t val, int off, bool isNat, bool negative)
        : SerializedType (name), mIssue(issue),  mValue (val),
          mOffset (off), mIsNative (isNat), mIsNegative (negative)
    {
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

inline bool isXRP(STAmount const& amount)
{
    return isXRP (amount.issue().currency);
}

// VFALCO TODO Make static member accessors for these in STAmount
extern const STAmount saZero;
extern const STAmount saOne;

} // ripple

#endif
