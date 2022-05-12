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

#ifndef RIPPLE_APP_RDB_UNITARYSHARD_H_INCLUDED
#define RIPPLE_APP_RDB_UNITARYSHARD_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/app/rdb/RelationalDatabase.h>
#include <ripple/core/Config.h>
#include <ripple/protocol/RippleLedgerHash.h>
#include <boost/filesystem.hpp>

namespace ripple {

struct DatabasePair
{
    std::unique_ptr<DatabaseCon> ledgerDb;
    std::unique_ptr<DatabaseCon> transactionDb;
};

/**
 * @brief makeShardCompleteLedgerDBs Opens shard databases for verified shards
 *        and returns their descriptors.
 * @param config Config object.
 * @param setup Path to the databases and other opening parameters.
 * @return Pair of unique pointers to the opened ledger and transaction
 *         databases.
 */
DatabasePair
makeShardCompleteLedgerDBs(
    Config const& config,
    DatabaseCon::Setup const& setup);

/**
 * @brief makeShardIncompleteLedgerDBs Opens shard databases for partially
 *        downloaded or unverified shards and returns their descriptors.
 * @param config Config object.
 * @param setup Path to the databases and other opening parameters.
 * @param checkpointerSetup Checkpointer parameters.
 * @return Pair of unique pointers to the opened ledger and transaction
 *         databases.
 */
DatabasePair
makeShardIncompleteLedgerDBs(
    Config const& config,
    DatabaseCon::Setup const& setup,
    DatabaseCon::CheckpointerSetup const& checkpointerSetup);

/**
 * @brief updateLedgerDBs Saves the given ledger to shard databases.
 * @param txdb Session with the transaction databases.
 * @param lgrdb Session with the ledger databases.
 * @param ledger Ledger to save.
 * @param index Index of the shard that owns the ledger.
 * @param stop Reference to an atomic flag that can stop the process if raised.
 * @param j Journal
 * @return True if the ledger was successfully saved.
 */
bool
updateLedgerDBs(
    soci::session& txdb,
    soci::session& lgrdb,
    std::shared_ptr<Ledger const> const& ledger,
    std::uint32_t index,
    std::atomic<bool>& stop,
    beast::Journal j);

/**
 * @brief makeAcquireDB Opens the shard acquire database and returns its
 *        descriptor.
 * @param setup Path to the database and other opening parameters.
 * @param checkpointerSetup Checkpointer parameters.
 * @return Unique pointer to the opened database.
 */
std::unique_ptr<DatabaseCon>
makeAcquireDB(
    DatabaseCon::Setup const& setup,
    DatabaseCon::CheckpointerSetup const& checkpointerSetup);

/**
 * @brief insertAcquireDBIndex Adds a new shard index to the shard acquire
 *        database.
 * @param session Session with the database.
 * @param index Index to add.
 */
void
insertAcquireDBIndex(soci::session& session, std::uint32_t index);

/**
 * @brief selectAcquireDBLedgerSeqs Returns the set of acquired ledgers for
 *        the given shard.
 * @param session Session with the database.
 * @param index Shard index.
 * @return Pair which contains true if such an index was found in the database,
 *         and a string which contains the set of ledger sequences.
 *         If no sequences were saved then the optional will have no value.
 */
std::pair<bool, std::optional<std::string>>
selectAcquireDBLedgerSeqs(soci::session& session, std::uint32_t index);

struct AcquireShardSeqsHash
{
    std::optional<std::string> sequences;
    std::optional<std::string> hash;
};

/**
 * @brief selectAcquireDBLedgerSeqsHash Returns the set of acquired ledger
 *        sequences and the last ledger hash for the shard with the provided
 *        index.
 * @param session Session with the database.
 * @param index Shard index.
 * @return Pair which contains true if such an index was found in the database
 *         and the AcquireShardSeqsHash structure which contains a string with
 *         the ledger sequences and a string with last ledger hash. If the set
 *         of sequences or hash were not saved then no value is returned.
 */
std::pair<bool, AcquireShardSeqsHash>
selectAcquireDBLedgerSeqsHash(soci::session& session, std::uint32_t index);

/**
 * @brief updateAcquireDB Updates information in the acquire DB.
 * @param session Session with the database.
 * @param ledger Ledger to save into the database.
 * @param index Shard index.
 * @param lastSeq Last acquired ledger sequence.
 * @param seqs Current set of acquired ledger sequences if it's not empty.
 */
void
updateAcquireDB(
    soci::session& session,
    std::shared_ptr<Ledger const> const& ledger,
    std::uint32_t index,
    std::uint32_t lastSeq,
    std::optional<std::string> const& seqs);

}  // namespace ripple

#endif
