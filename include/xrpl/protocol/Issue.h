#pragma once

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/UintTypes.h>

namespace xrpl {

/** A currency issued by an account.
    @see Currency, AccountID, Issue, Book
*/
class Issue
{
public:
    Currency currency;
    AccountID account;

    Issue() = default;

    Issue(Currency const& c, AccountID const& a) : currency(c), account(a)
    {
    }

    [[nodiscard]] AccountID const&
    getIssuer() const
    {
        return account;
    }

    [[nodiscard]] std::string
    getText() const;

    void
    setJson(json::Value& jv) const;

    [[nodiscard]] bool
    native() const;

    [[nodiscard]] bool
    integral() const;

    friend constexpr std::weak_ordering
    operator<=>(Issue const& lhs, Issue const& rhs);
};

bool
isConsistent(Issue const& ac);

std::string
to_string(Issue const& ac);

json::Value
toJson(Issue const& is);

Issue
issueFromJson(json::Value const& v);

std::ostream&
operator<<(std::ostream& os, Issue const& x);

template <class Hasher>
void
hash_append(Hasher& h, Issue const& r)
{
    using beast::hash_append;
    hash_append(h, r.currency, r.account);
}

/** Equality comparison. */
/** @{ */
[[nodiscard]] constexpr bool
operator==(Issue const& lhs, Issue const& rhs)
{
    return (lhs.currency == rhs.currency) && (isXRP(lhs.currency) || lhs.account == rhs.account);
}
/** @} */

/** Strict weak ordering. */
/** @{ */
[[nodiscard]] constexpr std::weak_ordering
operator<=>(Issue const& lhs, Issue const& rhs)
{
    if (auto const c{lhs.currency <=> rhs.currency}; c != 0)
        return c;

    if (isXRP(lhs.currency))
        return std::weak_ordering::equivalent;

    return (lhs.account <=> rhs.account);
}
/** @} */

//------------------------------------------------------------------------------

/** Returns an asset specifier that represents XRP. */
inline Issue const&
xrpIssue()
{
    static Issue const kIssue{xrpCurrency(), xrpAccount()};
    return kIssue;
}

/** Returns an asset specifier that represents no account and currency. */
inline Issue const&
noIssue()
{
    static Issue const kIssue{noCurrency(), noAccount()};
    return kIssue;
}

inline bool
isXRP(Issue const& issue)
{
    return issue.native();
}

}  // namespace xrpl
