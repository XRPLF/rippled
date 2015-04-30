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

#ifndef RIPPLE_PROTOCOL_STAMOUNT_H_INCLUDED
#define RIPPLE_PROTOCOL_STAMOUNT_H_INCLUDED

#include <ripple/protocol/SField.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/STBase.h>
#include <ripple/protocol/Issue.h>
#include <beast/cxx14/memory.h> // <memory>

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
class STAmount
    : public STBase
{
public:
    typedef std::uint64_t mantissa_type;
    typedef int exponent_type;
    typedef std::pair <mantissa_type, exponent_type> rep;

private:
    Issue mIssue;
    mantissa_type mValue;
    exponent_type mOffset;
    bool mIsNative;      // A shorthand for isXRP(mIssue).
    bool mIsNegative;

public:
    static const int cMinOffset = -96;
    static const int cMaxOffset = 80;

    // Maximum native value supported by the code
    static const std::uint64_t cMinValue   = 1000000000000000ull;
    static const std::uint64_t cMaxValue   = 9999999999999999ull;
    static const std::uint64_t cMaxNative  = 9000000000000000000ull;

    // Max native value on network.
    static const std::uint64_t cMaxNativeN = 100000000000000000ull;
    static const std::uint64_t cNotNative  = 0x8000000000000000ull;
    static const std::uint64_t cPosNative  = 0x4000000000000000ull;

    static std::uint64_t const uRateOne;

    //--------------------------------------------------------------------------

    struct unchecked { };

    STAmount(SerialIter& sit, SField const& name);

    // Calls canonicalize
    STAmount (SField const& name, Issue const& issue,
        mantissa_type mantissa, exponent_type exponent,
            bool native, bool negative);

    // Does not call canonicalize
    STAmount (SField const& name, Issue const& issue,
        mantissa_type mantissa, exponent_type exponent,
            bool native, bool negative, unchecked);

    STAmount (SField const& name, std::int64_t mantissa);

    STAmount (SField const& name,
        std::uint64_t mantissa = 0, bool negative = false);

    STAmount (SField const& name, Issue const& issue,
        std::uint64_t mantissa = 0, int exponent = 0, bool negative = false);

    STAmount (std::uint64_t mantissa = 0, bool negative = false);

    STAmount (Issue const& issue,
        std::uint64_t mantissa = 0, int exponent = 0, bool negative = false);

    // VFALCO Is this needed when we have the previous signature?
    STAmount (Issue const& issue,
        std::uint32_t mantissa, int exponent = 0, bool negative = false);

    STAmount (Issue const& issue, std::int64_t mantissa, int exponent = 0);

    STAmount (Issue const& issue, int mantissa, int exponent = 0);

    STBase*
    copy (std::size_t n, void* buf) const override
    {
        return emplace(n, buf, *this);
    }

    STBase*
    move (std::size_t n, void* buf) override
    {
        return emplace(n, buf, std::move(*this));
    }

    //--------------------------------------------------------------------------

private:
    static
    std::unique_ptr<STAmount>
    construct (SerialIter&, SField const& name);

    void
    setSNValue (std::int64_t);

public:
    static
    STAmount
    createFromInt64 (SField const& n, std::int64_t v);

    //--------------------------------------------------------------------------
    //
    // Observers
    //
    //--------------------------------------------------------------------------

    int exponent() const noexcept { return mOffset; }
    bool native() const noexcept { return mIsNative; }
    bool negative() const noexcept { return mIsNegative; }
    std::uint64_t mantissa() const noexcept { return mValue; }
    Issue const& issue() const { return mIssue; }

    // These three are deprecated
    Currency const& getCurrency() const { return mIssue.currency; }
    Account const& getIssuer() const { return mIssue.account; }
    bool isNative() const { return mIsNative; }

    int
    signum() const noexcept
    {
        return mValue ? (mIsNegative ? -1 : 1) : 0;
    }

    /** Returns a zero value with the same issuer and currency. */
    STAmount
    zeroed() const
    {
        // TODO(tom): what does this next comment mean here?
        // See https://ripplelabs.atlassian.net/browse/WC-1847?jql=
        return STAmount (mIssue);
    }

    // VFALCO TODO This can be a free function or just call the
    //             member on the issue.
    std::string
    getHumanCurrency() const;

    void
    setJson (Json::Value&) const;

    //--------------------------------------------------------------------------
    //
    // Operators
    //
    //--------------------------------------------------------------------------

    explicit operator bool() const noexcept
    {
        return *this != zero;
    }

    bool isComparable (STAmount const&) const;
    void throwComparable (STAmount const&) const;

    STAmount& operator+= (STAmount const&);
    STAmount& operator-= (STAmount const&);

    STAmount& operator= (beast::Zero)
    {
        clear();
        return *this;
    }

    friend STAmount operator+ (STAmount const& v1, STAmount const& v2);
    friend STAmount operator- (STAmount const& v1, STAmount const& v2);

    //--------------------------------------------------------------------------
    //
    // Modification
    //
    //--------------------------------------------------------------------------

    // VFALCO TODO Remove this, it is only called from the unit test
    void roundSelf();

    void negate()
    {
        if (*this != zero)
            mIsNegative = !mIsNegative;
    }

    void clear()
    {
        // VFALCO: Why -100?
        mOffset = mIsNative ? 0 : -100;
        mValue = 0;
        mIsNegative = false;
    }

    // Zero while copying currency and issuer.
    void clear (STAmount const& saTmpl)
    {
        clear (saTmpl.mIssue);
    }

    void clear (Issue const& issue)
    {
        setIssue(issue);
        clear();
    }

    void setIssuer (Account const& uIssuer)
    {
        mIssue.account = uIssuer;
        setIssue(mIssue);
    }

    /** Set the Issue for this amount and update mIsNative. */
    void setIssue (Issue const& issue);

    // VFALCO TODO Rename to setValueOnly (it only sets mantissa and exponent)
    //             Make this private
    bool setValue (std::string const& sAmount);

    //--------------------------------------------------------------------------
    //
    // STBase
    //
    //--------------------------------------------------------------------------

    SerializedTypeID
    getSType() const override
    {
        return STI_AMOUNT;
    }

    std::string
    getFullText() const override;

    std::string
    getText() const override;

    Json::Value
    getJson (int) const override;

    void
    add (Serializer& s) const override;

    bool
    isEquivalent (const STBase& t) const override;

    bool
    isDefault() const override
    {
        return (mValue == 0) && mIsNative;
    }

    void canonicalize();
    void set (std::int64_t v);
};

//------------------------------------------------------------------------------
//
// Creation
//
//------------------------------------------------------------------------------

// VFALCO TODO The parameter type should be Quality not uint64_t
STAmount
amountFromQuality (std::uint64_t rate);

STAmount
amountFromJson (SField const& name, Json::Value const& v);

STAmount
amountFromRate (std::uint64_t uRate);

bool
amountFromJsonNoThrow (STAmount& result, Json::Value const& jvSource);

//------------------------------------------------------------------------------
//
// Observers
//
//------------------------------------------------------------------------------

inline
bool
isLegalNet (STAmount const& value)
{
    return ! value.native() || (value.mantissa() <= STAmount::cMaxNativeN);
}

//------------------------------------------------------------------------------
//
// Operators
//
//------------------------------------------------------------------------------

bool operator== (STAmount const& lhs, STAmount const& rhs);
bool operator!= (STAmount const& lhs, STAmount const& rhs);
bool operator<  (STAmount const& lhs, STAmount const& rhs);
bool operator>  (STAmount const& lhs, STAmount const& rhs);
bool operator<= (STAmount const& lhs, STAmount const& rhs);
bool operator>= (STAmount const& lhs, STAmount const& rhs);

// native currency only
bool operator<  (STAmount const& lhs, std::uint64_t rhs);
bool operator>  (STAmount const& lhs, std::uint64_t rhs);
bool operator<= (STAmount const& lhs, std::uint64_t rhs);
bool operator>= (STAmount const& lhs, std::uint64_t rhs);

STAmount operator+ (STAmount const& lhs, std::uint64_t rhs);
STAmount operator- (STAmount const& lhs, std::uint64_t rhs);
STAmount operator- (STAmount const& value);

//------------------------------------------------------------------------------
//
// Arithmetic
//
//------------------------------------------------------------------------------

STAmount
divide (STAmount const& v1, STAmount const& v2, Issue const& issue);

STAmount
multiply (STAmount const& v1, STAmount const& v2, Issue const& issue);

// multiply, or divide rounding result in specified direction

STAmount
mulRound (STAmount const& v1, STAmount const& v2,
    Issue const& issue, bool roundUp);

STAmount
divRound (STAmount const& v1, STAmount const& v2,
    Issue const& issue, bool roundUp);

// Someone is offering X for Y, what is the rate?
// Rate: smaller is better, the taker wants the most out: in/out
// VFALCO TODO Return a Quality object
std::uint64_t
getRate (STAmount const& offerOut, STAmount const& offerIn);

// When the currency is XRP, the value in raw unsigned units.
std::uint64_t
getNValue(STAmount const& amount);

//------------------------------------------------------------------------------

inline bool isXRP(STAmount const& amount)
{
    return isXRP (amount.issue().currency);
}

// VFALCO TODO Make static member accessors for these in STAmount
extern const STAmount saZero;
extern const STAmount saOne;

} // ripple

#endif
