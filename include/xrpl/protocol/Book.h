#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Asset.h>

#include <boost/utility/base_from_member.hpp>

namespace xrpl {

/** Specifies an order book.
    The order book is a pair of Issues called in and out.
    @see Issue.
*/
class Book final : public CountedObject<Book>
{
public:
    Asset in;
    Asset out;
    std::optional<uint256> domain;

    Book()
    {
    }

    Book(Asset const& in_, Asset const& out_, std::optional<uint256> const& domain_)
        : in(in_), out(out_), domain(domain_)
    {
    }
};

bool
isConsistent(Book const& book);

std::string
to_string(Book const& book);

std::ostream&
operator<<(std::ostream& os, Book const& x);

template <class Hasher>
void
hash_append(Hasher& h, Book const& b)
{
    using beast::hash_append;
    hash_append(h, b.in, b.out);
    if (b.domain)
        hash_append(h, *(b.domain));
}

Book
reversed(Book const& book);

/** Equality comparison. */
/** @{ */
[[nodiscard]] inline constexpr bool
operator==(Book const& lhs, Book const& rhs)
{
    return (lhs.in == rhs.in) && (lhs.out == rhs.out) && (lhs.domain == rhs.domain);
}
/** @} */

/** Strict weak ordering. */
/** @{ */
[[nodiscard]] inline constexpr std::weak_ordering
operator<=>(Book const& lhs, Book const& rhs)
{
    if (auto const c{lhs.in <=> rhs.in}; c != 0)
        return c;
    if (auto const c{lhs.out <=> rhs.out}; c != 0)
        return c;

    // Manually compare optionals
    if (lhs.domain && rhs.domain)
        return *lhs.domain <=> *rhs.domain;  // Compare values if both exist
    if (!lhs.domain && rhs.domain)
        return std::weak_ordering::less;  // Empty is considered less
    if (lhs.domain && !rhs.domain)
        return std::weak_ordering::greater;  // Non-empty is greater

    return std::weak_ordering::equivalent;  // Both are empty
}
/** @} */

}  // namespace xrpl

//------------------------------------------------------------------------------

namespace std {

template <>
struct hash<xrpl::Issue> : private boost::base_from_member<std::hash<xrpl::Currency>, 0>,
                           private boost::base_from_member<std::hash<xrpl::AccountID>, 1>
{
private:
    using currency_hash_type = boost::base_from_member<std::hash<xrpl::Currency>, 0>;
    using issuer_hash_type = boost::base_from_member<std::hash<xrpl::AccountID>, 1>;

public:
    hash() = default;

    using value_type = std::size_t;
    using argument_type = xrpl::Issue;

    value_type
    operator()(argument_type const& value) const
    {
        value_type result(currency_hash_type::member(value.currency));
        if (!isXRP(value.currency))
            boost::hash_combine(result, issuer_hash_type::member(value.account));
        return result;
    }
};

template <>
struct hash<xrpl::MPTIssue> : private boost::base_from_member<std::hash<xrpl::MPTID>, 0>
{
private:
    using id_hash_type = boost::base_from_member<std::hash<xrpl::MPTID>, 0>;

public:
    explicit hash() = default;

    using value_type = std::size_t;
    using argument_type = xrpl::MPTIssue;

    value_type
    operator()(argument_type const& value) const
    {
        value_type const result(id_hash_type::member(value.getMptID()));
        return result;
    }
};

template <>
struct hash<xrpl::Asset>
{
private:
    using value_type = std::size_t;
    using argument_type = xrpl::Asset;

    using issue_hasher = std::hash<xrpl::Issue>;
    using mptissue_hasher = std::hash<xrpl::MPTIssue>;

    issue_hasher m_issue_hasher;
    mptissue_hasher m_mptissue_hasher;

public:
    explicit hash() = default;

    value_type
    operator()(argument_type const& asset) const
    {
        return asset.visit(
            [&](xrpl::Issue const& issue) {
                value_type const result(m_issue_hasher(issue));
                return result;
            },
            [&](xrpl::MPTIssue const& issue) {
                value_type const result(m_mptissue_hasher(issue));
                return result;
            });
    }
};

//------------------------------------------------------------------------------

template <>
struct hash<xrpl::Book>
{
private:
    using asset_hasher = std::hash<xrpl::Asset>;
    using uint256_hasher = xrpl::uint256::hasher;

    asset_hasher m_asset_hasher;
    uint256_hasher m_uint256_hasher;

public:
    hash() = default;

    using value_type = std::size_t;
    using argument_type = xrpl::Book;

    value_type
    operator()(argument_type const& value) const
    {
        value_type result(m_asset_hasher(value.in));
        boost::hash_combine(result, m_asset_hasher(value.out));

        if (value.domain)
            boost::hash_combine(result, m_uint256_hasher(*value.domain));

        return result;
    }
};

}  // namespace std

//------------------------------------------------------------------------------

namespace boost {

template <>
struct hash<xrpl::Issue> : std::hash<xrpl::Issue>
{
    hash() = default;

    using Base = std::hash<xrpl::Issue>;
    // VFALCO NOTE broken in vs2012
    // using Base::Base; // inherit ctors
};

template <>
struct hash<xrpl::MPTIssue> : std::hash<xrpl::MPTIssue>
{
    explicit hash() = default;

    using Base = std::hash<xrpl::MPTIssue>;
};

template <>
struct hash<xrpl::Asset> : std::hash<xrpl::Asset>
{
    explicit hash() = default;

    using Base = std::hash<xrpl::Asset>;
};

template <>
struct hash<xrpl::Book> : std::hash<xrpl::Book>
{
    hash() = default;

    using Base = std::hash<xrpl::Book>;
    // VFALCO NOTE broken in vs2012
    // using Base::Base; // inherit ctors
};

}  // namespace boost
