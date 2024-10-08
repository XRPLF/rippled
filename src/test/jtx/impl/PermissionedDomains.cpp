//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#include <test/jtx/PermissionedDomains.h>
#include <exception>

namespace ripple {
namespace test {
namespace jtx {
namespace pd {

// helpers
// Make json for PermissionedDomainSet transaction
Json::Value
setTx(
    AccountID const& account,
    Credentials const& credentials,
    std::optional<uint256> domain)
{
    Json::Value jv;
    jv[sfTransactionType.jsonName] = jss::PermissionedDomainSet;
    jv[sfAccount.jsonName] = to_string(account);
    if (domain)
        jv[sfDomainID.jsonName] = to_string(*domain);
    Json::Value a(Json::arrayValue);
    for (auto const& credential : credentials)
    {
        Json::Value obj(Json::objectValue);
        obj[sfIssuer.jsonName] = to_string(credential.first);
        obj[sfCredentialType.jsonName] =
            strHex(Slice{credential.second.data(), credential.second.size()});
        Json::Value o2(Json::objectValue);
        o2[sfAcceptedCredential.jsonName] = obj;
        a.append(o2);
    }
    jv[sfAcceptedCredentials.jsonName] = a;
    return jv;
}

// Make json for PermissionedDomainDelete transaction
Json::Value
deleteTx(AccountID const& account, uint256 const& domain)
{
    Json::Value jv{Json::objectValue};
    jv[sfTransactionType.jsonName] = jss::PermissionedDomainDelete;
    jv[sfAccount.jsonName] = to_string(account);
    jv[sfDomainID.jsonName] = to_string(domain);
    return jv;
}

// Get PermissionedDomain objects from account_objects rpc call
std::map<uint256, Json::Value>
getObjects(Account const& account, Env& env)
{
    std::map<uint256, Json::Value> ret;
    Json::Value params;
    params[jss::account] = account.human();
    auto const& resp = env.rpc("json", "account_objects", to_string(params));
    Json::Value a(Json::arrayValue);
    a = resp[jss::result][jss::account_objects];
    for (auto const& object : a)
    {
        if (object["LedgerEntryType"] != "PermissionedDomain")
            continue;
        uint256 index;
        std::ignore = index.parseHex(object[jss::index].asString());
        ret[index] = object;
    }
    return ret;
}

// Check if ledger object is there
bool
objectExists(uint256 const& objID, Env& env)
{
    Json::Value params;
    params[jss::index] = to_string(objID);
    auto const& resp =
        env.rpc("json", "ledger_entry", to_string(params))["result"]["status"]
            .asString();
    if (resp == "success")
        return true;
    if (resp == "error")
        return false;
    throw std::runtime_error("Error getting ledger_entry RPC result.");
}

// Extract credentials from account_object object
Credentials
credentialsFromJson(Json::Value const& object)
{
    Credentials ret;
    Json::Value a(Json::arrayValue);
    a = object["AcceptedCredentials"];
    for (auto const& credential : a)
    {
        Json::Value obj(Json::objectValue);
        obj = credential["AcceptedCredential"];
        auto const& issuer = obj["Issuer"];
        auto const& credentialType = obj["CredentialType"];
        ret.emplace_back(
            *parseBase58<AccountID>(issuer.asString()),
            strUnHex(credentialType.asString()).value());
    }
    return ret;
}

// Sort credentials the same way as PermissionedDomainSet. Silently
// remove duplicates.
Credentials
sortCredentials(Credentials const& input)
{
    Credentials ret = input;

    std::set<Credential> cSet;
    for (auto const& c : ret)
        cSet.insert(c);
    if (ret.size() > cSet.size())
    {
        ret = Credentials();
        for (auto const& c : cSet)
            ret.push_back(c);
    }

    std::sort(
        ret.begin(),
        ret.end(),
        [](Credential const& left, Credential const& right) -> bool {
            if (left.first < right.first)
                return true;
            if (left.first == right.first)
            {
                if (left.second < right.second)
                    return true;
            }
            return false;
        });
    return ret;
}

// Get account_info
Json::Value
ownerInfo(Account const& account, Env& env)
{
    Json::Value params;
    params[jss::account] = account.human();
    auto const& resp = env.rpc("json", "account_info", to_string(params));
    return env.rpc(
        "json", "account_info", to_string(params))["result"]["account_data"];
}

uint256
getNewDomain(std::shared_ptr<STObject const> const& meta)
{
    uint256 ret;
    auto metaJson = meta->getJson(JsonOptions::none);
    Json::Value a(Json::arrayValue);
    a = metaJson["AffectedNodes"];

    for (auto const& node : a)
    {
        if (!node.isMember("CreatedNode") ||
            node["CreatedNode"]["LedgerEntryType"] != "PermissionedDomain")
        {
            continue;
        }
        std::ignore =
            ret.parseHex(node["CreatedNode"]["LedgerIndex"].asString());
    }

    return ret;
}

}  // namespace pd
}  // namespace jtx
}  // namespace test
}  // namespace ripple
