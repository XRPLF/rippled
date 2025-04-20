//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_OPTION_H_INCLUDED
#define RIPPLE_PROTOCOL_OPTION_H_INCLUDED

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/Issue.h>

#include <boost/utility/base_from_member.hpp>

namespace ripple {

class Option final : public CountedObject<Option>
{
public:
    Issue issue;
    uint64_t strike;
    uint32_t expiration;

    Option()
    {
    }

    Option(Issue issue_, uint64_t strike_, uint32_t expiration_)
        : issue(issue_), strike(strike_), expiration(expiration_)
    {
    }
};

std::string
to_string(Option const& option);

std::ostream&
operator<<(std::ostream& os, Option const& x);

template <class Hasher>
void
hash_append(Hasher& h, Option const& o)
{
    using beast::hash_append;
    hash_append(h, o.issue, o.strike, o.expiration);
}

/** Equality comparison. */
/** @{ */
[[nodiscard]] inline constexpr bool
operator==(Option const& lhs, Option const& rhs)
{
    return (lhs.issue == rhs.issue) && (lhs.strike == rhs.strike) &&
        (lhs.expiration == rhs.expiration);
}
/** @} */

/** Strict weak ordering. */
/** @{ */
[[nodiscard]] inline constexpr std::weak_ordering
operator<=>(Option const& lhs, Option const& rhs)
{
    if (auto const c{lhs.strike <=> rhs.strike}; c != 0)
        return c;
    return lhs.strike <=> rhs.strike;
}
/** @} */

}  // namespace ripple

//------------------------------------------------------------------------------

namespace boost {

template <>
struct hash<ripple::Option> : std::hash<ripple::Option>
{
    using Base = std::hash<ripple::Option>;
    // VFALCO NOTE broken in vs2012
    // using Base::Base; // inherit ctors
};

}  // namespace boost

#endif