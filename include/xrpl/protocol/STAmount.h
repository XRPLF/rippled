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

#ifndef XRPL_PROTOCOL_STAMOUNT_H_INCLUDED
#define XRPL_PROTOCOL_STAMOUNT_H_INCLUDED

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/LocalValue.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTAmount.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/json_get_or_throw.h>

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
class STAmount final : public STBase, public CountedObject<STAmount>
{
public:
    using mantissa_type = std::uint64_t;
    using exponent_type = int;
    using rep = std::pair<mantissa_type, exponent_type>;

private:
    Asset mAsset;
    mantissa_type mValue;
    exponent_type mOffset;
    bool mIsNegative;

public:
    using value_type = STAmount;

    static int const cMinOffset = -96;
    static int const cMaxOffset = 80;

    // Maximum native value supported by the code
    static std::uint64_t const cMinValue = 1000000000000000ull;
    static std::uint64_t const cMaxValue = 9999999999999999ull;
    static std::uint64_t const cMaxNative = 9000000000000000000ull;

    // Max native value on network.
    static std::uint64_t const cMaxNativeN = 100000000000000000ull;
    static std::uint64_t const cIssuedCurrency = 0x8000000000000000ull;
    static std::uint64_t const cPositive = 0x4000000000000000ull;
    static std::uint64_t const cMPToken = 0x2000000000000000ull;
    static std::uint64_t const cValueMask = ~(cPositive | cMPToken);

    static std::uint64_t const uRateOne;

    //--------------------------------------------------------------------------
    STAmount(SerialIter& sit, SField const& name);

    struct unchecked
    {
        explicit unchecked() = default;
    };

    // Do not call canonicalize
    template <AssetType A>
    STAmount(
        SField const& name,
        A const& asset,
        mantissa_type mantissa,
        exponent_type exponent,
        bool negative,
        unchecked);

    template <AssetType A>
    STAmount(
        A const& asset,
        mantissa_type mantissa,
        exponent_type exponent,
        bool negative,
        unchecked);

    // Call canonicalize
    template <AssetType A>
    STAmount(
        SField const& name,
        A const& asset,
        mantissa_type mantissa = 0,
        exponent_type exponent = 0,
        bool negative = false);

    STAmount(SField const& name, std::int64_t mantissa);

    STAmount(
        SField const& name,
        std::uint64_t mantissa = 0,
        bool negative = false);

    explicit STAmount(std::uint64_t mantissa = 0, bool negative = false);

    explicit STAmount(SField const& name, STAmount const& amt);

    template <AssetType A>
    STAmount(
        A const& asset,
        std::uint64_t mantissa = 0,
        int exponent = 0,
        bool negative = false)
        : mAsset(asset)
        , mValue(mantissa)
        , mOffset(exponent)
        , mIsNegative(negative)
    {
        canonicalize();
    }

    // VFALCO Is this needed when we have the previous signature?
    template <AssetType A>
    STAmount(
        A const& asset,
        std::uint32_t mantissa,
        int exponent = 0,
        bool negative = false);

    template <AssetType A>
    STAmount(A const& asset, std::int64_t mantissa, int exponent = 0);

    template <AssetType A>
    STAmount(A const& asset, int mantissa, int exponent = 0);

    template <AssetType A>
    STAmount(A const& asset, Number const& number)
        : STAmount(asset, number.mantissa(), number.exponent())
    {
    }

    // Legacy support for new-style amounts
    STAmount(IOUAmount const& amount, Issue const& issue);
    STAmount(XRPAmount const& amount);
    STAmount(MPTAmount const& amount, MPTIssue const& mptIssue);
    operator Number() const;

    //--------------------------------------------------------------------------
    //
    // Observers
    //
    //--------------------------------------------------------------------------

    int
    exponent() const noexcept;

    bool
    native() const noexcept;

    template <ValidIssueType TIss>
    constexpr bool
    holds() const noexcept;

    bool
    negative() const noexcept;

    std::uint64_t
    mantissa() const noexcept;

    Asset const&
    asset() const;

    template <ValidIssueType TIss>
    constexpr TIss const&
    get() const;

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

    explicit
    operator bool() const noexcept;

    STAmount&
    operator+=(STAmount const&);
    STAmount&
    operator-=(STAmount const&);

    STAmount& operator=(beast::Zero);

    STAmount&
    operator=(XRPAmount const& amount);

    STAmount&
    operator=(Number const&);

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
    clear(Asset const& asset);

    void
    setIssuer(AccountID const& uIssuer);

    /** Set the Issue for this amount. */
    void
    setIssue(Asset const& asset);

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

    Json::Value getJson(JsonOptions = JsonOptions::none) const override;

    void
    add(Serializer& s) const override;

    bool
    isEquivalent(STBase const& t) const override;

    bool
    isDefault() const override;

    XRPAmount
    xrp() const;
    IOUAmount
    iou() const;
    MPTAmount
    mpt() const;

private:
    static std::unique_ptr<STAmount>
    construct(SerialIter&, SField const& name);

    void
    set(std::int64_t v);
    void
    canonicalize();

    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    STAmount&
    operator=(IOUAmount const& iou);

    friend class detail::STVar;

    friend STAmount
    operator+(STAmount const& v1, STAmount const& v2);
};

template <AssetType A>
STAmount::STAmount(
    SField const& name,
    A const& asset,
    mantissa_type mantissa,
    exponent_type exponent,
    bool negative,
    unchecked)
    : STBase(name)
    , mAsset(asset)
    , mValue(mantissa)
    , mOffset(exponent)
    , mIsNegative(negative)
{
}

template <AssetType A>
STAmount::STAmount(
    A const& asset,
    mantissa_type mantissa,
    exponent_type exponent,
    bool negative,
    unchecked)
    : mAsset(asset), mValue(mantissa), mOffset(exponent), mIsNegative(negative)
{
}

template <AssetType A>
STAmount::STAmount(
    SField const& name,
    A const& asset,
    std::uint64_t mantissa,
    int exponent,
    bool negative)
    : STBase(name)
    , mAsset(asset)
    , mValue(mantissa)
    , mOffset(exponent)
    , mIsNegative(negative)
{
    // mValue is uint64, but needs to fit in the range of int64
    XRPL_ASSERT(
        mValue <= std::numeric_limits<std::int64_t>::max(),
        "ripple::STAmount::STAmount(SField, A, std::uint64_t, int, bool) : "
        "maximum mantissa input");
    canonicalize();
}

template <AssetType A>
STAmount::STAmount(A const& asset, std::int64_t mantissa, int exponent)
    : mAsset(asset), mOffset(exponent)
{
    set(mantissa);
    canonicalize();
}

template <AssetType A>
STAmount::STAmount(
    A const& asset,
    std::uint32_t mantissa,
    int exponent,
    bool negative)
    : STAmount(asset, safe_cast<std::uint64_t>(mantissa), exponent, negative)
{
}

template <AssetType A>
STAmount::STAmount(A const& asset, int mantissa, int exponent)
    : STAmount(asset, safe_cast<std::int64_t>(mantissa), exponent)
{
}

// Legacy support for new-style amounts
inline STAmount::STAmount(IOUAmount const& amount, Issue const& issue)
    : mAsset(issue)
    , mOffset(amount.exponent())
    , mIsNegative(amount < beast::zero)
{
    if (mIsNegative)
        mValue = unsafe_cast<std::uint64_t>(-amount.mantissa());
    else
        mValue = unsafe_cast<std::uint64_t>(amount.mantissa());

    canonicalize();
}

inline STAmount::STAmount(MPTAmount const& amount, MPTIssue const& mptIssue)
    : mAsset(mptIssue), mOffset(0), mIsNegative(amount < beast::zero)
{
    if (mIsNegative)
        mValue = unsafe_cast<std::uint64_t>(-amount.value());
    else
        mValue = unsafe_cast<std::uint64_t>(amount.value());

    canonicalize();
}

//------------------------------------------------------------------------------
//
// Creation
//
//------------------------------------------------------------------------------

// VFALCO TODO The parameter type should be Quality not uint64_t
STAmount
amountFromQuality(std::uint64_t rate);

STAmount
amountFromString(Asset const& asset, std::string const& amount);

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
    return mAsset.native();
}

template <ValidIssueType TIss>
constexpr bool
STAmount::holds() const noexcept
{
    return mAsset.holds<TIss>();
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

inline Asset const&
STAmount::asset() const
{
    return mAsset;
}

template <ValidIssueType TIss>
constexpr TIss const&
STAmount::get() const
{
    return mAsset.get<TIss>();
}

inline Issue const&
STAmount::issue() const
{
    return get<Issue>();
}

inline Currency const&
STAmount::getCurrency() const
{
    return mAsset.get<Issue>().currency;
}

inline AccountID const&
STAmount::getIssuer() const
{
    return mAsset.getIssuer();
}

inline int
STAmount::signum() const noexcept
{
    return mValue ? (mIsNegative ? -1 : 1) : 0;
}

inline STAmount
STAmount::zeroed() const
{
    return STAmount(mAsset);
}

inline STAmount::operator bool() const noexcept
{
    return *this != beast::zero;
}

inline STAmount::operator Number() const
{
    if (native())
        return xrp();
    if (mAsset.holds<MPTIssue>())
        return mpt();
    return iou();
}

inline STAmount&
STAmount::operator=(beast::Zero)
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

inline STAmount&
STAmount::operator=(Number const& number)
{
    mIsNegative = number.mantissa() < 0;
    mValue = mIsNegative ? -number.mantissa() : number.mantissa();
    mOffset = number.exponent();
    canonicalize();
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
    mOffset = native() ? 0 : -100;
    mValue = 0;
    mIsNegative = false;
}

inline void
STAmount::clear(Asset const& asset)
{
    setIssue(asset);
    clear();
}

inline void
STAmount::setIssuer(AccountID const& uIssuer)
{
    mAsset.get<Issue>().account = uIssuer;
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
divide(STAmount const& v1, STAmount const& v2, Asset const& asset);

STAmount
multiply(STAmount const& v1, STAmount const& v2, Asset const& asset);

// multiply rounding result in specified direction
STAmount
mulRound(
    STAmount const& v1,
    STAmount const& v2,
    Asset const& asset,
    bool roundUp);

// multiply following the rounding directions more precisely.
STAmount
mulRoundStrict(
    STAmount const& v1,
    STAmount const& v2,
    Asset const& asset,
    bool roundUp);

// divide rounding result in specified direction
STAmount
divRound(
    STAmount const& v1,
    STAmount const& v2,
    Asset const& asset,
    bool roundUp);

// divide following the rounding directions more precisely.
STAmount
divRoundStrict(
    STAmount const& v1,
    STAmount const& v2,
    Asset const& asset,
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
    return amount.native();
}

bool
canAdd(STAmount const& amt1, STAmount const& amt2);

bool
canSubtract(STAmount const& amt1, STAmount const& amt2);

}  // namespace ripple

//------------------------------------------------------------------------------
namespace Json {
template <>
inline ripple::STAmount
getOrThrow(Json::Value const& v, ripple::SField const& field)
{
    using namespace ripple;
    Json::StaticString const& key = field.getJsonName();
    if (!v.isMember(key))
        Throw<JsonMissingKeyError>(key);
    Json::Value const& inner = v[key];
    return amountFromJson(field, inner);
}
}  // namespace Json
#endif
