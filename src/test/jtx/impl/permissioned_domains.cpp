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

#include <test/jtx/permissioned_domains.h>
#include <exception>

namespace ripple {
namespace test {
namespace jtx {
namespace pdomain {

// helpers
// Make json for PermissionedDomainSet transaction
Json::Value
setTx(
    AccountID const& account,
    Credentials const& credentials,
    std::optional<uint256> domain)
{
    Json::Value jv;
    jv[sfTransactionType] = jss::PermissionedDomainSet;
    jv[sfAccount] = to_string(account);
    if (domain)
        jv[sfDomainID] = to_string(*domain);
    Json::Value credentials2(Json::arrayValue);
    for (auto const& credential : credentials)
    {
        Json::Value object(Json::objectValue);
        object[sfCredential] = credential.toJson();
        credentials2.append(std::move(object));
    }
    jv[sfAcceptedCredentials] = credentials2;
    return jv;
}

// Make json for PermissionedDomainDelete transaction
Json::Value
deleteTx(AccountID const& account, uint256 const& domain)
{
    Json::Value jv{Json::objectValue};
    jv[sfTransactionType] = jss::PermissionedDomainDelete;
    jv[sfAccount] = to_string(account);
    jv[sfDomainID] = to_string(domain);
    return jv;
}

// Get PermissionedDomain objects by type from account_objects rpc call
std::map<uint256, Json::Value>
getObjects(Account const& account, Env& env, bool withType)
{
    std::map<uint256, Json::Value> ret;
    Json::Value params;
    params[jss::account] = account.human();
    if (withType)
        params[jss::type] = jss::permissioned_domain;
    auto const& resp = env.rpc("json", "account_objects", to_string(params));
    Json::Value objects(Json::arrayValue);
    objects = resp[jss::result][jss::account_objects];
    for (auto const& object : objects)
    {
        if (object["LedgerEntryType"] != "PermissionedDomain")
        {
            if (withType)
            {  // impossible to get there
                Throw<std::runtime_error>(
                    "Invalid object type: " +
                    object["LedgerEntryType"].asString());  // LCOV_EXCL_LINE
            }
            continue;
        }
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
credentialsFromJson(
    Json::Value const& object,
    std::unordered_map<std::string, Account> const& pubKey2Acc)
{
    Credentials ret;
    Json::Value credentials(Json::arrayValue);
    credentials = object["AcceptedCredentials"];
    for (auto const& credential : credentials)
    {
        Json::Value obj(Json::objectValue);
        obj = credential[jss::Credential];
        auto const& issuer = obj[jss::Issuer];
        auto const& credentialType = obj["CredentialType"];
        auto blob = strUnHex(credentialType.asString()).value();
        ret.push_back(
            {pubKey2Acc.at(issuer.asString()),
             std::string(blob.begin(), blob.end())});
    }
    return ret;
}

// Sort credentials the same way as PermissionedDomainSet. Silently
// remove duplicates.
Credentials
sortCredentials(Credentials const& input)
{
    std::set<Credential> credentialsSet;
    for (auto const& credential : input)
        credentialsSet.insert(credential);
    return {credentialsSet.begin(), credentialsSet.end()};
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
        break;
    }

    return ret;
}

}  // namespace pdomain
}  // namespace jtx
}  // namespace test
}  // namespace ripple
