#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/deposit.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STObject.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace xrpl::test::jtx::pdomain {

// Helpers for PermissionedDomains testing
using Credential = xrpl::test::jtx::deposit::AuthorizeCredentials;
using Credentials = std::vector<Credential>;

// helpers
// Make json for PermissionedDomainSet transaction
json::Value
setTx(
    AccountID const& account,
    Credentials const& credentials,
    std::optional<uint256> domain = std::nullopt);

// Make json for PermissionedDomainDelete transaction
json::Value
deleteTx(AccountID const& account, uint256 const& domain);

// Get PermissionedDomain objects from account_objects rpc call
std::map<uint256, json::Value>
getObjects(Account const& account, Env& env, bool withType = true);

// Check if ledger object is there
bool
objectExists(uint256 const& objID, Env& env);

// Extract credentials from account_object object
Credentials
credentialsFromJson(
    json::Value const& object,
    std::unordered_map<std::string, Account> const& human2Acc);

// Sort credentials the same way as PermissionedDomainSet
Credentials
sortCredentials(Credentials const& input);

// Get newly created domain from transaction metadata.
uint256
getNewDomain(std::shared_ptr<STObject const> const& meta);

}  // namespace xrpl::test::jtx::pdomain
