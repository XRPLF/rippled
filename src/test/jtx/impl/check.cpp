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

#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/jss.h>
#include <test/jtx/check.h>

namespace ripple {
namespace test {
namespace jtx {

namespace check {

// Create a check.
Json::Value
create(
    jtx::Account const& account,
    jtx::Account const& dest,
    STAmount const& sendMax)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfSendMax.jsonName] = sendMax.getJson(JsonOptions::none);
    jv[sfDestination.jsonName] = dest.human();
    jv[sfTransactionType.jsonName] = jss::CheckCreate;
    jv[sfFlags.jsonName] = tfUniversal;
    return jv;
}

// Cash a check requiring that a specific amount be delivered.
Json::Value
cash(jtx::Account const& dest, uint256 const& checkId, STAmount const& amount)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = dest.human();
    jv[sfAmount.jsonName] = amount.getJson(JsonOptions::none);
    jv[sfCheckID.jsonName] = to_string(checkId);
    jv[sfTransactionType.jsonName] = jss::CheckCash;
    jv[sfFlags.jsonName] = tfUniversal;
    return jv;
}

// Cash a check requiring that at least a minimum amount be delivered.
Json::Value
cash(
    jtx::Account const& dest,
    uint256 const& checkId,
    DeliverMin const& atLeast)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = dest.human();
    jv[sfDeliverMin.jsonName] = atLeast.value.getJson(JsonOptions::none);
    jv[sfCheckID.jsonName] = to_string(checkId);
    jv[sfTransactionType.jsonName] = jss::CheckCash;
    jv[sfFlags.jsonName] = tfUniversal;
    return jv;
}

// Cancel a check.
Json::Value
cancel(jtx::Account const& dest, uint256 const& checkId)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = dest.human();
    jv[sfCheckID.jsonName] = to_string(checkId);
    jv[sfTransactionType.jsonName] = jss::CheckCancel;
    jv[sfFlags.jsonName] = tfUniversal;
    return jv;
}

}  // namespace check

}  // namespace jtx
}  // namespace test
}  // namespace ripple
