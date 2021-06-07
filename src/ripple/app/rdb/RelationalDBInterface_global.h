//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#ifndef RIPPLE_CORE_RELATIONALDBINTERFACE_GLOBAL_H_INCLUDED
#define RIPPLE_CORE_RELATIONALDBINTERFACE_GLOBAL_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/misc/Manifest.h>
#include <ripple/core/Config.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/overlay/PeerReservationTable.h>
#include <ripple/peerfinder/impl/Store.h>
#include <boost/filesystem.hpp>

namespace ripple {

/* Wallet DB */

/**
 * @brief makeWalletDB Opens wallet DB and returns it.
 * @param setup Path to database and other opening parameters.
 * @return Unique pointer to database descriptor.
 */
std::unique_ptr<DatabaseCon>
makeWalletDB(DatabaseCon::Setup const& setup);

/**
 * @brief makeTestWalletDB Opens test wallet DB with arbitrary name.
 * @param setup Path to database and other opening parameters.
 * @param dbname Name of database.
 * @return Unique pointer to database descriptor.
 */
std::unique_ptr<DatabaseCon>
makeTestWalletDB(DatabaseCon::Setup const& setup, std::string const& dbname);

/**
 * @brief getManifests Loads manifest from wallet DB and stores it in the cache.
 * @param session Session with database.
 * @param dbTable Name of table in the database to extract manifest from.
 * @param mCache Cache to store manifest.
 * @param j Journal.
 */
void
getManifests(
    soci::session& session,
    std::string const& dbTable,
    ManifestCache& mCache,
    beast::Journal j);

/**
 * @brief saveManifests Saves all given manifests to database.
 * @param session Session with database.
 * @param dbTable Name of database table to save manifest into.
 * @param isTrusted Callback returned true if key is trusted.
 * @param map Map to save which points public keys to manifests.
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
 * @brief addValidatorManifest Saves manifest of validator to database.
 * @param session Session with database.
 * @param serialized Manifest of validator in raw format.
 */
void
addValidatorManifest(soci::session& session, std::string const& serialized);

/**
 * @brief getNodeIdentity Returns public and private keys of this node.
 * @param session Session with database.
 * @return Pair of public and private keys.
 */
std::pair<PublicKey, SecretKey>
getNodeIdentity(soci::session& session);

/**
 * @brief getPeerReservationTable Returns peer reservation table.
 * @param session Session with database.
 * @param j Journal.
 * @return Peer reservation hash table.
 */
std::unordered_set<PeerReservation, beast::uhash<>, KeyEqual>
getPeerReservationTable(soci::session& session, beast::Journal j);

/**
 * @brief insertPeerReservation Adds entry to peer reservation table.
 * @param session Session with database.
 * @param nodeId public key of node.
 * @param description Description of node.
 */
void
insertPeerReservation(
    soci::session& session,
    PublicKey const& nodeId,
    std::string const& description);

/**
 * @brief deletePeerReservation Deletes entry from peer reservation table.
 * @param session Session with database.
 * @param nodeId Public key of node to remove.
 */
void
deletePeerReservation(soci::session& session, PublicKey const& nodeId);

/**
 * @brief createFeatureVotes Creates FeatureVote table if it is not exists.
 * @param session Session with walletDB database.
 * @return true if the table already exists
 */
bool
createFeatureVotes(soci::session& session);

/**
 * @brief readAmendments Read all amendments from FeatureVotes table.
 * @param session Session with walletDB database.
 * @param callback Callback called for each amendment passing its hash, name
 *        and teh flag if it should be vetoed as callback parameters
 */
void
readAmendments(
    soci::session& session,
    std::function<void(
        boost::optional<std::string> amendment_hash,
        boost::optional<std::string> amendment_name,
        boost::optional<int> vote_to_veto)> const& callback);

/**
 * @brief voteAmendment Set veto value for particular amendment.
 * @param session Session with walletDB database.
 * @param amendment Hash of amendment.
 * @param name Name of amendment.
 * @param vote_to_veto Trus if this amendment should be vetoed.
 */
void
voteAmendment(
    soci::session& session,
    uint256 const& amendment,
    std::string const& name,
    bool vote_to_veto);

/* State DB */

struct SavedState
{
    std::string writableDb;
    std::string archiveDb;
    LedgerIndex lastRotated;
};

/**
 * @brief initStateDB Opens DB session with State DB.
 * @param session Structure to open session in.
 * @param config Path to database and other opening parameters.
 * @param dbName Name of database.
 */
void
initStateDB(
    soci::session& session,
    BasicConfig const& config,
    std::string const& dbName);

/**
 * @brief getCanDelete Returns ledger sequence which can be deleted.
 * @param session Session with database.
 * @return Ledger sequence.
 */
LedgerIndex
getCanDelete(soci::session& session);

/**
 * @brief setCanDelete Updates ledger sequence which can be deleted.
 * @param session Session with database.
 * @param canDelete Ledger sequence to save.
 * @return Previous value of ledger sequence whic can be deleted.
 */
LedgerIndex
setCanDelete(soci::session& session, LedgerIndex canDelete);

/**
 * @brief getSavedState Returns saved state.
 * @param session Session with database.
 * @return The SavedState structure which contains names of
 *         writable DB, archive DB and last rotated ledger sequence.
 */
SavedState
getSavedState(soci::session& session);

/**
 * @brief setSavedState Saves given state.
 * @param session Session with database.
 * @param state The SavedState structure which contains names of
 *        writable DB, archive DB and last rotated ledger sequence.
 */
void
setSavedState(soci::session& session, SavedState const& state);

/**
 * @brief setLastRotated Updates last rotated ledger sequence.
 * @param session Session with database.
 * @param seq New value of last rotated ledger sequence.
 */
void
setLastRotated(soci::session& session, LedgerIndex seq);

/* DatabaseBody DB */

/**
 * @brief openDatabaseBodyDb Opens file download DB and returns its descriptor.
 *        Start new download process or continue existing one.
 * @param setup Path to database and other opening parameters.
 * @param path Path of new file to download.
 * @return Pair of unique pointer to database and current downloaded size
 *         if download process continues.
 */
std::pair<std::unique_ptr<DatabaseCon>, std::optional<std::uint64_t>>
openDatabaseBodyDb(
    DatabaseCon::Setup const& setup,
    boost::filesystem::path const& path);

/**
 * @brief databaseBodyDoPut Saves new fragment of downloaded file.
 * @param session Session with database.
 * @param data Downloaded piece to file data tp save.
 * @param path Path of downloading file.
 * @param fileSize Size of downloaded piece of file.
 * @param part Sequence number of downloaded file part.
 * @param maxRowSizePad Maximum size of file part to save.
 * @return Number of saved parts. Downloaded piece may be splitted
 *         into several parts of size not large that maxRowSizePad.
 */
std::uint64_t
databaseBodyDoPut(
    soci::session& session,
    std::string const& data,
    std::string const& path,
    std::uint64_t fileSize,
    std::uint64_t part,
    std::uint16_t maxRowSizePad);

/**
 * @brief databaseBodyFinish Finishes download process and writes file to disk.
 * @param session Session with database.
 * @param fout Opened file to write downloaded data from database.
 */
void
databaseBodyFinish(soci::session& session, std::ofstream& fout);

/* Vacuum DB */

/**
 * @brief doVacuumDB Creates, initialises DB, and performs its cleanup.
 * @param setup Path to database and other opening parameters.
 * @return True if vacuum process completed successfully.
 */
bool
doVacuumDB(DatabaseCon::Setup const& setup);

/* PeerFinder DB */

/**
 * @brief initPeerFinderDB Opens session with peer finder database.
 * @param session Structure to open session in.
 * @param config Path to database and other opening parameters.
 * @param j Journal.
 */
void
initPeerFinderDB(
    soci::session& session,
    BasicConfig const& config,
    beast::Journal j);

/**
 * @brief updatePeerFinderDB Update peer finder DB to new version.
 * @param session Session with database.
 * @param currentSchemaVersion New version of database.
 * @param j Journal.
 */
void
updatePeerFinderDB(
    soci::session& session,
    int currentSchemaVersion,
    beast::Journal j);

/**
 * @brief readPeerFinderDB Read all entries from peer finder DB and call
 *        given callback for each entry.
 * @param session Session with database.
 * @param func Callback to call for each entry.
 */
void
readPeerFinderDB(
    soci::session& session,
    std::function<void(std::string const&, int)> const& func);

/**
 * @brief savePeerFinderDB Save new entry to peer finder DB.
 * @param session Session with database.
 * @param v Entry to save which contains information about new peer.
 */
void
savePeerFinderDB(
    soci::session& session,
    std::vector<PeerFinder::Store::Entry> const& v);

}  // namespace ripple

#endif
