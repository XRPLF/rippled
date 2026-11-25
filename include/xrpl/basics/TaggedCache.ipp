#pragma once

#include <xrpl/basics/IntrusivePointer.ipp>
#include <xrpl/basics/Log.h>  // IWYU pragma: keep
#include <xrpl/basics/TaggedCache.h>
#include <xrpl/basics/partitioned_unordered_map.h>  // Provides extract().
#include <xrpl/basics/scope.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <algorithm>

namespace xrpl {

namespace detail {

// Replace-policy tags selecting how TaggedCache::canonicalizeImpl resolves a
// collision when the key already exists:
//   - ReplaceCached: always replace the cached value with `data`. `data` is
//     never written back and may be const.
//   - ReplaceClient: keep the cached value and write it back into `data` (the
//     client's pointer), which must therefore be writable.
//   - ReplaceDynamically: call the supplied callback to decide per call; `data`
//     is written back when the cached value is kept, so it must be writable.
struct ReplaceCached
{
};

struct ReplaceClient
{
};

struct ReplaceDynamically
{
};

}  // namespace detail

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
inline TaggedCache<
    Key,
    T,
    IsKeyCache,
    SharedWeakUnionPointer,
    SharedPointerType,
    Hash,
    KeyEqual,
    Mutex>::
    TaggedCache(
        std::string const& name,
        int size,
        ClockType::duration expiration,
        ClockType& clock,
        beast::Journal journal,
        beast::insight::Collector::Ptr const& collector)
    : journal_(journal)
    , clock_(clock)
    , name_(name)
    , targetSize_(size)
    , targetAge_(expiration)
    , partitions_(std::max<std::size_t>(1, std::thread::hardware_concurrency()))
    , stats_(name, [this] { collectMetrics(); }, collector)
{
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
inline auto
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    clock() -> ClockType&
{
    return clock_;
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
inline std::size_t
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    size() const
{
    // One partition at a time: no thread ever holds two partition locks.
    std::size_t total = 0;
    for (auto const& partition : partitions_)
        total += partition.guarded.lock()->map.size();
    return total;
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
inline int
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    getCacheSize() const
{
    return getCounters().cacheCount;
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
inline int
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    getTrackSize() const
{
    return size();
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
inline float
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    getHitRate()
{
    auto const counters = getCounters();
    auto const total = static_cast<float>(counters.hits + counters.misses);
    return counters.hits * (100.0f / std::max(1.0f, total));
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
inline void
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    clear()
{
    for (auto& partition : partitions_)
    {
        auto locked = partition.guarded.lock();
        locked->map.clear();
        locked->counters.cacheCount = 0;
    }
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
inline void
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    reset()
{
    for (auto& partition : partitions_)
    {
        auto locked = partition.guarded.lock();
        locked->map.clear();
        locked->counters = {};
    }
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
template <class KeyComparable>
inline bool
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    touchIfExists(KeyComparable const& key)
{
    // Convert once: the same key picks the partition and is looked up in it.
    key_type const& k = key;
    auto partition = getLockedPartition(k);
    auto const iter(partition->map.find(k));
    if (iter == partition->map.end())
    {
        ++partition->counters.misses;
        return false;
    }
    iter->second.touch(clock_.now());
    ++partition->counters.hits;
    return true;
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
inline void
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    sweep()
{
    // Keep references to all the stuff we sweep
    // For performance, each worker thread should exit before the swept data
    // is destroyed. No partition lock is held when it is.
    std::vector<SweptPointersVector> allStuffToSweep(partitions_.size());

    ClockType::time_point const now(clock_.now());
    ClockType::time_point whenExpire;

    auto const start = std::chrono::steady_clock::now();
    {
        // Read once. Partitions are locked one at a time, so this is a recent
        // total and not an exact one, which is enough to pick the cut-off.
        std::size_t const trackSize = size();

        if (targetSize_ == 0 || (static_cast<int>(trackSize) <= targetSize_))
        {
            whenExpire = now - targetAge_;
        }
        else
        {
            whenExpire = now - (targetAge_ * targetSize_ / trackSize);

            ClockType::duration const minimumAge(std::chrono::seconds(1));
            if (whenExpire > (now - minimumAge))
                whenExpire = now - minimumAge;

            JLOG(journal_.trace())
                << name_ << " is growing fast " << trackSize << " of " << targetSize_
                << " aging at " << (now - whenExpire).count() << " of " << targetAge_.count();
        }

        std::vector<std::thread> workers;
        workers.reserve(partitions_.size());

        for (std::size_t p = 0; p < partitions_.size(); ++p)
        {
            workers.push_back(sweepHelper(whenExpire, now, partitions_[p], allStuffToSweep[p]));
        }
        for (std::thread& worker : workers)
            worker.join();
    }
    // At this point allStuffToSweep will go out of scope outside any lock
    // and decrement the reference count on each strong pointer.
    JLOG(journal_.debug()) << name_ << " TaggedCache sweep lock duration "
                           << std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - start)
                                  .count()
                           << "ms";
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
inline bool
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    del(key_type const& key, bool valid)
{
    // Remove from cache, if !valid, remove from map too. Returns true if removed from cache
    auto partition = getLockedPartition(key);

    auto const cit = partition->map.find(key);

    if (cit == partition->map.end())
        return false;

    Entry& entry = cit->second;

    bool ret = false;

    if (entry.isCached())
    {
        --partition->counters.cacheCount;
        entry.ptr.convertToWeak();
        ret = true;
    }

    if (!valid || entry.isExpired())
        partition->map.erase(cit);

    return ret;
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
template <class Policy, class Callback>
inline bool
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    canonicalizeImpl(
        key_type const& key,
        CanonicalizeClientPointerType<Policy> data,
        Policy policy,
        Callback&& replaceCallback)
{
    auto partition = getLockedPartition(key);
    return canonicalizeImpl(partition, key, data, policy, std::forward<Callback>(replaceCallback));
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
template <class Policy, class Callback>
inline bool
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    canonicalizeImpl(
        LockedPartition& partition,
        key_type const& key,
        CanonicalizeClientPointerType<Policy> data,
        [[maybe_unused]] Policy policy,
        [[maybe_unused]] Callback&& replaceCallback)
{
    // Return canonical value, store if needed, refresh in cache
    // Return values: true=we had the data already

    // `Policy` is one of:
    //   - detail::ReplaceCached: always replace the cached value with `data`;
    //     `data` is never written back and may be const.
    //   - detail::ReplaceClient: keep the cached value and write it back into
    //     `data` (the client's pointer), which must therefore be writable.
    //   - detail::ReplaceDynamically: call `replaceCallback` to decide at run
    //     time; `data` must be writable.
    // For the latter two the write-back below requires a mutable `data`, so
    // passing a const argument is a compile error.
    constexpr bool replaceCached = std::is_same_v<Policy, detail::ReplaceCached>;

    auto const cit = partition->map.find(key);

    if (cit == partition->map.end())
    {
        partition->map.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(key),
            std::forward_as_tuple(clock_.now(), data));
        ++partition->counters.cacheCount;
        return false;
    }

    Entry& entry = cit->second;
    entry.touch(clock_.now());

    auto const shouldReplaceCached = [&] {
        if constexpr (replaceCached)
        {
            return true;
        }
        else if constexpr (std::is_same_v<Policy, detail::ReplaceClient>)
        {
            return false;
        }
        else
        {
            return replaceCallback(entry.ptr.getStrong());
        }
    };

    if (entry.isCached())
    {
        if (shouldReplaceCached())
        {
            entry.ptr = data;
        }
        else if constexpr (!replaceCached)
        {
            data = entry.ptr.getStrong();
        }

        return true;
    }

    auto const cachedData = entry.lock();

    if (cachedData)
    {
        if (shouldReplaceCached())
        {
            entry.ptr = data;
        }
        else if constexpr (!replaceCached)
        {
            entry.ptr.convertToStrong();
            data = cachedData;
        }

        ++partition->counters.cacheCount;
        return true;
    }

    entry.ptr = data;
    ++partition->counters.cacheCount;

    return false;
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
template <class Callback>
inline bool
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    canonicalize(key_type const& key, SharedPointerType& data, Callback&& replaceCallback)
{
    return canonicalizeImpl(
        key, data, detail::ReplaceDynamically{}, std::forward<Callback>(replaceCallback));
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
inline bool
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    canonicalizeReplaceCache(key_type const& key, SharedPointerType const& data)
{
    return canonicalizeImpl(key, data, detail::ReplaceCached{});
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
inline bool
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    canonicalizeReplaceClient(key_type const& key, SharedPointerType& data)
{
    return canonicalizeImpl(key, data, detail::ReplaceClient{});
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
inline SharedPointerType
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    fetch(key_type const& key)
{
    auto partition = getLockedPartition(key);
    auto ret = initialFetch(key, partition);
    if (!ret)
        ++partition->counters.misses;
    return ret;
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
template <class ReturnType>
inline auto
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    insert(key_type const& key, T const& value) -> ReturnType
    requires(!IsKeyCache)
{
    static_assert(
        std::is_same_v<std::shared_ptr<T>, SharedPointerType> ||
        std::is_same_v<intr_ptr::SharedPtr<T>, SharedPointerType>);

    if constexpr (std::is_same_v<std::shared_ptr<T>, SharedPointerType>)
    {
        auto p = std::make_shared<T>(std::cref(value));
        return canonicalizeReplaceClient(key, p);
    }
    if constexpr (std::is_same_v<intr_ptr::SharedPtr<T>, SharedPointerType>)
    {
        auto p = intr_ptr::makeShared<T>(std::cref(value));
        return canonicalizeReplaceClient(key, p);
    }
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
template <class ReturnType>
inline auto
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    insert(key_type const& key) -> ReturnType
    requires IsKeyCache
{
    auto partition = getLockedPartition(key);
    ClockType::time_point const now(clock_.now());
    auto [it, inserted] = partition->map.emplace(
        std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple(now));
    if (inserted)
    {
        ++partition->counters.cacheCount;
    }
    else
    {
        it->second.lastAccess = now;
    }
    return inserted;
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
inline bool
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    retrieve(key_type const& key, T& data)
{
    // retrieve the value of the stored data
    auto const entry = fetch(key);

    if (!entry)
        return false;

    data = *entry;
    return true;
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
inline auto
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    getKeys() const -> std::vector<key_type>
{
    std::vector<key_type> v;

    // Keep track of how many iterations are needed. Exit the loop if the number of retries gets
    // absurd. (Note that if this somehow ever happens, one more allocation will be done under
    // lock, which is undesirable, but really should be almost impossible.)
    std::size_t allocationIterations = 0;

    // Grows `v` to hold at least `size` keys. Never called with a lock held. It is defined here,
    // where no partition is in scope, so it cannot touch one.
    auto const reserve = [&](std::size_t size) {
        if (allocationIterations > 0)
        {
            JLOG(journal_.info()) << "getKeys(): Cache grew beyond allocated capacity after "
                                  << allocationIterations << " prior attempt(s). Have "
                                  << v.capacity() << ", need " << size << ". Retrying allocation";
        }
        // Allocate the current size plus a little extra, in case the cache grows while
        // allocating. Each time another allocation is needed, the extra also gets bigger until
        // it ultimately doubles the size + 1.
        constexpr std::size_t baseShift = 5;
        auto const bufferOffset = std::min(allocationIterations, std::size_t{baseShift});
        auto const bufferShift = baseShift - bufferOffset;
        size += (size >> bufferShift) + 1;
        v.reserve(size);
        ++allocationIterations;
    };

    // As before the partitions had their own locks, an empty cache allocates nothing.
    if (auto const total = size(); v.capacity() < total)
        reserve(total);

    for (auto const& partition : partitions_)
    {
        auto locked = partition.guarded.template lock<std::unique_lock>();
        std::unique_lock<MutexType>& lock = locked;
        // `needed` counts the keys already copied plus this partition's.
        for (auto needed = v.size() + locked->map.size();
             v.capacity() < needed && allocationIterations < 20;
             needed = v.size() + locked->map.size())
        {
            ScopeUnlock const unlock(lock);
            // Reserve for the whole cache, not only up to this partition, so
            // that the partitions still to come do not each force another
            // reallocation and copy. No lock is held here, so size() is safe.
            reserve(std::max(needed, size()));
        }
        if (v.capacity() < v.size() + locked->map.size())
        {
            // LCOV_EXCL_START
            UNREACHABLE("xrpl::TaggedCache::getKeys(): failed to allocate sufficient capacity");
            v.reserve(v.size() + locked->map.size());
            // LCOV_EXCL_STOP
        }
        XRPL_ASSERT(lock.owns_lock(), "xrpl::TaggedCache::getKeys(): owns lock");
        XRPL_ASSERT(
            v.capacity() >= v.size() + locked->map.size(),
            "xrpl::TaggedCache::getKeys(): sufficient capacity");
        for (auto const& _ : locked->map)
            v.push_back(_.first);
    }

    return v;
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
inline double
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    rate() const
{
    auto const counters = getCounters();
    auto const tot = counters.hits + counters.misses;
    if (tot == 0)
        return 0;
    return double(counters.hits) / tot;
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
template <class Handler>
inline SharedPointerType
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    fetch(key_type const& digest, Handler const& h)
{
    {
        auto partition = getLockedPartition(digest);
        if (auto ret = initialFetch(digest, partition))
            return ret;
    }

    auto const sle = h();
    if (!sle)
        return {};

    auto partition = getLockedPartition(digest);
    ++partition->counters.misses;
    auto const [it, inserted] = partition->map.emplace(digest, Entry(clock_.now(), std::move(sle)));
    if (!inserted)
        it->second.touch(clock_.now());
    return it->second.ptr.getStrong();
}
// End CachedSLEs functions.

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
template <class Callback>
inline void
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    fetchAndModify(key_type const& key, Callback&& callback)
{
    static_assert(
        !IsKeyCache, "fetchAndModify is only supported for value caches, not key-only caches");
    static_assert(
        std::is_same_v<std::shared_ptr<T>, SharedPointerType> ||
            std::is_same_v<intr_ptr::SharedPtr<T>, SharedPointerType>,
        "fetchAndModify requires std::shared_ptr<T> or intr_ptr::SharedPtr<T>");

    auto partition = getLockedPartition(key);

    // Same pointer-type dispatch as insert(): the placeholder must have exactly
    // SharedPointerType so canonicalizeImpl can write the cached pointer back.
    SharedPointerType entry = [] {
        if constexpr (std::is_same_v<std::shared_ptr<T>, SharedPointerType>)
        {
            return std::make_shared<T>();
        }
        else
        {
            return intr_ptr::makeShared<T>();
        }
    }();
    canonicalizeImpl(partition, key, entry, detail::ReplaceClient{});

    ALWAYS(
        entry != nullptr, "xrpl::TaggedCache::fetchAndModify : entry present after canonicalize");

    callback(*entry);
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
inline SharedPointerType
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    initialFetch(key_type const& key, LockedPartition& partition)
{
    auto const cit = partition->map.find(key);
    if (cit == partition->map.end())
        return {};

    Entry& entry = cit->second;
    if (entry.isCached())
    {
        ++partition->counters.hits;
        entry.touch(clock_.now());
        return entry.ptr.getStrong();
    }
    entry.ptr = entry.lock();
    if (entry.isCached())
    {
        // independent of cache size, so not counted as a hit
        ++partition->counters.cacheCount;
        entry.touch(clock_.now());
        return entry.ptr.getStrong();
    }

    partition->map.erase(cit);
    return {};
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
inline void
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    collectMetrics()
{
    // One pass over the partitions serves both gauges.
    auto const counters = getCounters();
    stats_.size.set(counters.cacheCount);

    {
        beast::insight::Gauge::value_type hitRate(0);
        {
            auto const total(counters.hits + counters.misses);
            if (total != 0)
                hitRate = (counters.hits * 100) / total;
        }
        stats_.hitRate.set(hitRate);
    }
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
inline auto
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    getLockedPartition(key_type const& key) -> LockedPartition
{
    return partitions_[extract(key) % partitions_.size()].guarded.lock();
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
inline auto
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    getCounters() const -> Counters
{
    Counters total;
    for (auto const& partition : partitions_)
    {
        auto const locked = partition.guarded.lock();
        total.cacheCount += locked->counters.cacheCount;
        total.hits += locked->counters.hits;
        total.misses += locked->counters.misses;
    }
    return total;
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
inline std::thread
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    sweepHelper(
        ClockType::time_point const& whenExpire,
        [[maybe_unused]] ClockType::time_point const& now,
        Partition& guardedPartition,
        SweptPointersVector& stuffToSweep)
    requires(!IsKeyCache)
{
    return std::thread([&, this] {
        // The worker holds its partition for the whole scan and settles the
        // count before unlocking, so count and map never disagree unlocked.
        auto locked = guardedPartition.guarded.lock();
        auto& partition = locked->map;

        int cacheRemovals = 0;
        int mapRemovals = 0;

        // Keep references to all the stuff we sweep
        // so that we can destroy them outside the lock.
        stuffToSweep.reserve(partition.size());
        {
            auto cit = partition.begin();
            while (cit != partition.end())
            {
                if (cit->second.isWeak())
                {
                    // weak
                    if (cit->second.isExpired())
                    {
                        stuffToSweep.emplace_back(std::move(cit->second.ptr));
                        ++mapRemovals;
                        cit = partition.erase(cit);
                    }
                    else
                    {
                        ++cit;
                    }
                }
                else if (cit->second.lastAccess <= whenExpire)
                {
                    // strong, expired
                    ++cacheRemovals;
                    if (cit->second.ptr.useCount() == 1)
                    {
                        stuffToSweep.emplace_back(std::move(cit->second.ptr));
                        ++mapRemovals;
                        cit = partition.erase(cit);
                    }
                    else
                    {
                        // remains weakly cached
                        cit->second.ptr.convertToWeak();
                        ++cit;
                    }
                }
                else
                {
                    // strong, not expired
                    ++cit;
                }
            }
        }

        if (mapRemovals || cacheRemovals)
        {
            JLOG(journal_.debug())
                << "TaggedCache partition sweep " << name_ << ": cache = " << partition.size()
                << "-" << cacheRemovals << ", map-=" << mapRemovals;
        }

        locked->counters.cacheCount -= cacheRemovals;
    });
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class SharedWeakUnionPointer,
    class SharedPointerType,
    class Hash,
    class KeyEqual,
    class Mutex>
inline std::thread
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    sweepHelper(
        ClockType::time_point const& whenExpire,
        ClockType::time_point const& now,
        Partition& guardedPartition,
        SweptPointersVector&)
    requires IsKeyCache
{
    return std::thread([&, this] {
        auto locked = guardedPartition.guarded.lock();
        auto& partition = locked->map;

        int cacheRemovals = 0;
        int mapRemovals = 0;

        // Keep references to all the stuff we sweep
        // so that we can destroy them outside the lock.
        {
            auto cit = partition.begin();
            while (cit != partition.end())
            {
                if (cit->second.lastAccess > now)
                {
                    cit->second.lastAccess = now;
                    ++cit;
                }
                else if (cit->second.lastAccess <= whenExpire)
                {
                    // A key-only entry is never held weakly, so an expired one leaves the
                    // cache and the map at the same time.
                    ++cacheRemovals;
                    ++mapRemovals;
                    cit = partition.erase(cit);
                }
                else
                {
                    ++cit;
                }
            }
        }

        if (mapRemovals > 0 || cacheRemovals > 0)
        {
            JLOG(journal_.debug())
                << "TaggedCache partition sweep " << name_ << ": cache = " << partition.size()
                << "-" << cacheRemovals << ", map-=" << mapRemovals;
        }

        locked->counters.cacheCount -= cacheRemovals;
    });
}

}  // namespace xrpl
