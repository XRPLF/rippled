//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2021 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_APP_RDB_WALLET_H_INCLUDED
#define RIPPLE_APP_RDB_WALLET_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/misc/Manifest.h>
#include <ripple/core/Config.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/overlay/PeerReservationTable.h>
#include <ripple/peerfinder/impl/Store.h>

namespace ripple {

/**
 * @brief makeWalletDB Opens the wallet database and returns it.
 * @param setup Path to the database and other opening parameters.
 * @return Unique pointer to the database descriptor.
 */
std::unique_ptr<DatabaseCon>
makeWalletDB(DatabaseCon::Setup const& setup);

/**
 * @brief makeTestWalletDB Opens a test wallet database with an arbitrary name.
 * @param setup Path to the database and other opening parameters.
 * @param dbname Name of the database.
 * @return Unique pointer to the database descriptor.
 */
std::unique_ptr<DatabaseCon>
makeTestWalletDB(DatabaseCon::Setup const& setup, std::string const& dbname);

/**
 * @brief getManifests Loads a manifest from the wallet database and stores it
 *        in the cache.
 * @param session Session with the database.
 * @param dbTable Name of the database table from which the manifest will be
 *        extracted.
 * @param mCache Cache for storing the manifest.
 * @param j Journal.
 */
void
getManifests(
    soci::session& session,
    std::string const& dbTable,
    ManifestCache& mCache,
    beast::Journal j);

/**
 * @brief saveManifests Saves all given manifests to the database.
 * @param session Session with the database.
 * @param dbTable Name of the database table that will store the manifest.
 * @param isTrusted Callback that returns true if the key is trusted.
 * @param map Maps public keys to manifests.
 * @param j Journal.
 */
void
saveManifests(
    soci::session& session,
    std::string const& dbTable,
    std::function<bool(PublicKey const&)> const& isTrusted,
    hash_map<PublicKey, Manifest> const& map,
    beast::Journal j);

/**
 * @brief addValidatorManifest Saves the manifest of a validator to the
 *        database.
 * @param session Session with the database.
 * @param serialized Manifest of the validator in raw format.
 */
void
addValidatorManifest(soci::session& session, std::string const& serialized);

/** Delete any saved public/private key associated with this node. */
void
clearNodeIdentity(soci::session& session);

/** Returns a stable public and private key for this node.

    The node's public identity is defined by a secp256k1 keypair
    that is (normally) randomly generated. This function will
    return such a keypair, securely generating one if needed.

    @param session Session with the database.

    @return Pair of public and private secp256k1 keys.
 */
std::pair<PublicKey, SecretKey>
getNodeIdentity(soci::session& session);

/**
 * @brief getPeerReservationTable Returns the peer reservation table.
 * @param session Session with the database.
 * @param j Journal.
 * @return Peer reservation hash table.
 */
std::unordered_set<PeerReservation, beast::uhash<>, KeyEqual>
getPeerReservationTable(soci::session& session, beast::Journal j);

/**
 * @brief insertPeerReservation Adds an entry to the peer reservation table.
 * @param session Session with the database.
 * @param nodeId Public key of the node.
 * @param description Description of the node.
 */
void
insertPeerReservation(
    soci::session& session,
    PublicKey const& nodeId,
    std::string const& description);

/**
 * @brief deletePeerReservation Deletes an entry from the peer reservation
 *        table.
 * @param session Session with the database.
 * @param nodeId Public key of the node to remove.
 */
void
deletePeerReservation(soci::session& session, PublicKey const& nodeId);

/**
 * @brief createFeatureVotes Creates the FeatureVote table if it does not exist.
 * @param session Session with the wallet database.
 * @return true if the table already exists
 */
bool
createFeatureVotes(soci::session& session);

// For historical reasons the up-vote and down-vote integer representations
// are unintuitive.
enum class AmendmentVote : int { up = 0, down = 1 };

/**
 * @brief readAmendments Reads all amendments from the FeatureVotes table.
 * @param session Session with the wallet database.
 * @param callback Callback called for each amendment with its hash, name and
 *        optionally a flag denoting whether the amendment should be vetoed.
 */
void
readAmendments(
    soci::session& session,
    std::function<void(
        boost::optional<std::string> amendment_hash,
        boost::optional<std::string> amendment_name,
        boost::optional<AmendmentVote> vote)> const& callback);

/**
 * @brief voteAmendment Set the veto value for a particular amendment.
 * @param session Session with the wallet database.
 * @param amendment Hash of the amendment.
 * @param name Name of the amendment.
 * @param vote Whether to vote in favor of this amendment.
 */
void
voteAmendment(
    soci::session& session,
    uint256 const& amendment,
    std::string const& name,
    AmendmentVote vote);

}  // namespace ripple

#endif
