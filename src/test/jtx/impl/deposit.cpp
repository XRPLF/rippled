//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

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

#include <test/jtx/deposit.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {
namespace jtx {

namespace deposit {

// Add DepositPreauth.
Json::Value
auth(
    jtx::Account const& account,
    jtx::Account const& auth,
    std::optional<jtx::Account> const& onBehalfOf)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfAuthorize.jsonName] = auth.human();
    jv[sfTransactionType.jsonName] = jss::DepositPreauth;
    if (onBehalfOf)
        jv[sfOnBehalfOf.jsonName] = onBehalfOf->human();
    return jv;
}

// Remove DepositPreauth.
Json::Value
unauth(
    jtx::Account const& account,
    jtx::Account const& unauth,
    std::optional<jtx::Account> const& onBehalfOf)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfUnauthorize.jsonName] = unauth.human();
    jv[sfTransactionType.jsonName] = jss::DepositPreauth;
    if (onBehalfOf)
        jv[sfOnBehalfOf.jsonName] = onBehalfOf->human();
    return jv;
}

// Add DepositPreauth.
Json::Value
authCredentials(
    jtx::Account const& account,
    std::vector<AuthorizeCredentials> const& auth)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfAuthorizeCredentials.jsonName] = Json::arrayValue;
    auto& arr(jv[sfAuthorizeCredentials.jsonName]);
    for (auto const& o : auth)
    {
        Json::Value j2;
        j2[jss::Credential] = o.toJson();
        arr.append(std::move(j2));
    }
    jv[sfTransactionType.jsonName] = jss::DepositPreauth;
    return jv;
}

// Remove DepositPreauth.
Json::Value
unauthCredentials(
    jtx::Account const& account,
    std::vector<AuthorizeCredentials> const& auth)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfUnauthorizeCredentials.jsonName] = Json::arrayValue;
    auto& arr(jv[sfUnauthorizeCredentials.jsonName]);
    for (auto const& o : auth)
    {
        Json::Value j2;
        j2[jss::Credential] = o.toJson();
        arr.append(std::move(j2));
    }
    jv[sfTransactionType.jsonName] = jss::DepositPreauth;
    return jv;
}

Json::Value
ledgerEntryDepositPreauth(
    jtx::Env& env,
    jtx::Account const& acc,
    std::vector<jtx::deposit::AuthorizeCredentials> const& auth)
{
    Json::Value jvParams;
    jvParams[jss::ledger_index] = jss::validated;
    jvParams[jss::deposit_preauth][jss::owner] = acc.human();
    jvParams[jss::deposit_preauth][jss::authorized_credentials] =
        Json::arrayValue;
    auto& arr(jvParams[jss::deposit_preauth][jss::authorized_credentials]);
    for (auto const& o : auth)
    {
        arr.append(o.toLEJson());
    }
    return env.rpc("json", "ledger_entry", to_string(jvParams));
}

}  // namespace deposit

}  // namespace jtx
}  // namespace test
}  // namespace ripple
