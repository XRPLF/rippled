//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2012-2015 Ripple Labs Inc.

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

#ifndef RIPPLE_TEST_JTX_AMOUNT_H_INCLUDED
#define RIPPLE_TEST_JTX_AMOUNT_H_INCLUDED

#include <test/jtx/Account.h>
#include <test/jtx/tags.h>

#include <xrpl/basics/contract.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/Units.h>

#include <cstdint>
#include <ostream>
#include <string>
#include <type_traits>

namespace ripple {
namespace detail {

struct epsilon_multiple
{
    std::size_t n;
};

}  // namespace detail

namespace test {
namespace jtx {

/*

The decision was made to accept amounts of drops and XRP
using an int type, since the range of XRP is 100 billion
and having both signed and unsigned overloads creates
tricky code leading to overload resolution ambiguities.

*/

struct AnyAmount;

// Represents "no amount" of a currency
// This is distinct from zero or a balance.
// For example, no USD means the trust line
// doesn't even exist. Using this in an
// inappropriate context will generate a
// compile error.
//
struct None
{
    Asset asset;
};

//------------------------------------------------------------------------------

// This value is also defined in SystemParameters.h. It's
// duplicated here to catch any possible future errors that
// could change that value (however unlikely).
constexpr XRPAmount dropsPerXRP{1'000'000};

/** Represents an XRP or IOU quantity
    This customizes the string conversion and supports
    XRP conversions from integer and floating point.
*/
struct PrettyAmount
{
private:
    // VFALCO TODO should be Amount
    STAmount amount_;
    std::string name_;

public:
    PrettyAmount() = default;
    PrettyAmount(PrettyAmount const&) = default;
    PrettyAmount&
    operator=(PrettyAmount const&) = default;

    PrettyAmount(STAmount const& amount, std::string const& name)
        : amount_(amount), name_(name)
    {
    }

    /** drops */
    template <class T>
    PrettyAmount(
        T v,
        std::enable_if_t<
            sizeof(T) >= sizeof(int) && std::is_integral_v<T> &&
            std::is_signed_v<T>>* = nullptr)
        : amount_((v > 0) ? v : -v, v < 0)
    {
    }

    /** drops */
    template <class T>
    PrettyAmount(
        T v,
        std::enable_if_t<sizeof(T) >= sizeof(int) && std::is_unsigned_v<T>>* =
            nullptr)
        : amount_(v)
    {
    }

    /** drops */
    PrettyAmount(XRPAmount v) : amount_(v)
    {
    }

    std::string const&
    name() const
    {
        return name_;
    }

    STAmount const&
    value() const
    {
        return amount_;
    }

    Number
    number() const
    {
        return amount_;
    }

    inline int
    signum() const
    {
        return amount_.signum();
    }

    operator STAmount const&() const
    {
        return amount_;
    }

    operator AnyAmount() const;

    operator Json::Value() const
    {
        return to_json(value());
    }
};

inline bool
operator==(PrettyAmount const& lhs, PrettyAmount const& rhs)
{
    return lhs.value() == rhs.value();
}

inline bool
operator!=(PrettyAmount const& lhs, PrettyAmount const& rhs)
{
    return !operator==(lhs, rhs);
}

std::ostream&
operator<<(std::ostream& os, PrettyAmount const& amount);

struct PrettyAsset
{
private:
    Asset asset_;
    std::uint32_t scale_;

public:
    template <typename A>
        requires std::convertible_to<A, Asset>
    PrettyAsset(A const& asset, std::uint32_t scale = 1)
        : PrettyAsset{Asset{asset}, scale}
    {
    }

    PrettyAsset(Asset const& asset, std::uint32_t scale = 1)
        : asset_(asset), scale_(scale)
    {
    }

    Asset const&
    raw() const
    {
        return asset_;
    }

    operator Asset const&() const
    {
        return asset_;
    }

    operator Json::Value() const
    {
        return to_json(asset_);
    }

    template <std::integral T>
    PrettyAmount
    operator()(T v, Number::rounding_mode rounding = Number::getround()) const
    {
        return operator()(Number(v), rounding);
    }

    PrettyAmount
    operator()(Number v, Number::rounding_mode rounding = Number::getround())
        const
    {
        NumberRoundModeGuard mg(rounding);
        STAmount amount{asset_, v * scale_};
        return {amount, ""};
    }

    None
    operator()(none_t) const
    {
        return {asset_};
    }
};
//------------------------------------------------------------------------------

// Specifies an order book
struct BookSpec
{
    AccountID account;
    ripple::Currency currency;

    BookSpec(AccountID const& account_, ripple::Currency const& currency_)
        : account(account_), currency(currency_)
    {
    }
};

//------------------------------------------------------------------------------

struct XRP_t
{
    /** Implicit conversion to Issue.

        This allows passing XRP where
        an Issue is expected.
    */
    operator Issue() const
    {
        return xrpIssue();
    }

    /** Returns an amount of XRP as PrettyAmount,
        which is trivially convertable to STAmount

        @param v The number of XRP (not drops)
    */
    /** @{ */
    template <class T, class = std::enable_if_t<std::is_integral_v<T>>>
    PrettyAmount
    operator()(T v) const
    {
        using TOut = std::
            conditional_t<std::is_signed_v<T>, std::int64_t, std::uint64_t>;
        return {TOut{v} * dropsPerXRP};
    }

    PrettyAmount
    operator()(double v) const
    {
        auto const c = dropsPerXRP.drops();
        if (v >= 0)
        {
            auto const d = std::uint64_t(std::round(v * c));
            if (double(d) / c != v)
                Throw<std::domain_error>("unrepresentable");
            return {d};
        }
        auto const d = std::int64_t(std::round(v * c));
        if (double(d) / c != v)
            Throw<std::domain_error>("unrepresentable");
        return {d};
    }
    /** @} */

    /** Returns None-of-XRP */
    None
    operator()(none_t) const
    {
        return {xrpIssue()};
    }

    friend BookSpec
    operator~(XRP_t const&)
    {
        return BookSpec(xrpAccount(), xrpCurrency());
    }
};

/** Converts to XRP Issue or STAmount.

    Examples:
        XRP         Converts to the XRP Issue
        XRP(10)     Returns STAmount of 10 XRP
*/
extern XRP_t const XRP;

/** Returns an XRP PrettyAmount, which is trivially convertible to STAmount.

    Example:
        drops(10)   Returns PrettyAmount of 10 drops
*/
template <class Integer, class = std::enable_if_t<std::is_integral_v<Integer>>>
PrettyAmount
drops(Integer i)
{
    return {i};
}

/** Returns an XRP PrettyAmount, which is trivially convertible to STAmount.

Example:
drops(view->fee().basefee)   Returns PrettyAmount of 10 drops
*/
inline PrettyAmount
drops(XRPAmount i)
{
    return {i};
}

//------------------------------------------------------------------------------

// The smallest possible IOU STAmount
struct epsilon_t
{
    epsilon_t()
    {
    }

    detail::epsilon_multiple
    operator()(std::size_t n) const
    {
        return {n};
    }
};

static epsilon_t const epsilon;

/** Converts to IOU Issue or STAmount.

    Examples:
        IOU         Converts to the underlying Issue
        IOU(10)     Returns STAmount of 10 of
                        the underlying Issue.
*/
class IOU
{
public:
    Account account;
    ripple::Currency currency;

    IOU(Account const& account_, ripple::Currency const& currency_)
        : account(account_), currency(currency_)
    {
    }

    Issue
    issue() const
    {
        return {currency, account.id()};
    }
    Asset
    asset() const
    {
        return issue();
    }

    /** Implicit conversion to Issue or Asset.

        This allows passing an IOU
        value where an Issue or Asset is expected.
    */
    operator Issue() const
    {
        return issue();
    }
    operator PrettyAsset() const
    {
        return asset();
    }

    template <
        class T,
        class = std::enable_if_t<
            sizeof(T) >= sizeof(int) && std::is_arithmetic<T>::value>>
    PrettyAmount
    operator()(T v) const
    {
        // VFALCO NOTE Should throw if the
        //             representation of v is not exact.
        return {amountFromString(issue(), std::to_string(v)), account.name()};
    }

    PrettyAmount
    operator()(epsilon_t) const;
    PrettyAmount
    operator()(detail::epsilon_multiple) const;

    // VFALCO TODO
    // STAmount operator()(char const* s) const;

    /** Returns None-of-Issue */
    None
    operator()(none_t) const
    {
        return {issue()};
    }

    friend BookSpec
    operator~(IOU const& iou)
    {
        return BookSpec(iou.account.id(), iou.currency);
    }
};

std::ostream&
operator<<(std::ostream& os, IOU const& iou);

//------------------------------------------------------------------------------

/** Converts to MPT Issue or STAmount.

    Examples:
        MPT         Converts to the underlying Issue
        MPT(10)     Returns STAmount of 10 of
                        the underlying MPT
*/
class MPT
{
public:
    std::string name;
    ripple::MPTID issuanceID;

    MPT(std::string const& n, ripple::MPTID const& issuanceID_)
        : name(n), issuanceID(issuanceID_)
    {
    }

    ripple::MPTID const&
    mpt() const
    {
        return issuanceID;
    }

    /** Explicit conversion to MPTIssue or asset.
     */
    ripple::MPTIssue
    mptIssue() const
    {
        return MPTIssue{issuanceID};
    }
    Asset
    asset() const
    {
        return mptIssue();
    }

    /** Implicit conversion to MPTIssue or asset.

        This allows passing an MPT
        value where an MPTIssue is expected.
    */
    operator ripple::MPTIssue() const
    {
        return mptIssue();
    }

    operator PrettyAsset() const
    {
        return asset();
    }

    template <class T>
        requires(sizeof(T) >= sizeof(int) && std::is_arithmetic_v<T>)
    PrettyAmount
    operator()(T v) const
    {
        return {amountFromString(mpt(), std::to_string(v)), name};
    }

    PrettyAmount
    operator()(epsilon_t) const;
    PrettyAmount
    operator()(detail::epsilon_multiple) const;

    /** Returns None-of-Issue */
    None
    operator()(none_t) const
    {
        return {mptIssue()};
    }

    friend BookSpec
    operator~(MPT const& mpt)
    {
        assert(false);
        Throw<std::logic_error>("MPT is not supported");
        return BookSpec{beast::zero, noCurrency()};
    }
};

std::ostream&
operator<<(std::ostream& os, MPT const& mpt);

//------------------------------------------------------------------------------

struct any_t
{
    inline AnyAmount
    operator()(STAmount const& sta) const;
};

/** Amount specifier with an option for any issuer. */
struct AnyAmount
{
    bool is_any;
    STAmount value;

    AnyAmount() = delete;
    AnyAmount(AnyAmount const&) = default;
    AnyAmount&
    operator=(AnyAmount const&) = default;

    AnyAmount(STAmount const& amount) : is_any(false), value(amount)
    {
    }

    AnyAmount(STAmount const& amount, any_t const*)
        : is_any(true), value(amount)
    {
    }

    // Reset the issue to a specific account
    void
    to(AccountID const& id)
    {
        if (!is_any)
            return;
        value.setIssuer(id);
    }
};

inline AnyAmount
any_t::operator()(STAmount const& sta) const
{
    return AnyAmount(sta, this);
}

/** Returns an amount representing "any issuer"
    @note With respect to what the recipient will accept
*/
extern any_t const any;

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
