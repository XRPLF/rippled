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

#pragma once

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {
namespace credentials {

// These function will be used by the code that use DepositPreauth / Credentials
// (and any future preauthorization modes) as part of authorization (all the
// transfer funds transactions)

// Check if credential sfExpiration field has passed ledger's parentCloseTime
bool
checkExpired(
    std::shared_ptr<SLE const> const& sleCredential,
    NetClock::time_point const& closed);

// Return true if any expired credential was found in arr (and deleted)
bool
removeExpired(ApplyView& view, STVector256 const& arr, beast::Journal const j);

// Actually remove a credentials object from the ledger
TER
deleteSLE(
    ApplyView& view,
    std::shared_ptr<SLE> const& sleCredential,
    beast::Journal j);

// Amendment and parameters checks for sfCredentialIDs field
NotTEC
checkFields(PreflightContext const& ctx);

// Accessing the ledger to check if provided credentials are valid. Do not use
// in doApply (only in preclaim) since it does not remove expired credentials.
// If you call it in prelaim, you also must call verifyDepositPreauth in doApply
TER
valid(PreclaimContext const& ctx, AccountID const& src);

// Check if subject has any credential maching the given domain. Do not use in
// doApply (only in preclaim) since it does not remove expired credentials. If
// you call it in prelaim, you also must call verifyDomain in doApply
TER
valid(ReadView const& view, uint256 domainID, AccountID const& subject);

// This function is only called when we about to return tecNO_PERMISSION
// because all the checks for the DepositPreauth authorization failed.
TER
authorizedDepositPreauth(
    ApplyView const& view,
    STVector256 const& ctx,
    AccountID const& dst);

// Sort credentials array, return empty set if there are duplicates
std::set<std::pair<AccountID, Slice>>
makeSorted(STArray const& credentials);

// Check credentials array passed to DepositPreauth/PermissionedDomainSet
// transactions
NotTEC
checkArray(STArray const& credentials, unsigned maxSize, beast::Journal j);

}  // namespace credentials

// Check expired credentials and for credentials maching DomainID of the ledger
// object
TER
verifyDomain(
    ApplyView& view,
    AccountID const& account,
    uint256 domainID,
    beast::Journal j);

// Check expired credentials and for existing DepositPreauth ledger object
TER
verifyDepositPreauth(
    ApplyContext& ctx,
    AccountID const& src,
    AccountID const& dst,
    std::shared_ptr<SLE> const& sleDst);

}  // namespace ripple
