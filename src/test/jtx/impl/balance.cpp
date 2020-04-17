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
balance::operator()(Env& env) const
{
    if (isXRP(value_.issue()))
    {
        auto const sle = env.le(account_);
        if (none_)
        {
            env.test.expect(!sle);
        }
        else if (env.test.expect(sle))
        {
            env.test.expect(sle->getFieldAmount(sfBalance) == value_);
        }
    }
    else
    {
        auto const sle = env.le(keylet::line(account_.id(), value_.issue()));
        if (none_)
        {
            env.test.expect(!sle);
        }
        else if (env.test.expect(sle))
        {
            auto amount = sle->getFieldAmount(sfBalance);
            amount.setIssuer(value_.issue().account);
            if (account_.id() > value_.issue().account)
                amount.negate();
            env.test.expect(amount == value_);
        }
    }
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
