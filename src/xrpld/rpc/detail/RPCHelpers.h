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

/**
 * @brief Gets the start hint for traversing account objects.
 *
 * This function retrieves a hint value from the specified ledger entry (SLE)
 * that can be used to optimize traversal of account objects for the given
 * account ID.
 *
 * @param sle Shared pointer to the ledger entry (SLE) defined by the marker
 * passed into the RPC.
 * @param accountID The ID of the account whose objects are being traversed.
 * @return A 64-bit unsigned integer representing the start hint for traversal.
 */
std::uint64_t
getStartHint(std::shared_ptr<SLE const> const& sle, AccountID const& accountID);

/**
 * @brief Tests if a ledger entry (SLE) is owned by the specified account.
 *
 * Determines whether the given SLE is related to or owned by the provided
 * account ID within the context of the specified ledger.
 *
 * @param ledger The ledger view used to search for the SLE.
 * @param sle Shared pointer to the SLE to test for ownership.
 * @param accountID The account being tested for SLE ownership.
 * @return true if the SLE is owned by the account, false otherwise.
 */
bool
isRelatedToAccount(
    ReadView const& ledger,
    std::shared_ptr<SLE const> const& sle,
    AccountID const& accountID);

/**
 * @brief Parses an array of account IDs from a JSON value.
 *
 * Extracts and returns a set of AccountID objects from the provided JSON array.
 *
 * @param jvArray The JSON value containing an array of account IDs.
 * @return A hash_set containing the parsed AccountID objects.
 */
hash_set<AccountID>
parseAccountIds(Json::Value const& jvArray);

/**
 * @brief Retrieves the limit value from a JsonContext or sets a default.
 *
 * Reads the "limit" field from the given JsonContext, applies default, minimum,
 * and maximum constraints as appropriate, and returns an error as JSON if
 * validation fails.
 *
 * @param limit Reference to the variable where the limit value will be stored.
 * @param range The allowed range for the limit value.
 * @param context The JSON context from which to read the limit.
 * @return An optional JSON value containing an error if one occurred, or
 * std::nullopt on success.
 */
std::optional<Json::Value>
readLimitField(
    unsigned int& limit,
    Tuning::LimitRange const& range,
    JsonContext const& context);

/**
 * @brief Extracts a Seed from RPC parameters.
 *
 * Attempts to parse and return a Seed from the provided JSON RPC parameters.
 * If parsing fails, an error is set in the provided error JSON value.
 *
 * @param params The JSON value containing RPC parameters.
 * @param error Reference to a JSON value to be set with error information if
 * parsing fails.
 * @return An optional Seed if parsing is successful, or std::nullopt otherwise.
 */
std::optional<Seed>
getSeedFromRPC(Json::Value const& params, Json::Value& error);

/**
 * @brief Parses a RippleLib seed from RPC parameters.
 *
 * Attempts to extract and return a Seed from the provided JSON parameters using
 * RippleLib conventions.
 *
 * @param params The JSON value containing RPC parameters.
 * @return An optional Seed if parsing is successful, or std::nullopt otherwise.
 */
std::optional<Seed>
parseRippleLibSeed(Json::Value const& params);

/**
 * @brief Chooses the ledger entry type based on RPC parameters.
 *
 * Determines the appropriate LedgerEntryType to use based on the provided JSON
 * parameters, and returns a pair containing the RPC status and the selected
 * type.
 *
 * @param params The JSON value containing RPC parameters.
 * @return A pair consisting of the RPC status and the chosen LedgerEntryType.
 */
std::pair<RPC::Status, LedgerEntryType>
chooseLedgerEntryType(Json::Value const& params);

/**
 * @brief Checks if the type is a valid filtering type for the account_objects
 * method.
 *
 * Determines whether the specified LedgerEntryType is valid for filtering in
 * the account_objects RPC method. Types such as Amendments, DirectoryNode,
 * FeeSettings, and LedgerHashes are not considered valid as they cannot be
 * owned by an account.
 *
 * @param type The ledger entry type to check.
 * @return true if the type is valid for account_objects filtering, false
 * otherwise.
 */
bool
isAccountObjectsValidType(LedgerEntryType const& type);

/**
 * @brief Generates a keypair for signature from RPC parameters.
 *
 * Attempts to derive a public and secret key pair from the provided JSON RPC
 * parameters. If an error occurs, it is set in the provided error JSON value.
 *
 * @param params The JSON value containing RPC parameters.
 * @param error Reference to a JSON value to be set with error information if
 * keypair generation fails.
 * @param apiVersion The API version to use for keypair derivation (defaults to
 * apiVersionIfUnspecified).
 * @return An optional pair containing the public and secret keys if successful,
 * or std::nullopt otherwise.
 */
std::optional<std::pair<PublicKey, SecretKey>>
keypairForSignature(
    Json::Value const& params,
    Json::Value& error,
    unsigned int apiVersion = apiVersionIfUnspecified);

}  // namespace RPC

}  // namespace ripple

#endif
