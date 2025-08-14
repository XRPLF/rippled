//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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

#ifndef RIPPLE_RPC_RPCHELPERS_H_INCLUDED
#define RIPPLE_RPC_RPCHELPERS_H_INCLUDED

#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Status.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/proto/org/xrpl/rpc/v1/xrp_ledger.pb.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/SecretKey.h>

#include <optional>
namespace ripple {

class ReadView;

namespace RPC {

struct JsonContext;

/** Gets the start hint for traversing account objects
 * @param sle - Ledger entry defined by the marker passed into the RPC.
 * @param accountID - The ID of the account whose objects you are traversing.
 */
std::uint64_t
getStartHint(std::shared_ptr<SLE const> const& sle, AccountID const& accountID);

/**
 * Tests if a SLE is owned by accountID.
 * @param ledger - The ledger used to search for the sle.
 * @param sle - The SLE to test for ownership.
 * @param account - The account being tested for SLE ownership.
 */
bool
isRelatedToAccount(
    ReadView const& ledger,
    std::shared_ptr<SLE const> const& sle,
    AccountID const& accountID);

hash_set<AccountID>
parseAccountIds(Json::Value const& jvArray);

/** Retrieve the limit value from a JsonContext, or set a default -
    then restrict the limit by max and min if not an ADMIN request.

    If there is an error, return it as JSON.
*/
std::optional<Json::Value>
readLimitField(
    unsigned int& limit,
    Tuning::LimitRange const&,
    JsonContext const&);

std::optional<Seed>
getSeedFromRPC(Json::Value const& params, Json::Value& error);

std::optional<Seed>
parseRippleLibSeed(Json::Value const& params);

std::pair<RPC::Status, LedgerEntryType>
chooseLedgerEntryType(Json::Value const& params);

/**
 * Check if the type is a valid filtering type for account_objects method
 *
 * Since Amendments, DirectoryNode, FeeSettings, LedgerHashes can not be
 * owned by an account, this function will return false in these situations.
 */
bool
isAccountObjectsValidType(LedgerEntryType const& type);

std::optional<std::pair<PublicKey, SecretKey>>
keypairForSignature(
    Json::Value const& params,
    Json::Value& error,
    unsigned int apiVersion = apiVersionIfUnspecified);

}  // namespace RPC

}  // namespace ripple

#endif
