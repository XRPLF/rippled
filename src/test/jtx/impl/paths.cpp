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

#include <ripple/app/paths/Pathfinder.h>
#include <ripple/protocol/jss.h>
#include <test/jtx/paths.h>

namespace ripple {
namespace test {
namespace jtx {

void
paths::operator()(Env& env, JTx& jt) const
{
    auto& jv = jt.jv;
    auto const from = env.lookup(jv[jss::Account].asString());
    auto const to = env.lookup(jv[jss::Destination].asString());
    auto const amount = amountFromJson(sfAmount, jv[jss::Amount]);
    Pathfinder pf(
        std::make_shared<RippleLineCache>(env.current()),
        from,
        to,
        in_.currency,
        in_.account,
        amount,
        std::nullopt,
        env.app());
    if (!pf.findPaths(depth_))
        return;

    STPath fp;
    pf.computePathRanks(limit_);
    auto const found = pf.getBestPaths(limit_, fp, {}, in_.account);

    // VFALCO TODO API to allow caller to examine the STPathSet
    // VFALCO isDefault should be renamed to empty()
    if (!found.isDefault())
        jv[jss::Paths] = found.getJson(JsonOptions::none);
}

//------------------------------------------------------------------------------

Json::Value&
path::create()
{
    return jv_.append(Json::objectValue);
}

void
path::append_one(Account const& account)
{
    auto& jv = create();
    jv["account"] = toBase58(account.id());
}

void
path::append_one(IOU const& iou)
{
    auto& jv = create();
    jv["currency"] = to_string(iou.issue().currency);
    jv["account"] = toBase58(iou.issue().account);
}

void
path::append_one(BookSpec const& book)
{
    auto& jv = create();
    jv["currency"] = to_string(book.currency);
    jv["issuer"] = toBase58(book.account);
}

void
path::operator()(Env& env, JTx& jt) const
{
    jt.jv["Paths"].append(jv_);
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple
