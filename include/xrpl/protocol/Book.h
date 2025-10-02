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

#ifndef XRPL_PROTOCOL_BOOK_H_INCLUDED
#define XRPL_PROTOCOL_BOOK_H_INCLUDED

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Issue.h>

#include <boost/utility/base_from_member.hpp>

namespace ripple {

/** Specifies an order book.
    The order book is a pair of Issues called in and out.
    @see Issue.
*/
class Book final : public CountedObject<Book>
{
public:
    Issue in;
    Issue out;
    std::optional<uint256> domain;

    Book()
    {
    }

    Book(
        Issue const& in_,
        Issue const& out_,
        std::optional<uint256> const& domain_)
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
    return (lhs.in == rhs.in) && (lhs.out == rhs.out) &&
        (lhs.domain == rhs.domain);
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

}  // namespace ripple

//------------------------------------------------------------------------------

namespace std {

template <>
struct hash<ripple::Issue>
    : private boost::base_from_member<std::hash<ripple::Currency>, 0>,
      private boost::base_from_member<std::hash<ripple::AccountID>, 1>
{
private:
    using currency_hash_type =
        boost::base_from_member<std::hash<ripple::Currency>, 0>;
    using issuer_hash_type =
        boost::base_from_member<std::hash<ripple::AccountID>, 1>;

public:
    hash() = default;

    using value_type = std::size_t;
    using argument_type = ripple::Issue;

    value_type
    operator()(argument_type const& value) const
    {
        value_type result(currency_hash_type::member(value.currency));
        if (!isXRP(value.currency))
            boost::hash_combine(
                result, issuer_hash_type::member(value.account));
        return result;
    }
};

//------------------------------------------------------------------------------

template <>
struct hash<ripple::Book>
{
private:
    using issue_hasher = std::hash<ripple::Issue>;
    using uint256_hasher = ripple::uint256::hasher;

    issue_hasher m_issue_hasher;
    uint256_hasher m_uint256_hasher;

public:
    hash() = default;

    using value_type = std::size_t;
    using argument_type = ripple::Book;

    value_type
    operator()(argument_type const& value) const
    {
        value_type result(m_issue_hasher(value.in));
        boost::hash_combine(result, m_issue_hasher(value.out));

        if (value.domain)
            boost::hash_combine(result, m_uint256_hasher(*value.domain));

        return result;
    }
};

}  // namespace std

//------------------------------------------------------------------------------

namespace boost {

template <>
struct hash<ripple::Issue> : std::hash<ripple::Issue>
{
    hash() = default;

    using Base = std::hash<ripple::Issue>;
    // VFALCO NOTE broken in vs2012
    // using Base::Base; // inherit ctors
};

template <>
struct hash<ripple::Book> : std::hash<ripple::Book>
{
    hash() = default;

    using Base = std::hash<ripple::Book>;
    // VFALCO NOTE broken in vs2012
    // using Base::Base; // inherit ctors
};

}  // namespace boost

#endif
