//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

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

#include <test/jtx/check.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {
namespace jtx {

namespace check {

// Cash a check requiring that a specific amount be delivered.
Json::Value
cash(
    jtx::Account const& dest,
    uint256 const& checkId,
    STAmount const& amount,
    std::optional<jtx::Account> const& onBehalfOf)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = dest.human();
    jv[sfAmount.jsonName] = amount.getJson(JsonOptions::none);
    jv[sfCheckID.jsonName] = to_string(checkId);
    jv[sfTransactionType.jsonName] = jss::CheckCash;
    jv[sfFlags.jsonName] = tfUniversal;
    if (onBehalfOf)
        jv[sfOnBehalfOf.jsonName] = onBehalfOf->human();
    return jv;
}

// Cash a check requiring that at least a minimum amount be delivered.
Json::Value
cash(
    jtx::Account const& dest,
    uint256 const& checkId,
    DeliverMin const& atLeast,
    std::optional<jtx::Account> const& onBehalfOf)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = dest.human();
    jv[sfDeliverMin.jsonName] = atLeast.value.getJson(JsonOptions::none);
    jv[sfCheckID.jsonName] = to_string(checkId);
    jv[sfTransactionType.jsonName] = jss::CheckCash;
    jv[sfFlags.jsonName] = tfUniversal;
    if (onBehalfOf)
        jv[sfOnBehalfOf.jsonName] = onBehalfOf->human();
    return jv;
}

// Cancel a check.
Json::Value
cancel(
    jtx::Account const& dest,
    uint256 const& checkId,
    std::optional<jtx::Account> const& onBehalfOf)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = dest.human();
    jv[sfCheckID.jsonName] = to_string(checkId);
    jv[sfTransactionType.jsonName] = jss::CheckCancel;
    jv[sfFlags.jsonName] = tfUniversal;
    if (onBehalfOf)
        jv[sfOnBehalfOf.jsonName] = onBehalfOf->human();
    return jv;
}

// Helper function that returns the Checks on an account.
std::vector<std::shared_ptr<SLE const>>
checksOnAccount(test::jtx::Env& env, test::jtx::Account account)
{
    std::vector<std::shared_ptr<SLE const>> result;
    forEachItem(
        *env.current(),
        account,
        [&result](std::shared_ptr<SLE const> const& sle) {
            if (sle && sle->getType() == ltCHECK)
                result.push_back(sle);
        });
    return result;
}

}  // namespace check

}  // namespace jtx
}  // namespace test
}  // namespace ripple
