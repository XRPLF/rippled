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

#ifndef RIPPLE_TEST_JTX_PERMISSIONEDDOMAINS_H_INCLUDED
#define RIPPLE_TEST_JTX_PERMISSIONEDDOMAINS_H_INCLUDED

#include <test/jtx.h>
#include <optional>

namespace ripple {
namespace test {
namespace jtx {
namespace pd {

// Helpers for PermissionedDomains testing
using Credential = std::pair<AccountID, Blob>;
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
getObjects(Account const& account, Env& env);

// Check if ledger object is there
bool
objectExists(uint256 const& objID, Env& env);

// Convert string to Blob
inline Blob
toBlob(std::string const& input)
{
    return Blob(input.begin(), input.end());
}

// Extract credentials from account_object object
Credentials
credentialsFromJson(Json::Value const& object);

// Sort credentials the same way as PermissionedDomainSet
std::optional<Credentials>
sortCredentials(Credentials const& input);

// Get account_info
Json::Value
ownerInfo(Account const& account, Env& env);

// Get newly created domain from transaction metadata.
uint256
getNewDomain(std::shared_ptr<STObject const> const& meta);

}  // namespace pd
}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif  // RIPPLE_TEST_JTX_PERMISSIONEDDOMAINS_H_INCLUDED
