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

#include <test/jtx/balance.h>

namespace ripple {
namespace test {
namespace jtx {

void
doBalance(
    Env& env,
    AccountID const& account,
    bool none,
    STAmount const& value,
    Issue const& issue)
{
    if (isXRP(issue))
    {
        auto const sle = env.le(keylet::account(account));
        if (none)
        {
            env.test.expect(!sle);
        }
        else if (env.test.expect(sle))
        {
            env.test.expect(sle->getFieldAmount(sfBalance) == value);
        }
    }
    else
    {
        auto const sle = env.le(keylet::line(account, issue));
        if (none)
        {
            env.test.expect(!sle);
        }
        else if (env.test.expect(sle))
        {
            auto amount = sle->getFieldAmount(sfBalance);
            amount.setIssuer(issue.account);
            if (account > issue.account)
                amount.negate();
            env.test.expect(amount == value);
        }
    }
}

void
doBalance(
    Env& env,
    AccountID const& account,
    bool none,
    STAmount const& value,
    MPTIssue const& mptIssue)
{
    auto const sle = env.le(keylet::mptoken(mptIssue.getMptID(), account));
    if (none)
    {
        env.test.expect(!sle);
    }
    else if (env.test.expect(sle))
    {
        STAmount const amount{mptIssue, sle->getFieldU64(sfMPTAmount)};
        env.test.expect(amount == value);
    }
}

void
balance::operator()(Env& env) const
{
    return std::visit(
        [&](auto const& issue) {
            doBalance(env, account_.id(), none_, value_, issue);
        },
        value_.asset().value());
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
