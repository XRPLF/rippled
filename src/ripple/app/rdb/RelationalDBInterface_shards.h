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

#ifndef RIPPLE_CORE_RELATIONALDBINTERFACE_SHARDS_H_INCLUDED
#define RIPPLE_CORE_RELATIONALDBINTERFACE_SHARDS_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/rdb/RelationalDBInterface.h>
#include <ripple/core/Config.h>
#include <boost/filesystem.hpp>

namespace ripple {

struct DatabasePair
{
    std::unique_ptr<DatabaseCon> ledgerDb;
    std::unique_ptr<DatabaseCon> transactionDb;
};

/* Shard DB */

/**
 * @brief makeShardCompleteLedgerDBs Opens shard databases for already
 *        verified shard and returns its descriptors.
 * @param config Config object.
 * @param setup Path to database and other opening parameters.
 * @return Pair of unique pointers to opened ledger and transaction databases.
 */
DatabasePair
makeShardCompleteLedgerDBs(
    Config const& config,
    DatabaseCon::Setup const& setup);

/**
 * @brief makeShardIncompleteLedgerDBs Opens shard databases for not
 *        fully downloaded or verified shard and returns its descriptors.
 * @param config Config object.
 * @param setup Path to database and other opening parameters.
 * @param checkpointerSetup Checkpointer parameters.
 * @return Pair of unique pointers to opened ledger and transaction databases.
 */
DatabasePair
makeShardIncompleteLedgerDBs(
    Config const& config,
    DatabaseCon::Setup const& setup,
    DatabaseCon::CheckpointerSetup const& checkpointerSetup);

/**
 * @brief updateLedgerDBs Save given ledger to shard databases.
 * @param txdb Session with transaction DB.
 * @param lgrdb Sessiob with ledger DB.
 * @param ledger Ledger to save.
 * @param index Index of the shard which the ledger belonfs to.
 * @param stop Link to atomic flag which can stop the process if raised.
 * @param j Journal
 * @return True if ledger was successfully saved.
 */
bool
updateLedgerDBs(
    soci::session& txdb,
    soci::session& lgrdb,
    std::shared_ptr<Ledger const> const& ledger,
    std::uint32_t index,
    std::atomic<bool>& stop,
    beast::Journal j);

/* Shard acquire DB */

/**
 * @brief makeAcquireDB Opens shard acquire DB and returns its descriptor.
 * @param setup Path to DB and other opening parameters.
 * @param checkpointerSetup Checkpointer parameters.
 * @return Uniqye pointer to opened database.
 */
std::unique_ptr<DatabaseCon>
makeAcquireDB(
    DatabaseCon::Setup const& setup,
    DatabaseCon::CheckpointerSetup const& checkpointerSetup);

/**
 * @brief insertAcquireDBIndex Adds new shard index to shard acquire DB.
 * @param session Session with database.
 * @param index Index to add.
 */
void
insertAcquireDBIndex(soci::session& session, std::uint32_t index);

/**
 * @brief selectAcquireDBLedgerSeqs Returns set of acquired ledgers for
 *        given shard.
 * @param session Session with database.
 * @param index Shard index.
 * @return Pair which contains true if such as index found in database,
 *         and string which contains set of ledger sequences.
 *         If set of sequences was not saved than none is returned.
 */
std::pair<bool, std::optional<std::string>>
selectAcquireDBLedgerSeqs(soci::session& session, std::uint32_t index);

struct AcquireShardSeqsHash
{
    std::optional<std::string> sequences;
    std::optional<std::string> hash;
};

/**
 * @brief selectAcquireDBLedgerSeqsHash Returns set of acquired ledgers and
 *        hash for given shard.
 * @param session Session with database.
 * @param index Shard index.
 * @return Pair which contains true of such an index found in database,
 *         and the AcquireShardSeqsHash structure which contains string
 *         with ledger sequences set and string with last ledger hash.
 *         If set of sequences or hash were not saved than none is returned.
 */
std::pair<bool, AcquireShardSeqsHash>
selectAcquireDBLedgerSeqsHash(soci::session& session, std::uint32_t index);

/**
 * @brief updateAcquireDB Updates information in acquire DB.
 * @param session Session with database.
 * @param ledger Ledger to save into database.
 * @param index Shard index.
 * @param lastSeq Last acqyured ledger sequence.
 * @param seqs Current set or acquired ledger sequences if it's not empty.
 */
void
updateAcquireDB(
    soci::session& session,
    std::shared_ptr<Ledger const> const& ledger,
    std::uint32_t index,
    std::uint32_t lastSeq,
    std::optional<std::string> const& seqs);

/* Archive DB */

/**
 * @brief makeArchiveDB Opens shard archive DB and returns its descriptor.
 * @param dir Path to database to open.
 * @param dbName Name of database.
 * @return Unique pointer to opened database.
 */
std::unique_ptr<DatabaseCon>
makeArchiveDB(boost::filesystem::path const& dir, std::string const& dbName);

/**
 * @brief readArchiveDB Read entries from shard archive database and calls
 *        fiven callback for each entry.
 * @param db Session with database.
 * @param func Callback to call for each entry.
 */
void
readArchiveDB(
    DatabaseCon& db,
    std::function<void(std::string const&, int)> const& func);

/**
 * @brief insertArchiveDB Adds entry to shard archive database.
 * @param db Session with database.
 * @param shardIndex Shard index to add.
 * @param url Shard download url to add.
 */
void
insertArchiveDB(
    DatabaseCon& db,
    std::uint32_t shardIndex,
    std::string const& url);

/**
 * @brief deleteFromArchiveDB Deletes entry from shard archive DB.
 * @param db Session with database.
 * @param shardIndex Shard index to remove from DB.
 */
void
deleteFromArchiveDB(DatabaseCon& db, std::uint32_t shardIndex);

/**
 * @brief dropArchiveDB Removes table in shard archive DB.
 * @param db Session with database.
 */
void
dropArchiveDB(DatabaseCon& db);

}  // namespace ripple

#endif
