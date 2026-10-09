#pragma once

#include <xrpl/basics/SharedWeakCachePointer.h>
#include <xrpl/basics/SharedWeakCachePointer.ipp>  // IWYU pragma: keep
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/hardened_hash.h>
#include <xrpl/beast/clock/abstract_clock.h>
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/insight/Gauge.h>
#include <xrpl/beast/insight/Hook.h>
#include <xrpl/beast/insight/NullCollector.h>
#include <xrpl/beast/utility/Journal.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace xrpl {

namespace detail {

// Replace-policy tags selecting how TaggedCache::canonicalizeImpl resolves a
// collision when the key already exists (defined in TaggedCache.ipp):
//   - ReplaceCached: always replace the cached value with `data`. `data` is
//     never written back and may be const.
//   - ReplaceClient: keep the cached value and write it back into `data` (the
//     client's pointer), which must therefore be writable.
//   - ReplaceDynamically: call the supplied callback to decide per call; `data`
//     is written back when the cached value is kept, so it must be writable.
struct ReplaceCached;
struct ReplaceClient;
struct ReplaceDynamically;

}  // namespace detail

/**
 * Map/cache combination.
 * This class implements a cache and a map. The cache keeps objects alive
 * in the map. The map allows multiple code paths that reference objects
 * with the same tag to get the same actual object.
 *
 * So long as data is in the cache, it will stay in memory.
 * If it stays in memory even after it is ejected from the cache,
 * the map will track it.
 *
 * @note Callers must not modify data objects that are stored in the cache
 *       unless they hold their own lock over all cache operations.
 */
template <
    class Key,
    class T,
    bool IsKeyCache = false,
    class SharedWeakUnionPointerType = SharedWeakCachePointer<T>,
    class SharedPointer = std::shared_ptr<T>,
    class Hash = HardenedHash<>,
    class KeyEqual = std::equal_to<Key>,
    class Mutex = std::recursive_mutex>
class TaggedCache
{
public:
    using MutexType = Mutex;
    using key_type = Key;
    using mapped_type = T;
    using ClockType = beast::AbstractClock<std::chrono::steady_clock>;
    using SharedWeakComboPointerType = SharedWeakUnionPointerType;
    using SharedPointerType = SharedPointer;

public:
    /**
     * @param cacheHardCap When positive, a hard upper bound on the number of
     *     strongly-cached entries, enforced by demoting the approximately
     *     oldest entry whenever growth would exceed it. 0 disables the cap
     *     (the periodic sweep alone bounds the cache).
     * @param partitions Number of partitions the underlying map is split
     *     into; defaults to the hardware concurrency. Exposed so tests can
     *     pin a small, known partition count.
     */
    TaggedCache(
        std::string const& name,
        int size,
        ClockType::duration expiration,
        ClockType& clock,
        beast::Journal journal,
        beast::insight::Collector::Ptr const& collector = beast::insight::NullCollector::make(),
        int cacheHardCap = 0,
        std::optional<std::size_t> partitions = std::nullopt);

public:
    /**
     * Return the clock associated with the cache.
     */
    ClockType&
    clock();

    /**
     * Returns the number of items in the container.
     */
    std::size_t
    size() const;

    int
    getCacheSize() const;

    int
    getTrackSize() const;

    /**
     * Returns the number of strong-key slots evictForHardCap has examined
     * since construction.
     */
    std::uint64_t
    getEvictVisits() const;

    float
    getHitRate();

    void
    clear();

    void
    reset();

    /**
     * Refresh the last access time on a key if present.
     * @return `true` If the key was found.
     */
    template <class KeyComparable>
    bool
    touchIfExists(KeyComparable const& key);

    using SweptPointersVector = std::vector<SharedWeakUnionPointerType>;

    void
    sweep();

    bool
    del(key_type const& key, bool valid);

private:
    // Selects the `data` parameter type of canonicalizeImpl from the replace
    // policy: const for detail::ReplaceCached (never written back), otherwise
    // writable.
    template <typename Policy>
    using CanonicalizeClientPointerType = std::conditional_t<
        std::is_same_v<detail::ReplaceCached, Policy>,
        SharedPointerType const&,
        SharedPointerType&>;

    /**
     * Shared implementation of the canonicalize family.
     *
     * `policy` selects how a collision is resolved when `key` already exists:
     * detail::ReplaceCached, detail::ReplaceClient or
     * detail::ReplaceDynamically. For ReplaceDynamically `replaceCallback` is
     * invoked with the existing strong pointer and returns whether to replace
     * the cached value with `data`; for the tag policies it is unused.
     */
    template <class Policy, class Callback = std::nullptr_t>
    bool
    canonicalizeImpl(
        key_type const& key,
        CanonicalizeClientPointerType<Policy> data,
        Policy policy,
        Callback&& replaceCallback = nullptr);

public:
    /**
     * Replace aliased objects with originals.
     *
     * Due to concurrency it is possible for two separate objects with
     * the same content and referring to the same unique "thing" to exist.
     * This routine eliminates the duplicate and performs a replacement
     * on the callers shared pointer if needed.
     *
     * `replaceCallback` is a callable taking the existing strong pointer and
     * returning whether to replace the cached value with `data` (true) or to
     * keep the cached value and write it back into `data` (false). Because the
     * write-back case mutates `data`, `data` must be writable.
     *
     * @param key The key corresponding to the object
     * @param data A shared pointer to the data corresponding to the object.
     * @param replaceCallback A callable (existing strong pointer -> bool).
     *
     * @return `true` if an existing live entry was found and used; `false` if a new entry was
     *         inserted or an expired tracked entry was re-cached.
     */
    template <class Callback>
    bool
    canonicalize(key_type const& key, SharedPointerType& data, Callback&& replaceCallback);

    /**
     * Insert/update the canonical entry for `key`, always replacing the
     * cached value with `data`.
     *
     * If an entry already exists for `key`, the cached value is unconditionally
     * replaced with `data`; otherwise `data` is inserted. `data` is never
     * written back, so it may be const.
     *
     * @param key The key corresponding to the object.
     * @param data A shared pointer to the data corresponding to the object.
     *
     * @return `true` if an existing live entry was found and used; `false` if a new entry was
     *         inserted or an expired tracked entry was re-cached.
     */
    bool
    canonicalizeReplaceCache(key_type const& key, SharedPointerType const& data);

    /**
     * Insert the canonical entry for `key`, keeping any existing cached value.
     *
     * If an entry already exists for `key`, the cached value is kept and
     * written back into `data` so the caller ends up with the canonical
     * object; otherwise `data` is inserted. Because `data` may be overwritten
     * it must be writable.
     *
     * @param key The key corresponding to the object.
     * @param data A shared pointer to the data corresponding to the object;
     *             updated to the canonical value when one already exists.
     *
     * @return `true` if an existing live entry was found and used; `false` if a new entry was
     *         inserted or an expired tracked entry was re-cached.
     */
    bool
    canonicalizeReplaceClient(key_type const& key, SharedPointerType& data);

    SharedPointerType
    fetch(key_type const& key);

    /**
     * Insert the element into the container.
     * If the key already exists, nothing happens.
     * @return `true` If the element was inserted
     */
    template <class ReturnType = bool>
    auto
    insert(key_type const& key, T const& value) -> ReturnType
        requires(!IsKeyCache);

    template <class ReturnType = bool>
    auto
    insert(key_type const& key) -> ReturnType
        requires IsKeyCache;

    // VFALCO NOTE It looks like this returns a copy of the data in
    //             the output parameter 'data'. This could be expensive.
    //             Perhaps it should work like standard containers, which
    //             simply return an iterator.
    //
    bool
    retrieve(key_type const& key, T& data);

    MutexType&
    peekMutex();

    std::vector<key_type>
    getKeys() const;

    // CachedSLEs functions.
    /**
     * Returns the fraction of cache hits.
     */
    double
    rate() const;

    /**
     * Fetch an item from the cache.
     * If the digest was not found, Handler
     * will be called with this signature:
     *     SLE::const_pointer(void)
     */
    template <class Handler>
    SharedPointerType
    fetch(key_type const& digest, Handler const& h);
    // End CachedSLEs functions.

private:
    SharedPointerType
    initialFetch(key_type const& key, std::scoped_lock<MutexType> const& l);

    void
    collectMetrics();

private:
    struct Stats
    {
        template <class Handler>
        Stats(
            std::string const& prefix,
            Handler const& handler,
            beast::insight::Collector::Ptr const& collector)
            : hook(collector->makeHook(handler))
            , size(collector->makeGauge(prefix, "size"))
            , hitRate(collector->makeGauge(prefix, "hit_rate"))

        {
        }

        beast::insight::Hook hook;
        beast::insight::Gauge size;
        beast::insight::Gauge hitRate;

        std::size_t hits{0};
        std::size_t misses{0};
    };

    class KeyOnlyEntry
    {
    public:
        ClockType::time_point lastAccess;

        explicit KeyOnlyEntry(ClockType::time_point const& lastAccess) : lastAccess(lastAccess)
        {
        }

        void
        touch(ClockType::time_point const& now)
        {
            lastAccess = now;
        }
    };

    class ValueEntry
    {
    public:
        SharedWeakComboPointerType ptr;
        ClockType::time_point lastAccess;
        // Stamped by queueStrong each time the entry becomes strong; the
        // strong-key slot carrying the same seq is the entry's live slot.
        std::uint64_t strongSeq{0};

        ValueEntry(ClockType::time_point const& lastAccess, SharedPointerType const& ptr)
            : ptr(ptr), lastAccess(lastAccess)
        {
        }

        [[nodiscard]] bool
        isWeak() const
        {
            if (!ptr)
                return true;
            return ptr.isWeak();
        }
        [[nodiscard]] bool
        isCached() const
        {
            return ptr && ptr.isStrong();
        }
        [[nodiscard]] bool
        isExpired() const
        {
            return ptr.expired();
        }
        SharedPointerType
        lock()
        {
            return ptr.lock();
        }
        void
        touch(ClockType::time_point const& now)
        {
            lastAccess = now;
        }
    };

    using Entry = std::conditional_t<IsKeyCache, KeyOnlyEntry, ValueEntry>;

    using KeyOnlyCacheType = HardenedPartitionedHashMap<key_type, KeyOnlyEntry, Hash, KeyEqual>;

    using KeyValueCacheType = HardenedPartitionedHashMap<key_type, ValueEntry, Hash, KeyEqual>;

    using CacheType = HardenedPartitionedHashMap<key_type, Entry, Hash, KeyEqual>;

    // Counts an entry that just became strong and, under a hard cap, queues
    // its key for eviction and evicts down to the cap. Caller holds mutex_.
    void
    addStrong(CacheType::Iterator const& it);

    // Stamps the entry with a new strong seq and appends its key to
    // strongRing_, dropping stale slots once they outnumber the live ones.
    // Caller holds mutex_.
    void
    queueStrong(key_type const& key, Entry& entry);

    // CLOCK eviction over strongRing_: pops slots from the front, discards
    // stale ones, gives `keep` and entries used since they became strong a
    // second chance at the back, and demotes the first other strong entry,
    // repeating until the count is back under cacheHardCap_ or the demotion
    // budget is spent. Amortized O(1) per insert. No-op for key caches;
    // caller holds mutex_.
    void
    evictForHardCap(CacheType::Iterator const& keep);

    [[nodiscard]] std::thread
    sweepHelper(
        ClockType::time_point const& whenExpire,
        [[maybe_unused]] ClockType::time_point const& now,
        KeyValueCacheType::MapType& partition,
        SweptPointersVector& stuffToSweep,
        std::atomic<int>& allRemovals,
        std::scoped_lock<std::recursive_mutex> const&);

    [[nodiscard]] std::thread
    sweepHelper(
        ClockType::time_point const& whenExpire,
        ClockType::time_point const& now,
        KeyOnlyCacheType::MapType& partition,
        SweptPointersVector&,
        std::atomic<int>& allRemovals,
        std::scoped_lock<std::recursive_mutex> const&);

    beast::Journal journal_;
    ClockType& clock_;
    Stats stats_;

    MutexType mutable mutex_;

    // Used for logging
    std::string name_;

    // Desired number of cache entries (0 = ignore)
    int const targetSize_;

    // Desired maximum cache age
    ClockType::duration const targetAge_;

    // Hard upper bound on strongly-cached entries, enforced by
    // evictForHardCap whenever the strong count grows (fresh inserts and
    // weak-to-strong revivals). 0 disables it (sweep-only sizing).
    int const cacheHardCap_;

    // Total hard-cap evictions (under mutex_); the first marks saturation
    // onset for logging.
    std::uint64_t hardCapEvictions_{0};

    // Number of items cached
    int cacheCount_{0};

    // A key, the strong seq its entry had when the slot was queued, and the
    // time it was queued.
    struct StrongSlot
    {
        key_type key;
        std::uint64_t seq;
        ClockType::time_point since;
    };

    // Keys in the order their entries became strong, consumed front-first
    // by evictForHardCap. Filled only when cacheHardCap_ > 0. A slot is
    // stale once its entry is gone, weak, or strong again under a newer seq.
    std::deque<StrongSlot> strongRing_;

    // Source of strong seqs; advanced under mutex_.
    std::uint64_t strongSeqNext_{0};

    // Slots examined by evictForHardCap; read by getEvictVisits.
    std::uint64_t evictVisits_{0};

    CacheType cache_;  // Hold strong reference to recent objects
    std::uint64_t hits_{0};
    std::uint64_t misses_{0};
};

}  // namespace xrpl
