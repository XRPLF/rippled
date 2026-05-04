#include <test/jtx/permissioned_domains.h>

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>

#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/jss.h>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace xrpl::test::jtx::pdomain {

// helpers
// Make json for PermissionedDomainSet transaction
json::Value
setTx(AccountID const& account, Credentials const& credentials, std::optional<uint256> domain)
{
    json::Value jv;
    jv[sfTransactionType] = jss::PermissionedDomainSet;
    jv[sfAccount] = to_string(account);
    if (domain)
        jv[sfDomainID] = to_string(*domain);

    json::Value acceptedCredentials(json::ArrayValue);
    for (auto const& credential : credentials)
    {
        json::Value object(json::ObjectValue);
        object[sfCredential] = credential.toJson();
        acceptedCredentials.append(std::move(object));
    }

    jv[sfAcceptedCredentials] = acceptedCredentials;
    return jv;
}

// Make json for PermissionedDomainDelete transaction
json::Value
deleteTx(AccountID const& account, uint256 const& domain)
{
    json::Value jv{json::ObjectValue};
    jv[sfTransactionType] = jss::PermissionedDomainDelete;
    jv[sfAccount] = to_string(account);
    jv[sfDomainID] = to_string(domain);
    return jv;
}

// Get PermissionedDomain objects by type from account_objects rpc call
std::map<uint256, json::Value>
getObjects(Account const& account, Env& env, bool withType)
{
    std::map<uint256, json::Value> ret;
    json::Value params;
    params[jss::account] = account.human();
    if (withType)
        params[jss::type] = jss::permissioned_domain;

    auto const& resp = env.rpc("json", "account_objects", to_string(params));
    json::Value objects(json::ArrayValue);
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
    json::Value params;
    params[jss::index] = to_string(objID);

    auto const result = env.rpc("json", "ledger_entry", to_string(params))["result"];

    if ((result["status"] == "error") && (result["error"] == "entryNotFound"))
        return false;

    if ((result["node"]["LedgerEntryType"] != jss::PermissionedDomain))
        return false;

    if (result["status"] == "success")
        return true;

    throw std::runtime_error("Error getting ledger_entry RPC result.");
}

// Extract credentials from account_object object
Credentials
credentialsFromJson(
    json::Value const& object,
    std::unordered_map<std::string, Account> const& human2Acc)
{
    Credentials ret;
    json::Value credentials(json::ArrayValue);
    credentials = object["AcceptedCredentials"];
    for (auto const& credential : credentials)
    {
        json::Value obj(json::ObjectValue);
        obj = credential[jss::Credential];
        auto const& issuer = obj[jss::Issuer];
        auto const& credentialType = obj["CredentialType"];
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access): used only in tests
        auto blob = strUnHex(credentialType.asString()).value();
        ret.push_back({human2Acc.at(issuer.asString()), std::string(blob.begin(), blob.end())});
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

uint256
getNewDomain(std::shared_ptr<STObject const> const& meta)
{
    uint256 ret;
    auto metaJson = meta->getJson(JsonOptions::KNone);
    json::Value a(json::ArrayValue);
    a = metaJson["AffectedNodes"];

    for (auto const& node : a)
    {
        if (!node.isMember("CreatedNode") ||
            node["CreatedNode"]["LedgerEntryType"] != "PermissionedDomain")
        {
            continue;
        }
        std::ignore = ret.parseHex(node["CreatedNode"]["LedgerIndex"].asString());
        break;
    }

    return ret;
}

}  // namespace xrpl::test::jtx::pdomain
