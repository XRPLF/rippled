#pragma once

#include <xrpl/basics/IntrusivePointer.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/SharedWeakCachePointer.ipp>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/hardened_hash.h>
#include <xrpl/beast/clock/abstract_clock.h>
#include <xrpl/beast/insight/Insight.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <mutex>
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

/** Map/cache combination.
    This class implements a cache and a map. The cache keeps objects alive
    in the map. The map allows multiple code paths that reference objects
    with the same tag to get the same actual object.

    So long as data is in the cache, it will stay in memory.
    If it stays in memory even after it is ejected from the cache,
    the map will track it.

    @note Callers must not modify data objects that are stored in the cache
          unless they hold their own lock over all cache operations.
*/
template <
    class Key,
    class T,
    bool IsKeyCache = false,
    class SharedWeakUnionPointerType = SharedWeakCachePointer<T>,
    class SharedPointerType = std::shared_ptr<T>,
    class Hash = HardenedHash<>,
    class KeyEqual = std::equal_to<Key>,
    class Mutex = std::recursive_mutex>
class TaggedCache
{
public:
    using mutex_type = Mutex;
    using key_type = Key;
    using mapped_type = T;
    using clock_type = beast::AbstractClock<std::chrono::steady_clock>;
    using shared_weak_combo_pointer_type = SharedWeakUnionPointerType;
    using shared_pointer_type = SharedPointerType;

public:
    TaggedCache(
        std::string const& name,
        int size,
        clock_type::duration expiration,
        clock_type& clock,
        beast::Journal journal,
        beast::insight::Collector::ptr const& collector = beast::insight::NullCollector::make());

public:
    /** Return the clock associated with the cache. */
    clock_type&
    clock();

    /** Returns the number of items in the container. */
    std::size_t
    size() const;

    int
    getCacheSize() const;

    int
    getTrackSize() const;

    float
    getHitRate();

    void
    clear();

    void
    reset();

    /** Refresh the last access time on a key if present.
        @return `true` If the key was found.
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

    /** Shared implementation of the canonicalize family.

        `policy` selects how a collision is resolved when `key` already exists:
        detail::ReplaceCached, detail::ReplaceClient or
        detail::ReplaceDynamically. For ReplaceDynamically `replaceCallback` is
        invoked with the existing strong pointer and returns whether to replace
        the cached value with `data`; for the tag policies it is unused.
    */
    template <class Policy, class Callback = std::nullptr_t>
    bool
    canonicalizeImpl(
        key_type const& key,
        CanonicalizeClientPointerType<Policy> data,
        Policy policy,
        Callback&& replaceCallback = nullptr);

public:
    /** Replace aliased objects with originals.

        Due to concurrency it is possible for two separate objects with
        the same content and referring to the same unique "thing" to exist.
        This routine eliminates the duplicate and performs a replacement
        on the callers shared pointer if needed.

        `replaceCallback` is a callable taking the existing strong pointer and
        returning whether to replace the cached value with `data` (true) or to
        keep the cached value and write it back into `data` (false). Because the
        write-back case mutates `data`, `data` must be writable.

        @param key The key corresponding to the object
        @param data A shared pointer to the data corresponding to the object.
        @param replaceCallback A callable (existing strong pointer -> bool).

        @return `true` if an existing live entry was found and used; `false` if a new entry was
                inserted or an expired tracked entry was re-cached.
    **/
    template <class Callback>
    bool
    canonicalize(key_type const& key, SharedPointerType& data, Callback&& replaceCallback);

    /** Insert/update the canonical entry for `key`, always replacing the
        cached value with `data`.

        If an entry already exists for `key`, the cached value is unconditionally
        replaced with `data`; otherwise `data` is inserted. `data` is never
        written back, so it may be const.

        @param key The key corresponding to the object.
        @param data A shared pointer to the data corresponding to the object.

        @return `true` if an existing live entry was found and used; `false` if a new entry was
                inserted or an expired tracked entry was re-cached.
    **/
    bool
    canonicalizeReplaceCache(key_type const& key, SharedPointerType const& data);

    /** Insert the canonical entry for `key`, keeping any existing cached value.

        If an entry already exists for `key`, the cached value is kept and
        written back into `data` so the caller ends up with the canonical
        object; otherwise `data` is inserted. Because `data` may be overwritten
        it must be writable.

        @param key The key corresponding to the object.
        @param data A shared pointer to the data corresponding to the object;
                    updated to the canonical value when one already exists.

        @return `true` if an existing live entry was found and used; `false` if a new entry was
                inserted or an expired tracked entry was re-cached.
    **/
    bool
    canonicalizeReplaceClient(key_type const& key, SharedPointerType& data);

    SharedPointerType
    fetch(key_type const& key);

    /** Insert the element into the container.
        If the key already exists, nothing happens.
        @return `true` If the element was inserted
    */
    template <class ReturnType = bool>
    auto
    insert(key_type const& key, T const& value) -> std::enable_if_t<!IsKeyCache, ReturnType>;

    template <class ReturnType = bool>
    auto
    insert(key_type const& key) -> std::enable_if_t<IsKeyCache, ReturnType>;

    // VFALCO NOTE It looks like this returns a copy of the data in
    //             the output parameter 'data'. This could be expensive.
    //             Perhaps it should work like standard containers, which
    //             simply return an iterator.
    //
    bool
    retrieve(key_type const& key, T& data);

    mutex_type&
    peekMutex();

    std::vector<key_type>
    getKeys() const;

    // CachedSLEs functions.
    /** Returns the fraction of cache hits. */
    double
    rate() const;

    /** Fetch an item from the cache.
        If the digest was not found, Handler
        will be called with this signature:
            SLE::const_pointer(void)
    */
    template <class Handler>
    SharedPointerType
    fetch(key_type const& digest, Handler const& h);
    // End CachedSLEs functions.

private:
    SharedPointerType
    initialFetch(key_type const& key, std::scoped_lock<mutex_type> const& l);

    void
    collectMetrics();

private:
    struct Stats
    {
        template <class Handler>
        Stats(
            std::string const& prefix,
            Handler const& handler,
            beast::insight::Collector::ptr const& collector)
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
        clock_type::time_point lastAccess;

        explicit KeyOnlyEntry(clock_type::time_point const& lastAccess) : lastAccess(lastAccess)
        {
        }

        void
        touch(clock_type::time_point const& now)
        {
            lastAccess = now;
        }
    };

    class ValueEntry
    {
    public:
        shared_weak_combo_pointer_type ptr;
        clock_type::time_point lastAccess;

        ValueEntry(clock_type::time_point const& lastAccess, shared_pointer_type const& ptr)
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
        touch(clock_type::time_point const& now)
        {
            lastAccess = now;
        }
    };

    using Entry = std::conditional_t<IsKeyCache, KeyOnlyEntry, ValueEntry>;

    using KeyOnlyCacheType = hardened_partitioned_hash_map<key_type, KeyOnlyEntry, Hash, KeyEqual>;

    using KeyValueCacheType = hardened_partitioned_hash_map<key_type, ValueEntry, Hash, KeyEqual>;

    using cache_type = hardened_partitioned_hash_map<key_type, Entry, Hash, KeyEqual>;

    [[nodiscard]] std::thread
    sweepHelper(
        clock_type::time_point const& whenExpire,
        [[maybe_unused]] clock_type::time_point const& now,
        typename KeyValueCacheType::map_type& partition,
        SweptPointersVector& stuffToSweep,
        std::atomic<int>& allRemovals,
        std::scoped_lock<std::recursive_mutex> const&);

    [[nodiscard]] std::thread
    sweepHelper(
        clock_type::time_point const& whenExpire,
        clock_type::time_point const& now,
        typename KeyOnlyCacheType::map_type& partition,
        SweptPointersVector&,
        std::atomic<int>& allRemovals,
        std::scoped_lock<std::recursive_mutex> const&);

    beast::Journal journal_;
    clock_type& clock_;
    Stats stats_;

    mutex_type mutable mutex_;

    // Used for logging
    std::string name_;

    // Desired number of cache entries (0 = ignore)
    int const targetSize_;

    // Desired maximum cache age
    clock_type::duration const targetAge_;

    // Number of items cached
    int cacheCount_{0};
    cache_type cache_;  // Hold strong reference to recent objects
    std::uint64_t hits_{0};
    std::uint64_t misses_{0};
};

}  // namespace xrpl
