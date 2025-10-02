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

#ifndef XRPL_TEST_JTX_PERMISSIONED_DOMAINS_H_INCLUDED
#define XRPL_TEST_JTX_PERMISSIONED_DOMAINS_H_INCLUDED

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/deposit.h>

namespace ripple {
namespace test {
namespace jtx {
namespace pdomain {

// Helpers for PermissionedDomains testing
using Credential = ripple::test::jtx::deposit::AuthorizeCredentials;
using Credentials = std::vector<Credential>;

// helpers
// Make json for PermissionedDomainSet transaction
Json::Value
setTx(
    AccountID const& account,
    Credentials const& credentials,
    std::optional<uint256> domain = std::nullopt);

// Make json for PermissionedDomainDelete transaction
Json::Value
deleteTx(AccountID const& account, uint256 const& domain);

// Get PermissionedDomain objects from account_objects rpc call
std::map<uint256, Json::Value>
getObjects(Account const& account, Env& env, bool withType = true);

// Check if ledger object is there
bool
objectExists(uint256 const& objID, Env& env);

// Extract credentials from account_object object
Credentials
credentialsFromJson(
    Json::Value const& object,
    std::unordered_map<std::string, Account> const& human2Acc);

// Sort credentials the same way as PermissionedDomainSet
Credentials
sortCredentials(Credentials const& input);

// Get newly created domain from transaction metadata.
uint256
getNewDomain(std::shared_ptr<STObject const> const& meta);

}  // namespace pdomain
}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
