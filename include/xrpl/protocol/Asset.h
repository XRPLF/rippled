#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Rules.h>

namespace xrpl {

class STAmount;

template <typename T>
    requires(
        std::is_same_v<T, XRPAmount> || std::is_same_v<T, IOUAmount> ||
        std::is_same_v<T, MPTAmount>)
struct AmountType
{
    using amount_type = T;
};

/* Used to check for an asset with either badCurrency()
 * or MPT with 0 account.
 */
struct BadAsset
{
};

inline BadAsset const&
badAsset()
{
    static BadAsset const a;
    return a;
}

/* Asset is an abstraction of three different issue types: XRP, IOU, MPT.
 * For historical reasons, two issue types XRP and IOU are wrapped in Issue
 * type. Many functions and classes there were first written for Issue
 * have been rewritten for Asset.
 */
class Asset
{
public:
    using value_type = std::variant<Issue, MPTIssue>;
    using token_type = std::variant<Currency, MPTID>;
    using AmtType =
        std::variant<AmountType<XRPAmount>, AmountType<IOUAmount>, AmountType<MPTAmount>>;

private:
    value_type issue_;

public:
    Asset() = default;

    /** Conversions to Asset are implicit and conversions to specific issue
     *  type are explicit. This design facilitates the use of Asset.
     */
    Asset(Issue const& issue) : issue_(issue)
    {
    }

    Asset(MPTIssue const& mptIssue) : issue_(mptIssue)
    {
    }

    Asset(MPTID const& issuanceID) : issue_(MPTIssue{issuanceID})
    {
    }

    [[nodiscard]] AccountID const&
    getIssuer() const;

    template <ValidIssueType TIss>
    constexpr TIss const&
    get() const;

    template <ValidIssueType TIss>
    TIss&
    get();

    template <ValidIssueType TIss>
    [[nodiscard]] constexpr bool
    holds() const;

    [[nodiscard]] std::string
    getText() const;

    [[nodiscard]] constexpr value_type const&
    value() const;

    [[nodiscard]] constexpr token_type
    token() const;

    void
    setJson(Json::Value& jv) const;

    STAmount
    operator()(Number const&) const;

    [[nodiscard]] constexpr AmtType
    getAmountType() const;

    // Custom, generic visit implementation
    template <typename... Visitors>
    constexpr auto
    visit(Visitors&&... visitors) const -> decltype(auto)
    {
        // Simple delegation to the reusable utility, passing the internal
        // variant data.
        return detail::visit(issue_, std::forward<Visitors>(visitors)...);
    }

    [[nodiscard]] constexpr bool
    native() const
    {
        return visit(
            [&](Issue const& issue) { return issue.native(); },
            [&](MPTIssue const&) { return false; });
    }

    [[nodiscard]] bool
    integral() const
    {
        return visit(
            [&](Issue const& issue) { return issue.native(); },
            [&](MPTIssue const&) { return true; });
    }

    friend constexpr bool
    operator==(Asset const& lhs, Asset const& rhs);

    friend constexpr std::weak_ordering
    operator<=>(Asset const& lhs, Asset const& rhs);

    friend constexpr bool
    operator==(Currency const& lhs, Asset const& rhs);

    // rhs is either badCurrency() or MPT issuer is 0
    friend constexpr bool
    operator==(BadAsset const& lhs, Asset const& rhs);

    /** Return true if both assets refer to the same currency (regardless of
     * issuer) or MPT issuance. Otherwise return false.
     */
    friend constexpr bool
    equalTokens(Asset const& lhs, Asset const& rhs);
};

template <ValidIssueType TIss>
constexpr bool is_issue_v = std::is_same_v<TIss, Issue>;

template <ValidIssueType TIss>
constexpr bool is_mptissue_v = std::is_same_v<TIss, MPTIssue>;

inline Json::Value
to_json(Asset const& asset)
{
    Json::Value jv;
    asset.setJson(jv);
    return jv;
}

template <ValidIssueType TIss>
constexpr bool
Asset::holds() const
{
    return std::holds_alternative<TIss>(issue_);
}

template <ValidIssueType TIss>
[[nodiscard]] constexpr TIss const&
Asset::get() const
{
    if (!std::holds_alternative<TIss>(issue_))
        Throw<std::logic_error>("Asset is not a requested issue");
    return std::get<TIss>(issue_);
}

template <ValidIssueType TIss>
TIss&
Asset::get()
{
    if (!std::holds_alternative<TIss>(issue_))
        Throw<std::logic_error>("Asset is not a requested issue");
    return std::get<TIss>(issue_);
}

constexpr Asset::value_type const&
Asset::value() const
{
    return issue_;
}

constexpr Asset::token_type
Asset::token() const
{
    return visit(
        [&](Issue const& issue) -> Asset::token_type { return issue.currency; },
        [&](MPTIssue const& issue) -> Asset::token_type { return issue.getMptID(); });
}

constexpr Asset::AmtType
Asset::getAmountType() const
{
    return visit(
        [&](Issue const& issue) -> Asset::AmtType {
            constexpr AmountType<XRPAmount> xrp;
            constexpr AmountType<IOUAmount> iou;
            return native() ? AmtType(xrp) : AmtType(iou);
        },
        [&](MPTIssue const& issue) -> Asset::AmtType {
            constexpr AmountType<MPTAmount> mpt;
            return AmtType(mpt);
        });
}

constexpr bool
operator==(Asset const& lhs, Asset const& rhs)
{
    return std::visit(
        [&]<typename TLhs, typename TRhs>(TLhs const& issLhs, TRhs const& issRhs) {
            if constexpr (std::is_same_v<TLhs, TRhs>)
            {
                return issLhs == issRhs;
            }
            else
            {
                return false;
            }
        },
        lhs.issue_,
        rhs.issue_);
}

constexpr std::weak_ordering
operator<=>(Asset const& lhs, Asset const& rhs)
{
    return std::visit(
        []<ValidIssueType TLhs, ValidIssueType TRhs>(TLhs const& lhs_, TRhs const& rhs_) {
            if constexpr (std::is_same_v<TLhs, TRhs>)
            {
                return std::weak_ordering(lhs_ <=> rhs_);
            }
            else if constexpr (is_issue_v<TLhs> && is_mptissue_v<TRhs>)
            {
                return std::weak_ordering::greater;
            }
            else
            {
                return std::weak_ordering::less;
            }
        },
        lhs.issue_,
        rhs.issue_);
}

constexpr bool
operator==(Currency const& lhs, Asset const& rhs)
{
    return rhs.visit(
        [&](Issue const& issue) { return issue.currency == lhs; },
        [](MPTIssue const& issue) { return false; });
}

constexpr bool
operator==(BadAsset const&, Asset const& rhs)
{
    return rhs.visit(
        [](Issue const& issue) -> bool { return badCurrency() == issue.currency; },
        [](MPTIssue const& issue) -> bool { return issue.getIssuer() == xrpAccount(); });
}

constexpr bool
equalTokens(Asset const& lhs, Asset const& rhs)
{
    return std::visit(
        [&]<typename TLhs, typename TRhs>(TLhs const& issLhs, TRhs const& issRhs) {
            if constexpr (std::is_same_v<TLhs, Issue> && std::is_same_v<TRhs, Issue>)
            {
                return issLhs.currency == issRhs.currency;
            }
            else if constexpr (std::is_same_v<TLhs, MPTIssue> && std::is_same_v<TRhs, MPTIssue>)
            {
                return issLhs.getMptID() == issRhs.getMptID();
            }
            else
            {
                return false;
            }
        },
        lhs.issue_,
        rhs.issue_);
}

inline bool
isXRP(Asset const& asset)
{
    return asset.native();
}

std::string
to_string(Asset const& asset);

bool
validJSONAsset(Json::Value const& jv);

Asset
assetFromJson(Json::Value const& jv);

inline bool
isConsistent(Asset const& asset)
{
    return asset.visit(
        [](Issue const& issue) { return isConsistent(issue); },
        [](MPTIssue const&) { return true; });
}

inline bool
validAsset(Asset const& asset)
{
    return asset.visit(
        [](Issue const& issue) { return isConsistent(issue) && issue.currency != badCurrency(); },
        [](MPTIssue const& issue) { return issue.getIssuer() != xrpAccount(); });
}

template <class Hasher>
void
hash_append(Hasher& h, Asset const& r)
{
    using beast::hash_append;
    r.visit(
        [&](Issue const& issue) { hash_append(h, issue); },
        [&](MPTIssue const& issue) { hash_append(h, issue); });
}

std::ostream&
operator<<(std::ostream& os, Asset const& x);

}  // namespace xrpl
