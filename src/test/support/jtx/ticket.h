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

#ifndef RIPPLE_TEST_JTX_TICKET_H_INCLUDED
#define RIPPLE_TEST_JTX_TICKET_H_INCLUDED

#include <test/support/jtx/Env.h>
#include <test/support/jtx/Account.h>
#include <test/support/jtx/owners.h>
#include <boost/optional.hpp>
#include <cstdint>

namespace ripple {
namespace test {
namespace jtx {

/*
    This shows how the jtx system may be extended to other
    generators, funclets, conditions, and operations,
    without changing the base declarations.
*/

/** Ticket operations */
namespace ticket {

namespace detail {

Json::Value
create (Account const& account,
    boost::optional<Account> const& target,
        boost::optional<std::uint32_t> const& expire);

inline
void
create_arg (boost::optional<Account>& opt,
    boost::optional<std::uint32_t>&,
        Account const& value)
{
    opt = value;
}

inline
void
create_arg (boost::optional<Account>&,
    boost::optional<std::uint32_t>& opt,
        std::uint32_t value)
{
    opt = value;
}

inline
void
create_args (boost::optional<Account>&,
    boost::optional<std::uint32_t>&)
{
}

template<class Arg, class... Args>
void
create_args(boost::optional<Account>& account_opt,
    boost::optional<std::uint32_t>& expire_opt,
        Arg const& arg, Args const&... args)
{
    create_arg(account_opt, expire_opt, arg);
    create_args(account_opt, expire_opt, args...);
}

} // detail

/** Create a ticket */
template <class... Args>
Json::Value
create (Account const& account,
    Args const&... args)
{
    boost::optional<Account> target;
    boost::optional<std::uint32_t> expire;
    detail::create_args(target, expire, args...);
    return detail::create(
        account, target, expire);
}

/** Cancel a ticket */
Json::Value
cancel(Account const& account, std::string const & ticketId);

} // ticket

/** Match the number of tickets on the account. */
using tickets = owner_count<ltTICKET>;

} // jtx

} // test
} // ripple

#endif
