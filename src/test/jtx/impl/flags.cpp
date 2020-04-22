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

#include <ripple/protocol/jss.h>
#include <test/jtx/flags.h>

namespace ripple {
namespace test {
namespace jtx {

Json::Value
fset(Account const& account, std::uint32_t on, std::uint32_t off)
{
    Json::Value jv;
    jv[jss::Account] = account.human();
    jv[jss::TransactionType] = jss::AccountSet;
    if (on != 0)
        jv[jss::SetFlag] = on;
    if (off != 0)
        jv[jss::ClearFlag] = off;
    return jv;
}

void
flags::operator()(Env& env) const
{
    auto const sle = env.le(account_);
    if (sle->isFieldPresent(sfFlags))
        env.test.expect((sle->getFieldU32(sfFlags) & mask_) == mask_);
    else
        env.test.expect(mask_ == 0);
}

void
nflags::operator()(Env& env) const
{
    auto const sle = env.le(account_);
    if (sle->isFieldPresent(sfFlags))
        env.test.expect((sle->getFieldU32(sfFlags) & mask_) == 0);
    else
        env.test.pass();
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
