#ifndef XRPL_PROTOCOL_ASSET_H_INCLUDED
#define XRPL_PROTOCOL_ASSET_H_INCLUDED

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>

namespace ripple {

class Asset;
class STAmount;

template <typename TIss>
concept ValidIssueType =
    std::is_same_v<TIss, Issue> || std::is_same_v<TIss, MPTIssue>;

template <typename A>
concept AssetType =
    std::is_convertible_v<A, Asset> || std::is_convertible_v<A, Issue> ||
    std::is_convertible_v<A, MPTIssue> || std::is_convertible_v<A, MPTID>;

/* Asset is an abstraction of three different issue types: XRP, IOU, MPT.
 * For historical reasons, two issue types XRP and IOU are wrapped in Issue
 * type. Many functions and classes there were first written for Issue
 * have been rewritten for Asset.
 */
class Asset
{
public:
    using value_type = std::variant<Issue, MPTIssue>;

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

    AccountID const&
    getIssuer() const;

    template <ValidIssueType TIss>
    constexpr TIss const&
    get() const;

    template <ValidIssueType TIss>
    TIss&
    get();

    template <ValidIssueType TIss>
    constexpr bool
    holds() const;

    std::string
    getText() const;

    constexpr value_type const&
    value() const;

    void
    setJson(Json::Value& jv) const;

    STAmount
    operator()(Number const&) const;

    bool
    native() const
    {
        return std::visit(
            [&]<ValidIssueType TIss>(TIss const& issue) {
                if constexpr (std::is_same_v<TIss, Issue>)
                    return issue.native();
                if constexpr (std::is_same_v<TIss, MPTIssue>)
                    return false;
            },
            issue_);
    }

    bool
    integral() const
    {
        return std::visit(
            [&]<ValidIssueType TIss>(TIss const& issue) {
                if constexpr (std::is_same_v<TIss, Issue>)
                    return issue.native();
                if constexpr (std::is_same_v<TIss, MPTIssue>)
                    return true;
            },
            issue_);
    }

    friend constexpr bool
    operator==(Asset const& lhs, Asset const& rhs);

    friend constexpr std::weak_ordering
    operator<=>(Asset const& lhs, Asset const& rhs);

    friend constexpr bool
    operator==(Currency const& lhs, Asset const& rhs);

    /** Return true if both assets refer to the same currency (regardless of
     * issuer) or MPT issuance. Otherwise return false.
     */
    friend constexpr bool
    equalTokens(Asset const& lhs, Asset const& rhs);
};

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
constexpr TIss const&
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

constexpr bool
operator==(Asset const& lhs, Asset const& rhs)
{
    return std::visit(
        [&]<typename TLhs, typename TRhs>(
            TLhs const& issLhs, TRhs const& issRhs) {
            if constexpr (std::is_same_v<TLhs, TRhs>)
                return issLhs == issRhs;
            else
                return false;
        },
        lhs.issue_,
        rhs.issue_);
}

constexpr std::weak_ordering
operator<=>(Asset const& lhs, Asset const& rhs)
{
    return std::visit(
        []<ValidIssueType TLhs, ValidIssueType TRhs>(
            TLhs const& lhs_, TRhs const& rhs_) {
            if constexpr (std::is_same_v<TLhs, TRhs>)
                return std::weak_ordering(lhs_ <=> rhs_);
            else if constexpr (
                std::is_same_v<TLhs, Issue> && std::is_same_v<TRhs, MPTIssue>)
                return std::weak_ordering::greater;
            else
                return std::weak_ordering::less;
        },
        lhs.issue_,
        rhs.issue_);
}

constexpr bool
operator==(Currency const& lhs, Asset const& rhs)
{
    return rhs.holds<Issue>() && rhs.get<Issue>().currency == lhs;
}

constexpr bool
equalTokens(Asset const& lhs, Asset const& rhs)
{
    return std::visit(
        [&]<typename TLhs, typename TRhs>(
            TLhs const& issLhs, TRhs const& issRhs) {
            if constexpr (
                std::is_same_v<TLhs, Issue> && std::is_same_v<TRhs, Issue>)
                return issLhs.currency == issRhs.currency;
            else if constexpr (
                std::is_same_v<TLhs, MPTIssue> &&
                std::is_same_v<TRhs, MPTIssue>)
                return issLhs.getMptID() == issRhs.getMptID();
            else
                return false;
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

}  // namespace ripple

#endif  // XRPL_PROTOCOL_ASSET_H_INCLUDED
