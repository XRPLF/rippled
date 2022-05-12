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

#ifndef RIPPLE_NODESTORE_DATABASE_H_INCLUDED
#define RIPPLE_NODESTORE_DATABASE_H_INCLUDED

#include <ripple/basics/TaggedCache.h>
#include <ripple/nodestore/Backend.h>
#include <ripple/nodestore/NodeObject.h>
#include <ripple/nodestore/Scheduler.h>
#include <ripple/protocol/SystemParameters.h>

#include <condition_variable>
#include <thread>

namespace ripple {

class Ledger;

namespace NodeStore {

/** Persistency layer for NodeObject

    A Node is a ledger object which is uniquely identified by a key, which is
    the 256-bit hash of the body of the node. The payload is a variable length
    block of serialized data.

    All ledger data is stored as node objects and as such, needs to be persisted
    between launches. Furthermore, since the set of node objects will in
    general be larger than the amount of available memory, purged node objects
    which are later accessed must be retrieved from the node store.

    @see NodeObject
*/
class Database
{
public:
    Database() = delete;

    /** Construct the node store.

        @param scheduler The scheduler to use for performing asynchronous tasks.
        @param readThreads The number of asynchronous read threads to create.
        @param config The configuration settings
        @param journal Destination for logging output.
    */
    Database(
        Scheduler& scheduler,
        int readThreads,
        Section const& config,
        beast::Journal j);

    /** Destroy the node store.
        All pending operations are completed, pending writes flushed,
        and files closed before this returns.
    */
    virtual ~Database();

    /** Retrieve the name associated with this backend.
        This is used for diagnostics and may not reflect the actual path
        or paths used by the underlying backend.
    */
    virtual std::string
    getName() const = 0;

    /** Import objects from another database. */
    virtual void
    importDatabase(Database& source) = 0;

    /** Retrieve the estimated number of pending write operations.
        This is used for diagnostics.
    */
    virtual std::int32_t
    getWriteLoad() const = 0;

    /** Store the object.

        The caller's Blob parameter is overwritten.

        @param type The type of object.
        @param data The payload of the object. The caller's
                    variable is overwritten.
        @param hash The 256-bit hash of the payload data.
        @param ledgerSeq The sequence of the ledger the object belongs to.

        @return `true` if the object was stored?
    */
    virtual void
    store(
        NodeObjectType type,
        Blob&& data,
        uint256 const& hash,
        std::uint32_t ledgerSeq) = 0;

    /* Check if two ledgers are in the same database

        If these two sequence numbers map to the same database,
        the result of a fetch with either sequence number would
        be identical.

        @param s1 The first sequence number
        @param s2 The second sequence number

        @return 'true' if both ledgers would be in the same DB

    */
    virtual bool
    isSameDB(std::uint32_t s1, std::uint32_t s2) = 0;

    virtual void
    sync() = 0;

    /** Fetch a node object.
        If the object is known to be not in the database, isn't found in the
        database during the fetch, or failed to load correctly during the fetch,
        `nullptr` is returned.

        @note This can be called concurrently.
        @param hash The key of the object to retrieve.
        @param ledgerSeq The sequence of the ledger where the object is stored.
        @param fetchType the type of fetch, synchronous or asynchronous.
        @return The object, or nullptr if it couldn't be retrieved.
    */
    std::shared_ptr<NodeObject>
    fetchNodeObject(
        uint256 const& hash,
        std::uint32_t ledgerSeq = 0,
        FetchType fetchType = FetchType::synchronous,
        bool duplicate = false);

    /** Fetch an object without waiting.
        If I/O is required to determine whether or not the object is present,
        `false` is returned. Otherwise, `true` is returned and `object` is set
        to refer to the object, or `nullptr` if the object is not present.
        If I/O is required, the I/O is scheduled and `true` is returned

        @note This can be called concurrently.
        @param hash The key of the object to retrieve
        @param ledgerSeq The sequence of the ledger where the
                object is stored, used by the shard store.
        @param callback Callback function when read completes
    */
    virtual void
    asyncFetch(
        uint256 const& hash,
        std::uint32_t ledgerSeq,
        std::function<void(std::shared_ptr<NodeObject> const&)>&& callback);

    /** Store a ledger from a different database.

        @param srcLedger The ledger to store.
        @return true if the operation was successful
    */
    virtual bool
    storeLedger(std::shared_ptr<Ledger const> const& srcLedger) = 0;

    /** Remove expired entries from the positive and negative caches. */
    virtual void
    sweep() = 0;

    /** Gather statistics pertaining to read and write activities.
     *
     * @param obj Json object reference into which to place counters.
     */
    std::uint64_t
    getStoreCount() const
    {
        return storeCount_;
    }

    std::uint32_t
    getFetchTotalCount() const
    {
        return fetchTotalCount_;
    }

    std::uint32_t
    getFetchHitCount() const
    {
        return fetchHitCount_;
    }

    std::uint64_t
    getStoreSize() const
    {
        return storeSz_;
    }

    std::uint32_t
    getFetchSize() const
    {
        return fetchSz_;
    }

    void
    getCountsJson(Json::Value& obj);

    /** Returns the number of file descriptors the database expects to need */
    int
    fdRequired() const
    {
        return fdRequired_;
    }

    virtual void
    stop();

    bool
    isStopping() const;

    /** @return The maximum number of ledgers stored in a shard
     */
    [[nodiscard]] std::uint32_t
    ledgersPerShard() const noexcept
    {
        return ledgersPerShard_;
    }

    /** @return The earliest ledger sequence allowed
     */
    [[nodiscard]] std::uint32_t
    earliestLedgerSeq() const noexcept
    {
        return earliestLedgerSeq_;
    }

    /** @return The earliest shard index
     */
    [[nodiscard]] std::uint32_t
    earliestShardIndex() const noexcept
    {
        return earliestShardIndex_;
    }

    /** Calculates the first ledger sequence for a given shard index

        @param shardIndex The shard index considered
        @return The first ledger sequence pertaining to the shard index
    */
    [[nodiscard]] std::uint32_t
    firstLedgerSeq(std::uint32_t shardIndex) const noexcept
    {
        assert(shardIndex >= earliestShardIndex_);
        if (shardIndex <= earliestShardIndex_)
            return earliestLedgerSeq_;
        return 1 + (shardIndex * ledgersPerShard_);
    }

    /** Calculates the last ledger sequence for a given shard index

        @param shardIndex The shard index considered
        @return The last ledger sequence pertaining to the shard index
    */
    [[nodiscard]] std::uint32_t
    lastLedgerSeq(std::uint32_t shardIndex) const noexcept
    {
        assert(shardIndex >= earliestShardIndex_);
        return (shardIndex + 1) * ledgersPerShard_;
    }

    /** Calculates the shard index for a given ledger sequence

        @param ledgerSeq ledger sequence
        @return The shard index of the ledger sequence
    */
    [[nodiscard]] std::uint32_t
    seqToShardIndex(std::uint32_t ledgerSeq) const noexcept
    {
        assert(ledgerSeq >= earliestLedgerSeq_);
        return (ledgerSeq - 1) / ledgersPerShard_;
    }

    /** Calculates the maximum ledgers for a given shard index

        @param shardIndex The shard index considered
        @return The maximum ledgers pertaining to the shard index

        @note The earliest shard may store less if the earliest ledger
        sequence truncates its beginning
    */
    [[nodiscard]] std::uint32_t
    maxLedgers(std::uint32_t shardIndex) const noexcept;

protected:
    beast::Journal const j_;
    Scheduler& scheduler_;
    int fdRequired_{0};

    std::atomic<std::uint32_t> fetchHitCount_{0};
    std::atomic<std::uint32_t> fetchSz_{0};

    // The default is DEFAULT_LEDGERS_PER_SHARD (16384) to match the XRP ledger
    // network. Can be set through the configuration file using the
    // 'ledgers_per_shard' field under the 'node_db' and 'shard_db' stanzas.
    // If specified, the value must be a multiple of 256 and equally assigned
    // in both stanzas. Only unit tests or alternate networks should change
    // this value.
    std::uint32_t const ledgersPerShard_;

    // The default is XRP_LEDGER_EARLIEST_SEQ (32570) to match the XRP ledger
    // network's earliest allowed ledger sequence. Can be set through the
    // configuration file using the 'earliest_seq' field under the 'node_db'
    // and 'shard_db' stanzas. If specified, the value must be greater than zero
    // and equally assigned in both stanzas. Only unit tests or alternate
    // networks should change this value.
    std::uint32_t const earliestLedgerSeq_;

    // The earliest shard index
    std::uint32_t const earliestShardIndex_;

    // The maximum number of requests a thread extracts from the queue in an
    // attempt to minimize the overhead of mutex acquisition. This is an
    // advanced tunable, via the config file. The default value is 4.
    int const requestBundle_;

    void
    storeStats(std::uint64_t count, std::uint64_t sz)
    {
        assert(count <= sz);
        storeCount_ += count;
        storeSz_ += sz;
    }

    // Called by the public import function
    void
    importInternal(Backend& dstBackend, Database& srcDB);

    // Called by the public storeLedger function
    bool
    storeLedger(Ledger const& srcLedger, std::shared_ptr<Backend> dstBackend);

    void
    updateFetchMetrics(uint64_t fetches, uint64_t hits, uint64_t duration)
    {
        fetchTotalCount_ += fetches;
        fetchHitCount_ += hits;
        fetchDurationUs_ += duration;
    }

private:
    std::atomic<std::uint64_t> storeCount_{0};
    std::atomic<std::uint64_t> storeSz_{0};
    std::atomic<std::uint64_t> fetchTotalCount_{0};
    std::atomic<std::uint64_t> fetchDurationUs_{0};
    std::atomic<std::uint64_t> storeDurationUs_{0};

    mutable std::mutex readLock_;
    std::condition_variable readCondVar_;

    // reads to do
    std::map<
        uint256,
        std::vector<std::pair<
            std::uint32_t,
            std::function<void(std::shared_ptr<NodeObject> const&)>>>>
        read_;

    std::atomic<bool> readStopping_ = false;
    std::atomic<int> readThreads_ = 0;
    std::atomic<int> runningThreads_ = 0;

    virtual std::shared_ptr<NodeObject>
    fetchNodeObject(
        uint256 const& hash,
        std::uint32_t ledgerSeq,
        FetchReport& fetchReport,
        bool duplicate) = 0;

    /** Visit every object in the database
        This is usually called during import.

        @note This routine will not be called concurrently with itself
                or other methods.
        @see import
    */
    virtual void
    for_each(std::function<void(std::shared_ptr<NodeObject>)> f) = 0;

    /** Retrieve backend read and write stats.

        @note The Counters struct is specific to and only used
              by CassandraBackend.
    */
    virtual std::optional<Backend::Counters<std::uint64_t>>
    getCounters() const
    {
        return std::nullopt;
    }

    void
    threadEntry();
};

}  // namespace NodeStore
}  // namespace ripple

#endif
