#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/tags.h>

#include <xrpl/basics/contract.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/Units.h>

#include <cstdint>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>

namespace xrpl {
namespace detail {

struct EpsilonMultiple
{
    std::size_t n;
};

}  // namespace detail

namespace test::jtx {

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
constexpr XRPAmount kJtxDropsPerXrp{1'000'000};

/** Represents an XRP, IOU, or MPT quantity
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

    PrettyAmount(STAmount amount, std::string name)
        : amount_(std::move(amount)), name_(std::move(name))
    {
    }

    /** drops */
    template <class T>
    PrettyAmount(
        T v,
        std::enable_if_t<
            sizeof(T) >= sizeof(int) && std::is_integral_v<T> && std::is_signed_v<T>>* = nullptr)
        : amount_((v > 0) ? v : -v, v < 0)
    {
    }

    /** drops */
    template <class T>
    PrettyAmount(
        T v,
        std::enable_if_t<sizeof(T) >= sizeof(int) && std::is_unsigned_v<T>>* = nullptr)
        : amount_(v)
    {
    }

    /** drops */
    PrettyAmount(XRPAmount v) : amount_(v)
    {
    }

    [[nodiscard]] std::string const&
    name() const
    {
        return name_;
    }

    [[nodiscard]] STAmount const&
    value() const
    {
        return amount_;
    }

    [[nodiscard]] Number
    number() const
    {
        return amount_;
    }

    [[nodiscard]] int
    signum() const
    {
        return amount_.signum();
    }

    operator STAmount const&() const
    {
        return amount_;
    }

    operator AnyAmount() const;

    operator json::Value() const
    {
        return toJson(value());
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
    PrettyAsset(A const& asset, std::uint32_t scale = 1) : PrettyAsset{Asset{asset}, scale}
    {
    }

    PrettyAsset(Asset const& asset, std::uint32_t scale = 1) : asset_(asset), scale_(scale)
    {
    }

    [[nodiscard]] Asset const&
    raw() const
    {
        return asset_;
    }

    operator Asset const&() const
    {
        return asset_;
    }

    operator json::Value() const
    {
        return toJson(asset_);
    }

    template <std::integral T>
    PrettyAmount
    operator()(T v, Number::RoundingMode rounding = Number::getround()) const
    {
        return operator()(Number(v), rounding);
    }

    PrettyAmount
    operator()(Number v, Number::RoundingMode rounding = Number::getround()) const
    {
        NumberRoundModeGuard const mg(rounding);
        STAmount const amount{asset_, v * scale_};
        return {amount, ""};
    }

    None
    operator()(NoneT) const
    {
        return {asset_};
    }

    [[nodiscard]] bool
    integral() const
    {
        return asset_.integral();
    }

    [[nodiscard]] bool
    native() const
    {
        return asset_.native();
    }

    template <ValidIssueType TIss>
    [[nodiscard]] bool
    holds() const
    {
        return asset_.holds<TIss>();
    }
};
//------------------------------------------------------------------------------

// Specifies an order book
struct BookSpec
{
    xrpl::Asset asset;

    BookSpec(xrpl::Asset const& asset) : asset(asset)
    {
    }
};

//------------------------------------------------------------------------------

struct XrpT
{
    /** Implicit conversion to Issue.

        This allows passing XRP where
        an Issue is expected.
    */
    operator Issue() const
    {
        return xrpIssue();
    }
    operator Asset() const
    {
        return xrpIssue();
    }

    static bool
    integral()
    {
        return true;
    }

    /** Returns an amount of XRP as PrettyAmount,
        which is trivially convertible to STAmount

        @param v The number of XRP (not drops)
    */
    /** @{ */
    template <class T, class = std::enable_if_t<std::is_integral_v<T>>>
    PrettyAmount
    operator()(T v) const
    {
        using TOut = std::conditional_t<std::is_signed_v<T>, std::int64_t, std::uint64_t>;
        return {TOut{v} * kJtxDropsPerXrp};
    }

    /** Returns an amount of XRP as PrettyAmount,
        which is trivially convertible to STAmount

        @param v The Number of XRP (not drops). May be fractional.
    */
    PrettyAmount
    operator()(Number v) const
    {
        auto const c = kJtxDropsPerXrp.drops();
        auto const d = std::int64_t(v * c);
        if (Number(d) / c != v)
            Throw<std::domain_error>("unrepresentable");
        return {d};
    }

    PrettyAmount
    operator()(double v) const
    {
        auto const c = kJtxDropsPerXrp.drops();
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
    operator()(NoneT) const
    {
        return {xrpIssue()};
    }

    friend BookSpec
    operator~(XrpT const&)
    {
        return BookSpec(Issue{xrpCurrency(), xrpAccount()});
    }
};

/** Converts to XRP Issue or STAmount.

    Examples:
        XRP         Converts to the XRP Issue
        XRP(10)     Returns STAmount of 10 XRP
*/
extern XrpT const XRP;  // NOLINT(readability-identifier-naming)

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
struct EpsilonT
{
    EpsilonT() = default;

    detail::EpsilonMultiple
    operator()(std::size_t n) const
    {
        return {n};
    }
};

static EpsilonT const kEpsilon;

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
    xrpl::Currency currency;

    IOU(Account account, xrpl::Currency const& currency)
        : account(std::move(account)), currency(currency)
    {
    }

    [[nodiscard]] Issue
    issue() const
    {
        return {currency, account.id()};
    }
    [[nodiscard]] Asset
    asset() const
    {
        return issue();
    }
    [[nodiscard]] bool
    integral() const
    {
        return issue().integral();
    }

    /** Implicit conversion to Issue or Asset.

        This allows passing an IOU
        value where an Issue or Asset is expected.
    */
    operator Issue() const
    {
        return issue();
    }
    operator Asset() const
    {
        return asset();
    }
    operator PrettyAsset() const
    {
        return asset();
    }

    template <
        class T,
        class = std::enable_if_t<sizeof(T) >= sizeof(int) && std::is_arithmetic_v<T>>>
    PrettyAmount
    operator()(T v) const
    {
        // VFALCO NOTE Should throw if the
        //             representation of v is not exact.
        return {amountFromString(issue(), std::to_string(v)), account.name()};
    }

    PrettyAmount
    operator()(EpsilonT) const;
    PrettyAmount
    operator()(detail::EpsilonMultiple) const;

    // VFALCO TODO
    // STAmount operator()(char const* s) const;

    /** Returns None-of-Issue */
    None
    operator()(NoneT) const
    {
        return {issue()};
    }

    friend BookSpec
    operator~(IOU const& iou)
    {
        return BookSpec(Issue{iou.currency, iou.account.id()});
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
    xrpl::MPTID issuanceID;

    MPT(std::string n, xrpl::MPTID const& issuanceID) : name(std::move(n)), issuanceID(issuanceID)
    {
    }
    MPT(std::string n = "") : name(std::move(n)), issuanceID(noMPT())
    {
    }
    MPT(Asset const& asset) : issuanceID(asset.get<MPTIssue>())
    {
    }
    MPT(AccountID const& account, std::int32_t seq = 0) : issuanceID(makeMptID(seq, account))
    {
    }

    [[nodiscard]] xrpl::MPTID const&
    mpt() const
    {
        return issuanceID;
    }

    /** Explicit conversion to MPTIssue or asset.
     */
    [[nodiscard]] xrpl::MPTIssue
    mptIssue() const
    {
        return MPTIssue{issuanceID};
    }
    [[nodiscard]] Asset
    asset() const
    {
        return mptIssue();
    }
    static bool
    integral()
    {
        return true;
    }

    /** Implicit conversion to MPTIssue or asset.

        This allows passing an MPT
        value where an MPTIssue is expected.
    */
    operator xrpl::MPTIssue() const
    {
        return mptIssue();
    }

    operator PrettyAsset() const
    {
        return asset();
    }
    operator xrpl::Asset() const
    {
        return mpt();
    }
    operator xrpl::MPTID() const
    {
        return mpt();
    }

    template <class T>
        requires(sizeof(T) >= sizeof(int) && std::is_arithmetic_v<T>)
    PrettyAmount
    operator()(T v) const
    {
        return {amountFromString(mpt(), std::to_string(v)), name};
    }

    PrettyAmount
    operator()(EpsilonT) const;
    PrettyAmount
    operator()(detail::EpsilonMultiple) const;

    /** Returns None-of-Issue */
    None
    operator()(NoneT) const
    {
        return {noMPT()};
    }

    friend BookSpec
    operator~(MPT const& mpt)
    {
        return BookSpec{Asset{mpt}};
    }
};

std::ostream&
operator<<(std::ostream& os, MPT const& mpt);

//------------------------------------------------------------------------------

struct AnyT
{
    inline AnyAmount
    operator()(STAmount const& sta) const;
};

/** Amount specifier with an option for any issuer. */
struct AnyAmount
{
    bool isAny;
    STAmount value;

    AnyAmount() = delete;
    AnyAmount(AnyAmount const&) = default;
    AnyAmount&
    operator=(AnyAmount const&) = default;

    AnyAmount(STAmount amount) : isAny(false), value(std::move(amount))
    {
    }

    AnyAmount(STAmount amount, AnyT const*) : isAny(true), value(std::move(amount))
    {
    }

    // Reset the issue to a specific account
    void
    to(AccountID const& id)
    {
        if (!isAny)
            return;
        value.get<Issue>().account = id;
    }
};

inline AnyAmount
AnyT::operator()(STAmount const& sta) const
{
    return AnyAmount(sta, this);
}

/** Returns an amount representing "any issuer"
    @note With respect to what the recipient will accept
*/
extern AnyT const kAny;

}  // namespace test::jtx

}  // namespace xrpl
