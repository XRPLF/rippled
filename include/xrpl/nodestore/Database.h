#pragma once

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/TaggedCache.ipp>  // IWYU pragma: keep
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/WriteStats.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace xrpl {
class Section;
}  // namespace xrpl

namespace xrpl::node_store {

/**
 * Persistency layer for NodeObject
 *
 * A Node is a ledger object which is uniquely identified by a key, which is
 * the 256-bit hash of the body of the node. The payload is a variable length
 * block of serialized data.
 *
 * All ledger data is stored as node objects and as such, needs to be persisted
 * between launches. Furthermore, since the set of node objects will in
 * general be larger than the amount of available memory, purged node objects
 * which are later accessed must be retrieved from the node store.
 *
 * @see NodeObject
 */
class Database
{
public:
    Database() = delete;

    /**
     * Construct the node store.
     *
     * @param scheduler The scheduler to use for performing asynchronous tasks.
     * @param readThreads The number of asynchronous read threads to create.
     * @param config The configuration settings
     * @param journal Destination for logging output.
     */
    Database(Scheduler& scheduler, int readThreads, Section const& config, beast::Journal j);

    /**
     * Destroy the node store.
     * All pending operations are completed, pending writes flushed,
     * and files closed before this returns.
     */
    virtual ~Database();

    /**
     * Retrieve the name associated with this backend.
     * This is used for diagnostics and may not reflect the actual path
     * or paths used by the underlying backend.
     */
    virtual std::string
    getName() const = 0;

    /**
     * Import objects from another database.
     */
    virtual void
    importDatabase(Database& source) = 0;

    /**
     * Retrieve the estimated number of pending write operations.
     * This is used for diagnostics.
     */
    virtual std::int32_t
    getWriteLoad() const = 0;

    /**
     * Get backend write-path statistics, if the backend measures them.
     *
     * @return The statistics, or std::nullopt when the backend does not
     *         measure its writes.
     */
    [[nodiscard]] virtual std::optional<WriteStats>
    getWriteStats() const = 0;

    /**
     * Store the object.
     *
     * The caller's Blob parameter is overwritten.
     *
     * @param type The type of object.
     * @param data The payload of the object. The caller's
     *             variable is overwritten.
     * @param hash The 256-bit hash of the payload data.
     * @param ledgerSeq The sequence of the ledger the object belongs to.
     *
     * @return `true` if the object was stored?
     */
    virtual void
    store(NodeObjectType type, Blob&& data, uint256 const& hash, std::uint32_t ledgerSeq) = 0;

    /**
     * Check if two ledgers are in the same database
     *
     * If these two sequence numbers map to the same database,
     * the result of a fetch with either sequence number would
     * be identical.
     *
     * @param s1 The first sequence number
     * @param s2 The second sequence number
     *
     * @return 'true' if both ledgers would be in the same DB
     */
    virtual bool
    isSameDB(std::uint32_t s1, std::uint32_t s2) = 0;

    virtual void
    sync() = 0;

    /**
     * Fetch a node object.
     * If the object is known to be not in the database, isn't found in the
     * database during the fetch, or failed to load correctly during the fetch,
     * `nullptr` is returned.
     *
     * @note This can be called concurrently.
     * @param hash The key of the object to retrieve.
     * @param ledgerSeq The sequence of the ledger where the object is stored.
     * @param fetchType the type of fetch, synchronous or asynchronous.
     * @return The object, or nullptr if it couldn't be retrieved.
     */
    std::shared_ptr<NodeObject>
    fetchNodeObject(
        uint256 const& hash,
        std::uint32_t ledgerSeq = 0,
        FetchType fetchType = FetchType::Synchronous,
        bool duplicate = false);

    /**
     * Fetch an object without waiting.
     * If I/O is required to determine whether or not the object is present,
     * `false` is returned. Otherwise, `true` is returned and `object` is set
     * to refer to the object, or `nullptr` if the object is not present.
     * If I/O is required, the I/O is scheduled and `true` is returned
     *
     * @note This can be called concurrently.
     * @param hash The key of the object to retrieve
     * @param ledgerSeq The sequence of the ledger where the
     *         object is stored.
     * @param callback Callback function when read completes
     */
    virtual void
    asyncFetch(
        uint256 const& hash,
        std::uint32_t ledgerSeq,
        std::function<void(std::shared_ptr<NodeObject> const&)>&& callback);

    /**
     * Remove expired entries from the positive and negative caches.
     */
    virtual void
    sweep() = 0;

    /**
     * Gather statistics pertaining to read and write activities.
     *
     * @param obj Json object reference into which to place counters.
     */
    std::uint64_t
    getStoreCount() const
    {
        return storeCount_;
    }

    /**
     * Total number of fetches attempted, whether or not they found anything.
     *
     * @return The running count for the lifetime of this process.
     */
    std::uint64_t
    getFetchTotalCount() const
    {
        return fetchTotalCount_;
    }

    /**
     * Number of fetches that found the object.
     *
     * Divide by getFetchTotalCount() to get the read hit rate.
     *
     * @return The running count for the lifetime of this process.
     */
    std::uint64_t
    getFetchHitCount() const
    {
        return fetchHitCount_;
    }

    std::uint64_t
    getStoreSize() const
    {
        return storeSz_;
    }

    /**
     * Total payload bytes returned by successful fetches.
     *
     * @return The running byte total for the lifetime of this process.
     */
    std::uint64_t
    getFetchSize() const
    {
        return fetchSz_;
    }

    /**
     * Cumulative microseconds spent inside store() calls.
     *
     * Pairs with getStoreCount() to derive mean write latency
     * (`duration / count`), mirroring how the read side pairs
     * getFetchDurationUs() with getFetchTotalCount(). The mean includes any
     * time the backend spent waiting for its own internal locks, so it is wall
     * time per store, not service time.
     *
     * This is the "an existing DB syncs slower than a fresh one" signal: the
     * read counters cannot show it, because back-fill is write-bound. Also
     * published as the `node_writes_duration_us` field of getCountsJson(), so
     * the RPC and the metric report the same number.
     *
     * @return Total microseconds accumulated across every completed store.
     *
     * @note Thread-safe: a single relaxed atomic load. Cheap enough for a
     * periodic observer (the telemetry reader ticks every ~10 s). Relaxed is
     * sufficient because the value is a monotonic statistic, not a
     * synchronization signal — a reader that observes a slightly stale total
     * simply reports a slightly stale mean.
     * @note Monotonic and never reset, so a dashboard must take a rate or a
     * delta of both this and getStoreCount() over the same window to see
     * current latency rather than the since-boot average.
     */
    [[nodiscard]] std::uint64_t
    getStoreDurationUs() const noexcept
    {
        return storeDurationUs_.load(std::memory_order_relaxed);
    }

    /**
     * Cumulative microseconds spent inside fetchNodeObject() calls.
     *
     * Pairs with getFetchTotalCount() to derive mean read latency. That mean
     * is what separates a cold store from a warm one: a warm store reads in
     * single-digit microseconds, a cold one in low hundreds. The same
     * total is already published as the `node_reads_duration_us` field of
     * getCountsJson(); this accessor exposes it directly so a caller need not
     * build a json::Value and parse a decimal string back to an integer.
     *
     * @return Total microseconds accumulated across every completed fetch.
     *
     * @note Same threading and monotonicity contract as
     * getStoreDurationUs().
     */
    [[nodiscard]] std::uint64_t
    getFetchDurationUs() const noexcept
    {
        return fetchDurationUs_.load(std::memory_order_relaxed);
    }

    void
    getCountsJson(json::Value& obj);

    /**
     * Returns the number of file descriptors the database expects to need
     */
    int
    fdRequired() const
    {
        return fdRequired_;
    }

    virtual void
    stop();

    bool
    isStopping() const;

    /**
     * @return The earliest ledger sequence allowed
     */
    [[nodiscard]] std::uint32_t
    earliestLedgerSeq() const noexcept
    {
        return earliestLedgerSeq_;
    }

protected:
    beast::Journal const j_;
    Scheduler& scheduler_;
    int fdRequired_{0};

    // The default is XRP_LEDGER_EARLIEST_SEQ (32570) to match the XRP ledger
    // network's earliest allowed ledger sequence. Can be set through the
    // configuration file using the 'earliest_seq' field under the 'node_db'
    // stanza. If specified, the value must be greater than zero.
    // Only unit tests or alternate
    // networks should change this value.
    std::uint32_t const earliestLedgerSeq_;

    // The maximum number of requests a thread extracts from the queue in an
    // attempt to minimize the overhead of mutex acquisition. This is an
    // advanced tunable, via the config file. The default value is 4.
    int const requestBundle_;

    void
    storeStats(std::uint64_t count, std::uint64_t sz)
    {
        XRPL_ASSERT(count <= sz, "xrpl::node_store::Database::storeStats : valid inputs");
        storeCount_ += count;
        storeSz_ += sz;
    }

    /**
     * Accumulate the time one completed store took.
     *
     * The write counterpart of the timing fetchNodeObject() already does for
     * reads. `store()` is pure virtual, so unlike the read path there is no
     * non-virtual wrapper in this class to time — each concrete database calls
     * this once per store it completes, and the single conversion to
     * microseconds lives here rather than being repeated per subclass. Each
     * concrete store path times only its backend call, so the total reflects
     * disk work and excludes cache bookkeeping.
     *
     * @param elapsed Wall time the store took, as measured by the caller.
     *
     * @note Call once per store operation, never inside a per-tree-node loop:
     * a ledger write walks thousands of SHAMap nodes and this must stay a
     * single atomic add on the whole write, matching the one-sample-per-fetch
     * cost on the read side.
     * @note Thread-safe: one relaxed atomic add, no lock. Relaxed ordering is
     * correct because the total is a statistic that is only ever read by a
     * periodic observer, never used to order other memory operations.
     * @note A negative duration cannot occur (steady_clock is monotonic), but
     * a caller passing one would be clamped to zero rather than wrapping the
     * unsigned total to a huge value.
     */
    void
    recordStoreDuration(std::chrono::steady_clock::duration elapsed) noexcept
    {
        auto const us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
        if (us > 0)
            storeDurationUs_.fetch_add(static_cast<std::uint64_t>(us), std::memory_order_relaxed);
    }

    // Called by the public import function
    void
    importInternal(Backend& dstBackend, Database& srcDB);

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

    /**
     * Fetches that found the object.
     *
     * 64-bit because a 32-bit counter wraps on a long-lived node, which
     * silently corrupts the read hit rate.
     */
    std::atomic<std::uint64_t> fetchHitCount_{0};

    /**
     * Payload bytes returned by successful fetches.
     *
     * 64-bit for the same reason: at production read rates 32 bits wraps
     * in under an hour.
     */
    std::atomic<std::uint64_t> fetchSz_{0};

    /**
     * Wall time spent in backend fetches, in microseconds.
     *
     * Written by fetchNodeObject(), which times the whole fetch including a
     * cache lookup that misses.
     */
    std::atomic<std::uint64_t> fetchDurationUs_{0};

    /**
     * Wall time spent in backend stores, in microseconds.
     *
     * Written by each concrete store path via recordStoreDuration(), which
     * times only the backend call.
     */
    std::atomic<std::uint64_t> storeDurationUs_{0};

    mutable std::mutex readLock_;
    std::condition_variable readCondVar_;

    // reads to do
    std::map<
        uint256,
        std::vector<
            std::pair<std::uint32_t, std::function<void(std::shared_ptr<NodeObject> const&)>>>>
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

    /**
     * Visit every object in the database
     * This is usually called during import.
     *
     * @note This routine will not be called concurrently with itself
     *       or other methods.
     * @see import
     */
    virtual void
    forEach(std::function<void(std::shared_ptr<NodeObject>)> f) = 0;

    void
    threadEntry();
};

}  // namespace xrpl::node_store
