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

#include <ripple/basics/IOUAmount.h>
#include <ripple/basics/LocalValue.h>
#include <ripple/basics/XRPAmount.h>
#include <ripple/protocol/Issue.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/STBase.h>
#include <ripple/protocol/Serializer.h>

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
class STAmount : public STBase
{
public:
    using mantissa_type = std::uint64_t;
    using exponent_type = int;
    using rep = std::pair<mantissa_type, exponent_type>;

private:
    Issue mIssue;
    mantissa_type mValue;
    exponent_type mOffset;
    bool mIsNative;  // A shorthand for isXRP(mIssue).
    bool mIsNegative;

public:
    using value_type = STAmount;

    static const int cMinOffset = -96;
    static const int cMaxOffset = 80;

    // Maximum native value supported by the code
    static const std::uint64_t cMinValue = 1000000000000000ull;
    static const std::uint64_t cMaxValue = 9999999999999999ull;
    static const std::uint64_t cMaxNative = 9000000000000000000ull;

    // Max native value on network.
    static const std::uint64_t cMaxNativeN = 100000000000000000ull;
    static const std::uint64_t cNotNative = 0x8000000000000000ull;
    static const std::uint64_t cPosNative = 0x4000000000000000ull;

    static std::uint64_t const uRateOne;

    //--------------------------------------------------------------------------
    STAmount(SerialIter& sit, SField const& name);

    struct unchecked
    {
        explicit unchecked() = default;
    };

    // Do not call canonicalize
    STAmount(
        SField const& name,
        Issue const& issue,
        mantissa_type mantissa,
        exponent_type exponent,
        bool native,
        bool negative,
        unchecked);

    STAmount(
        Issue const& issue,
        mantissa_type mantissa,
        exponent_type exponent,
        bool native,
        bool negative,
        unchecked);

    // Call canonicalize
    STAmount(
        SField const& name,
        Issue const& issue,
        mantissa_type mantissa,
        exponent_type exponent,
        bool native,
        bool negative);

    STAmount(SField const& name, std::int64_t mantissa);

    STAmount(
        SField const& name,
        std::uint64_t mantissa = 0,
        bool negative = false);

    STAmount(
        SField const& name,
        Issue const& issue,
        std::uint64_t mantissa = 0,
        int exponent = 0,
        bool negative = false);

    explicit STAmount(std::uint64_t mantissa = 0, bool negative = false);

    STAmount(
        Issue const& issue,
        std::uint64_t mantissa = 0,
        int exponent = 0,
        bool negative = false);

    // VFALCO Is this needed when we have the previous signature?
    STAmount(
        Issue const& issue,
        std::uint32_t mantissa,
        int exponent = 0,
        bool negative = false);

    STAmount(Issue const& issue, std::int64_t mantissa, int exponent = 0);

    STAmount(Issue const& issue, int mantissa, int exponent = 0);

    // Legacy support for new-style amounts
    STAmount(IOUAmount const& amount, Issue const& issue);
    STAmount(XRPAmount const& amount);

    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    //--------------------------------------------------------------------------
    //
    // Observers
    //
    //--------------------------------------------------------------------------

    int
    exponent() const noexcept;

    bool
    native() const noexcept;

    bool
    negative() const noexcept;

    std::uint64_t
    mantissa() const noexcept;

    Issue const&
    issue() const;

    // These three are deprecated
    Currency const&
    getCurrency() const;

    AccountID const&
    getIssuer() const;

    int
    signum() const noexcept;

    /** Returns a zero value with the same issuer and currency. */
    STAmount
    zeroed() const;

    void
    setJson(Json::Value&) const;

    STAmount const&
    value() const noexcept;

    //--------------------------------------------------------------------------
    //
    // Operators
    //
    //--------------------------------------------------------------------------

    explicit operator bool() const noexcept;

    STAmount&
    operator+=(STAmount const&);
    STAmount&
    operator-=(STAmount const&);

    STAmount& operator=(beast::Zero);

    STAmount&
    operator=(XRPAmount const& amount);

    //--------------------------------------------------------------------------
    //
    // Modification
    //
    //--------------------------------------------------------------------------

    void
    negate();

    void
    clear();

    // Zero while copying currency and issuer.
    void
    clear(STAmount const& saTmpl);

    void
    clear(Issue const& issue);

    void
    setIssuer(AccountID const& uIssuer);

    /** Set the Issue for this amount and update mIsNative. */
    void
    setIssue(Issue const& issue);

    //--------------------------------------------------------------------------
    //
    // STBase
    //
    //--------------------------------------------------------------------------

    SerializedTypeID
    getSType() const override;

    std::string
    getFullText() const override;

    std::string
    getText() const override;

    Json::Value getJson(JsonOptions) const override;

    void
    add(Serializer& s) const override;

    bool
    isEquivalent(const STBase& t) const override;

    bool
    isDefault() const override;

    XRPAmount
    xrp() const;
    IOUAmount
    iou() const;

private:
    static std::unique_ptr<STAmount>
    construct(SerialIter&, SField const& name);

    void
    set(std::int64_t v);
    void
    canonicalize();
};

//------------------------------------------------------------------------------
//
// Creation
//
//------------------------------------------------------------------------------

// VFALCO TODO The parameter type should be Quality not uint64_t
STAmount
amountFromQuality(std::uint64_t rate);

STAmount
amountFromString(Issue const& issue, std::string const& amount);

STAmount
amountFromJson(SField const& name, Json::Value const& v);

bool
amountFromJsonNoThrow(STAmount& result, Json::Value const& jvSource);

// IOUAmount and XRPAmount define toSTAmount, defining this
// trivial conversion here makes writing generic code easier
inline STAmount const&
toSTAmount(STAmount const& a)
{
    return a;
}

//------------------------------------------------------------------------------
//
// Observers
//
//------------------------------------------------------------------------------

inline int
STAmount::exponent() const noexcept
{
    return mOffset;
}

inline bool
STAmount::native() const noexcept
{
    return mIsNative;
}

inline bool
STAmount::negative() const noexcept
{
    return mIsNegative;
}

inline std::uint64_t
STAmount::mantissa() const noexcept
{
    return mValue;
}

inline Issue const&
STAmount::issue() const
{
    return mIssue;
}

inline Currency const&
STAmount::getCurrency() const
{
    return mIssue.currency;
}

inline AccountID const&
STAmount::getIssuer() const
{
    return mIssue.account;
}

inline int
STAmount::signum() const noexcept
{
    return mValue ? (mIsNegative ? -1 : 1) : 0;
}

inline STAmount
STAmount::zeroed() const
{
    return STAmount(mIssue);
}

inline STAmount::operator bool() const noexcept
{
    return *this != beast::zero;
}

inline STAmount& STAmount::operator=(beast::Zero)
{
    clear();
    return *this;
}

inline STAmount&
STAmount::operator=(XRPAmount const& amount)
{
    *this = STAmount(amount);
    return *this;
}

inline void
STAmount::negate()
{
    if (*this != beast::zero)
        mIsNegative = !mIsNegative;
}

inline void
STAmount::clear()
{
    // The -100 is used to allow 0 to sort less than a small positive values
    // which have a negative exponent.
    mOffset = mIsNative ? 0 : -100;
    mValue = 0;
    mIsNegative = false;
}

// Zero while copying currency and issuer.
inline void
STAmount::clear(STAmount const& saTmpl)
{
    clear(saTmpl.mIssue);
}

inline void
STAmount::clear(Issue const& issue)
{
    setIssue(issue);
    clear();
}

inline void
STAmount::setIssuer(AccountID const& uIssuer)
{
    mIssue.account = uIssuer;
    setIssue(mIssue);
}

inline STAmount const&
STAmount::value() const noexcept
{
    return *this;
}

inline bool
isLegalNet(STAmount const& value)
{
    return !value.native() || (value.mantissa() <= STAmount::cMaxNativeN);
}

//------------------------------------------------------------------------------
//
// Operators
//
//------------------------------------------------------------------------------

bool
operator==(STAmount const& lhs, STAmount const& rhs);
bool
operator<(STAmount const& lhs, STAmount const& rhs);

inline bool
operator!=(STAmount const& lhs, STAmount const& rhs)
{
    return !(lhs == rhs);
}

inline bool
operator>(STAmount const& lhs, STAmount const& rhs)
{
    return rhs < lhs;
}

inline bool
operator<=(STAmount const& lhs, STAmount const& rhs)
{
    return !(rhs < lhs);
}

inline bool
operator>=(STAmount const& lhs, STAmount const& rhs)
{
    return !(lhs < rhs);
}

STAmount
operator-(STAmount const& value);

//------------------------------------------------------------------------------
//
// Arithmetic
//
//------------------------------------------------------------------------------

STAmount
operator+(STAmount const& v1, STAmount const& v2);
STAmount
operator-(STAmount const& v1, STAmount const& v2);

STAmount
divide(STAmount const& v1, STAmount const& v2, Issue const& issue);

STAmount
multiply(STAmount const& v1, STAmount const& v2, Issue const& issue);

// multiply, or divide rounding result in specified direction
STAmount
mulRound(
    STAmount const& v1,
    STAmount const& v2,
    Issue const& issue,
    bool roundUp);

STAmount
divRound(
    STAmount const& v1,
    STAmount const& v2,
    Issue const& issue,
    bool roundUp);

// Someone is offering X for Y, what is the rate?
// Rate: smaller is better, the taker wants the most out: in/out
// VFALCO TODO Return a Quality object
std::uint64_t
getRate(STAmount const& offerOut, STAmount const& offerIn);

//------------------------------------------------------------------------------

inline bool
isXRP(STAmount const& amount)
{
    return isXRP(amount.issue().currency);
}

// Since `canonicalize` does not have access to a ledger, this is needed to put
// the low-level routine stAmountCanonicalize on an amendment switch. Only
// transactions need to use this switchover. Outside of a transaction it's safe
// to unconditionally use the new behavior.
extern LocalValue<bool> stAmountCanonicalizeSwitchover;

/** RAII class to set and restore the STAmount canonicalize switchover.
 */

class STAmountSO
{
public:
    explicit STAmountSO(bool v) : saved_(*stAmountCanonicalizeSwitchover)
    {
        *stAmountCanonicalizeSwitchover = v;
    }

    ~STAmountSO()
    {
        *stAmountCanonicalizeSwitchover = saved_;
    }

private:
    bool saved_;
};

}  // namespace ripple

#endif
