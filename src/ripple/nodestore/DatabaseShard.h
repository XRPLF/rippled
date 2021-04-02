//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2017 Ripple Labs Inc.

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

#ifndef RIPPLE_NODESTORE_DATABASESHARD_H_INCLUDED
#define RIPPLE_NODESTORE_DATABASESHARD_H_INCLUDED

#include <ripple/app/ledger/Ledger.h>
#include <ripple/basics/RangeSet.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/nodestore/Database.h>
#include <ripple/nodestore/Types.h>

#include <memory>
#include <optional>

namespace ripple {
namespace NodeStore {

/** A collection of historical shards
 */
class DatabaseShard : public Database
{
public:
    /** Construct a shard store

        @param scheduler The scheduler to use for performing asynchronous tasks
        @param readThreads The number of asynchronous read threads to create
        @param config The shard configuration section for the database
        @param journal Destination for logging output
    */
    DatabaseShard(
        Scheduler& scheduler,
        int readThreads,
        Section const& config,
        beast::Journal journal)
        : Database(scheduler, readThreads, config, journal)
    {
    }

    /** Initialize the database

        @return `true` if the database initialized without error
    */
    virtual bool
    init() = 0;

    /** Prepare to store a new ledger in the shard being acquired

        @param validLedgerSeq The sequence of the maximum valid ledgers
        @return If a ledger should be fetched and stored, then returns the
        ledger sequence of the ledger to request. Otherwise returns
        std::nullopt.
                Some reasons this may return std::nullopt are: all shards are
                stored and full, max allowed disk space would be exceeded, or a
                ledger was recently requested and not enough time has passed
                between requests.
        @implNote adds a new writable shard if necessary
    */
    virtual std::optional<std::uint32_t>
    prepareLedger(std::uint32_t validLedgerSeq) = 0;

    /** Prepare one or more shard indexes to be imported into the database

        @param shardIndexes Shard indexes to be prepared for import
        @return true if all shard indexes successfully prepared for import
    */
    virtual bool
    prepareShards(std::vector<std::uint32_t> const& shardIndexes) = 0;

    /** Remove a previously prepared shard index for import

        @param shardIndex Shard index to be removed from import
    */
    virtual void
    removePreShard(std::uint32_t shardIndex) = 0;

    /** Get shard indexes being imported

        @return a string representing the shards prepared for import
    */
    virtual std::string
    getPreShards() = 0;

    /** Import a shard into the shard database

        @param shardIndex Shard index to import
        @param srcDir The directory to import from
        @return true If the shard was successfully imported
        @implNote if successful, srcDir is moved to the database directory
    */
    virtual bool
    importShard(
        std::uint32_t shardIndex,
        boost::filesystem::path const& srcDir) = 0;

    /** Fetch a ledger from the shard store

        @param hash The key of the ledger to retrieve
        @param seq The sequence of the ledger
        @return The ledger if found, nullptr otherwise
    */
    virtual std::shared_ptr<Ledger>
    fetchLedger(uint256 const& hash, std::uint32_t seq) = 0;

    /** Notifies the database that the given ledger has been
        fully acquired and stored.

        @param ledger The stored ledger to be marked as complete
    */
    virtual void
    setStored(std::shared_ptr<Ledger const> const& ledger) = 0;

    /** Query which complete shards are stored

        @return the indexes of complete shards
    */
    virtual std::string
    getCompleteShards() = 0;

    /**
     * @brief callForLedgerSQL Checkouts ledger database for shard
     *        containing given ledger and calls given callback function passing
     *        shard index and session with the database to it.
     * @param ledgerSeq Ledger sequence.
     * @param callback Callback function to call.
     * @return Value returned by callback function.
     */
    virtual bool
    callForLedgerSQL(
        LedgerIndex ledgerSeq,
        std::function<bool(soci::session& session, std::uint32_t index)> const&
            callback) = 0;

    /**
     * @brief callForTransactionSQL Checkouts transaction database for shard
     *        containing given ledger and calls given callback function passing
     *        shard index and session with the database to it.
     * @param ledgerSeq Ledger sequence.
     * @param callback Callback function to call.
     * @return Value returned by callback function.
     */
    virtual bool
    callForTransactionSQL(
        LedgerIndex ledgerSeq,
        std::function<bool(soci::session& session, std::uint32_t index)> const&
            callback) = 0;

    /**
     * @brief iterateLedgerSQLsForward Checkouts ledger databases for all
     *        shards in ascending order starting from given shard index until
     *        shard with the largest index visited or callback returned false.
     *        For each visited shard calls given callback function passing
     *        shard index and session with the database to it.
     * @param minShardIndex Start shard index to visit or none if all shards
     *        should be visited.
     * @param callback Callback function to call.
     * @return True if each callback function returns true, false otherwise.
     */
    virtual bool
    iterateLedgerSQLsForward(
        std::optional<std::uint32_t> minShardIndex,
        std::function<bool(soci::session& session, std::uint32_t index)> const&
            callback) = 0;

    /**
     * @brief iterateTransactionSQLsForward Checkouts transaction databases for
     *        all shards in ascending order starting from given shard index
     *        until shard with the largest index visited or callback returned
     *        false. For each visited shard calls given callback function
     *        passing shard index and session with the database to it.
     * @param minShardIndex Start shard index to visit or none if all shards
     *        should be visited.
     * @param callback Callback function to call.
     * @return True if each callback function returns true, false otherwise.
     */
    virtual bool
    iterateTransactionSQLsForward(
        std::optional<std::uint32_t> minShardIndex,
        std::function<bool(soci::session& session, std::uint32_t index)> const&
            callback) = 0;

    /**
     * @brief iterateLedgerSQLsBack Checkouts ledger databases for
     *        all shards in descending order starting from given shard index
     *        until shard with the smallest index visited or callback returned
     *        false. For each visited shard calls given callback function
     *        passing shard index and session with the database to it.
     * @param maxShardIndex Start shard index to visit or none if all shards
     *        should be visited.
     * @param callback Callback function to call.
     * @return True if each callback function returns true, false otherwise.
     */
    virtual bool
    iterateLedgerSQLsBack(
        std::optional<std::uint32_t> maxShardIndex,
        std::function<bool(soci::session& session, std::uint32_t index)> const&
            callback) = 0;

    /**
     * @brief iterateTransactionSQLsBack Checkouts transaction databases for
     *        all shards in descending order starting from given shard index
     *        until shard with the smallest index visited or callback returned
     *        false. For each visited shard calls given callback function
     *        passing shard index and session with the database to it.
     * @param maxShardIndex Start shard index to visit or none if all shards
     *        should be visited.
     * @param callback Callback function to call.
     * @return True if each callback function returns true, false otherwise.
     */
    virtual bool
    iterateTransactionSQLsBack(
        std::optional<std::uint32_t> maxShardIndex,
        std::function<bool(soci::session& session, std::uint32_t index)> const&
            callback) = 0;

    /** @return The maximum number of ledgers stored in a shard
     */
    virtual std::uint32_t
    ledgersPerShard() const = 0;

    /** @return The earliest shard index
     */
    virtual std::uint32_t
    earliestShardIndex() const = 0;

    /** Calculates the shard index for a given ledger sequence

        @param seq ledger sequence
        @return The shard index of the ledger sequence
    */
    virtual std::uint32_t
    seqToShardIndex(std::uint32_t seq) const = 0;

    /** Calculates the first ledger sequence for a given shard index

        @param shardIndex The shard index considered
        @return The first ledger sequence pertaining to the shard index
    */
    virtual std::uint32_t
    firstLedgerSeq(std::uint32_t shardIndex) const = 0;

    /** Calculates the last ledger sequence for a given shard index

        @param shardIndex The shard index considered
        @return The last ledger sequence pertaining to the shard index
    */
    virtual std::uint32_t
    lastLedgerSeq(std::uint32_t shardIndex) const = 0;

    /** Returns the root database directory
     */
    virtual boost::filesystem::path const&
    getRootDir() const = 0;

    /** The number of ledgers in a shard */
    static constexpr std::uint32_t ledgersPerShardDefault{16384u};
};

constexpr std::uint32_t
seqToShardIndex(
    std::uint32_t ledgerSeq,
    std::uint32_t ledgersPerShard = DatabaseShard::ledgersPerShardDefault)
{
    return (ledgerSeq - 1) / ledgersPerShard;
}

extern std::unique_ptr<DatabaseShard>
make_ShardStore(
    Application& app,
    Scheduler& scheduler,
    int readThreads,
    beast::Journal j);

}  // namespace NodeStore
}  // namespace ripple

#endif
