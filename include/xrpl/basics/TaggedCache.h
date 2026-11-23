#pragma once

#include <xrpl/basics/Mutex.hpp>
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

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
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
 *
 * @note The cache is split into partitions, each with its own mutex. A key
 *       always maps to the same partition, and an operation on one key locks
 *       only that partition. Operations on the whole cache (size(),
 *       getCacheSize(), getTrackSize(), getHitRate(), rate(), getKeys(),
 *       clear(), reset() and sweep()) lock one partition at a time, so they
 *       are not atomic with respect to operations on other partitions.
 *
 * @note No thread ever holds two partition locks of one cache. Callers may
 *       hold their own locks around a cache call. Code that runs while a
 *       partition is locked (the callbacks passed to canonicalize() and
 *       fetchAndModify(), and the destructors of cached objects) must not
 *       call back into the same cache or take a lock that another thread
 *       holds while calling into this cache: either can deadlock.
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
    TaggedCache(
        std::string const& name,
        int size,
        ClockType::duration expiration,
        ClockType& clock,
        beast::Journal journal,
        beast::insight::Collector::Ptr const& collector = beast::insight::NullCollector::make());

public:
    /**
     * Return the clock associated with the cache.
     */
    ClockType&
    clock();

    /**
     * Returns the number of items in the container.
     *
     * The partitions are counted one at a time, so under concurrent use the
     * result is a recent total and not a snapshot of one instant.
     */
    [[nodiscard]] std::size_t
    size() const;

    /**
     * Returns the number of items the cache keeps alive.
     *
     * A recent total under concurrent use, like size().
     */
    [[nodiscard]] int
    getCacheSize() const;

    /**
     * Returns the number of items tracked, cached or not.
     *
     * A recent total under concurrent use, like size().
     */
    [[nodiscard]] int
    getTrackSize() const;

    /**
     * Returns the percentage of lookups that were cache hits.
     *
     * A recent value under concurrent use, like size().
     */
    float
    getHitRate();

    /**
     * Remove every item.
     *
     * The partitions are emptied one at a time. An item inserted concurrently
     * into a partition that is already empty survives the call.
     */
    void
    clear();

    /**
     * Remove every item and zero the hit and miss counts.
     *
     * The partitions are reset one at a time, like clear().
     */
    void
    reset();

    /**
     * Refresh the last access time on a key if present.
     *
     * The lookup counts toward the hit rate that getHitRate() and rate()
     * report.
     *
     * @tparam KeyComparable The type of the key to look up. The cache must be
     *         able to compare it with `key_type`.
     * @param key The key to look up.
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
    struct PartitionData;

    // A locked partition: the lock and the data it protects, in one object.
    using LockedPartition = xrpl::Lock<PartitionData, std::scoped_lock, MutexType>;

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

    /**
     * canonicalizeImpl variant for callers that already hold the lock of the
     * partition `key` belongs to.
     *
     * `partition` is proof of ownership and the only map this overload
     * touches; this overload does not acquire the lock. Use it from other
     * methods (e.g. fetchAndModify) that need to compose canonicalize with
     * additional work under a single critical section, without relying on
     * recursive-mutex re-entry.
     */
    template <class Policy, class Callback = std::nullptr_t>
    bool
    canonicalizeImpl(
        LockedPartition& partition,
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

    /**
     * Insert a key into a key-only cache.
     *
     * A key already present has its last access time refreshed instead, and
     * is not counted again in getCacheSize().
     *
     * @tparam ReturnType The return type. It must be constructible from
     *         `bool`.
     * @param key The key to insert.
     * @return `true` if the key is new and is now tracked.
     */
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

    /**
     * Returns the keys of all tracked items.
     *
     * The partitions are copied one at a time. A key that is present for the
     * whole call is returned exactly once; a key inserted or removed during
     * the call may or may not be returned.
     */
    [[nodiscard]] std::vector<key_type>
    getKeys() const;

    // CachedSLEs functions.
    /**
     * Returns the fraction of cache hits.
     *
     * A recent value under concurrent use, like size().
     */
    [[nodiscard]] double
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

    /**
     * Fetch or create an entry and execute a callback while holding the lock.
     *
     * The entry for the given key is fetched from the cache or created if it
     * doesn't exist. The callback is then invoked with a reference to the
     * entry while the mutex of the key's partition is still held, allowing
     * safe modification of the cached object. No other operation on that key,
     * including sweep() and clear(), can run until the callback returns.
     *
     * @param key The key to fetch or create
     * @param callback Function to call with the entry under lock.
     *                 Signature: void(T&)
     */
    template <class Callback>
    void
    fetchAndModify(key_type const& key, Callback&& callback);

private:
    SharedPointerType
    initialFetch(key_type const& key, LockedPartition& partition);

    /**
     * Lock the partition `key` belongs to.
     *
     * Every single-key operation calls this once and then works only on the
     * returned partition.
     */
    [[nodiscard]] LockedPartition
    getLockedPartition(key_type const& key);

    struct Counters;

    // Sum of the per-partition counters, locking one partition at a time.
    [[nodiscard]] Counters
    getCounters() const;

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

    using MapType = HardenedHashMap<key_type, Entry, Hash, KeyEqual>;

    struct Counters
    {
        // Number of items cached
        int cacheCount{0};
        std::uint64_t hits{0};
        std::uint64_t misses{0};
    };

    // Everything one partition mutex protects.
    struct PartitionData
    {
        MapType map;  // Hold strong reference to recent objects
        Counters counters;
    };

    // Keeps the partitions on separate cache lines.
    static constexpr std::size_t kPartitionAlignment = 64;

    struct alignas(kPartitionAlignment) Partition
    {
        // Qualified, because `Mutex` names the template parameter here.
        xrpl::Mutex<PartitionData, MutexType> guarded;
    };

    [[nodiscard]] std::thread
    sweepHelper(
        ClockType::time_point const& whenExpire,
        [[maybe_unused]] ClockType::time_point const& now,
        Partition& guardedPartition,
        SweptPointersVector& stuffToSweep)
        requires(!IsKeyCache);

    /**
     * Sweep one partition of a key-only cache on its own thread.
     *
     * Every expired entry is erased and counted, so getCacheSize() tracks the
     * map size as it does for a value cache.
     *
     * @param whenExpire Entries whose last touch is not after this time are
     *        erased.
     * @param now The current time. An entry stamped later than this is
     *        clamped to it.
     * @param guardedPartition The partition to sweep. The worker locks it.
     * @return The worker thread, which the caller joins.
     */
    [[nodiscard]] std::thread
    sweepHelper(
        ClockType::time_point const& whenExpire,
        ClockType::time_point const& now,
        Partition& guardedPartition,
        SweptPointersVector&)
        requires IsKeyCache;

    beast::Journal journal_;
    ClockType& clock_;

    // Used for logging
    std::string name_;

    // Desired number of cache entries (0 = ignore)
    int const targetSize_;

    // Desired maximum cache age
    ClockType::duration const targetAge_;

    // Sized once, in the constructor, and never resized.
    std::vector<Partition> partitions_;

    // Declared last: its hook calls collectMetrics() from another thread, so
    // it must be destroyed before the partitions and constructed after them.
    Stats stats_;
};

}  // namespace xrpl
