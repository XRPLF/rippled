#pragma once

#include <xrpl/basics/IntrusivePointer.ipp>
#include <xrpl/basics/TaggedCache.h>

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
        clock_type::duration expiration,
        clock_type& clock,
        beast::Journal journal,
        beast::insight::Collector::ptr const& collector)
    : journal_(journal)
    , clock_(clock)
    , stats_(name, std::bind(&TaggedCache::collectMetrics, this), collector)
    , name_(name)
    , targetSize_(size)
    , targetAge_(expiration)
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
    clock() -> clock_type&
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
    std::scoped_lock const lock(mutex_);
    return cache_.size();
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
    std::scoped_lock const lock(mutex_);
    return cacheCount_;
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
    std::scoped_lock const lock(mutex_);
    return cache_.size();
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
    std::scoped_lock const lock(mutex_);
    auto const total = static_cast<float>(hits_ + misses_);
    return hits_ * (100.0f / std::max(1.0f, total));
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
    std::scoped_lock const lock(mutex_);
    cache_.clear();
    cacheCount_ = 0;
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
    std::scoped_lock const lock(mutex_);
    cache_.clear();
    cacheCount_ = 0;
    hits_ = 0;
    misses_ = 0;
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
    std::scoped_lock const lock(mutex_);
    auto const iter(cache_.find(key));
    if (iter == cache_.end())
    {
        ++stats_.misses;
        return false;
    }
    iter->second.touch(clock_.now());
    ++stats_.hits;
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
    // is destroyed but still within the main cache lock.
    std::vector<SweptPointersVector> allStuffToSweep(cache_.partitions());

    clock_type::time_point const now(clock_.now());
    clock_type::time_point whenExpire;

    auto const start = std::chrono::steady_clock::now();
    {
        std::scoped_lock const lock(mutex_);

        if (targetSize_ == 0 || (static_cast<int>(cache_.size()) <= targetSize_))
        {
            whenExpire = now - targetAge_;
        }
        else
        {
            whenExpire = now - (targetAge_ * targetSize_ / cache_.size());

            clock_type::duration const minimumAge(std::chrono::seconds(1));
            if (whenExpire > (now - minimumAge))
                whenExpire = now - minimumAge;

            JLOG(journal_.trace())
                << name_ << " is growing fast " << cache_.size() << " of " << targetSize_
                << " aging at " << (now - whenExpire).count() << " of " << targetAge_.count();
        }

        std::vector<std::thread> workers;
        workers.reserve(cache_.partitions());
        std::atomic<int> allRemovals = 0;

        for (std::size_t p = 0; p < cache_.partitions(); ++p)
        {
            workers.push_back(sweepHelper(
                whenExpire, now, cache_.map()[p], allStuffToSweep[p], allRemovals, lock));
        }
        for (std::thread& worker : workers)
            worker.join();

        cacheCount_ -= allRemovals;
    }
    // At this point allStuffToSweep will go out of scope outside the lock
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
    std::scoped_lock const lock(mutex_);

    auto cit = cache_.find(key);

    if (cit == cache_.end())
        return false;

    Entry& entry = cit->second;

    bool ret = false;

    if (entry.isCached())
    {
        --cacheCount_;
        entry.ptr.convertToWeak();
        ret = true;
    }

    if (!valid || entry.isExpired())
        cache_.erase(cit);

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

    std::scoped_lock const lock(mutex_);

    auto cit = cache_.find(key);

    if (cit == cache_.end())
    {
        cache_.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(key),
            std::forward_as_tuple(clock_.now(), data));
        ++cacheCount_;
        return false;
    }

    Entry& entry = cit->second;
    entry.touch(clock_.now());

    auto shouldReplaceCached = [&] {
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

    auto cachedData = entry.lock();

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

        ++cacheCount_;
        return true;
    }

    entry.ptr = data;
    ++cacheCount_;

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
    std::scoped_lock<mutex_type> const l(mutex_);
    auto ret = initialFetch(key, l);
    if (!ret)
        ++misses_;
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
    insert(key_type const& key, T const& value) -> std::enable_if_t<!IsKeyCache, ReturnType>
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
    insert(key_type const& key) -> std::enable_if_t<IsKeyCache, ReturnType>
{
    std::scoped_lock const lock(mutex_);
    clock_type::time_point const now(clock_.now());
    auto [it, inserted] = cache_.emplace(
        std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple(now));
    if (!inserted)
        it->second.lastAccess = now;
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
    auto entry = fetch(key);

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
    peekMutex() -> mutex_type&
{
    return mutex_;
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

    {
        std::scoped_lock const lock(mutex_);
        v.reserve(cache_.size());
        for (auto const& _ : cache_)
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
    std::scoped_lock const lock(mutex_);
    auto const tot = hits_ + misses_;
    if (tot == 0)
        return 0;
    return double(hits_) / tot;
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
        std::scoped_lock const l(mutex_);
        if (auto ret = initialFetch(digest, l))
            return ret;
    }

    auto sle = h();
    if (!sle)
        return {};

    std::scoped_lock const l(mutex_);
    ++misses_;
    auto const [it, inserted] = cache_.emplace(digest, Entry(clock_.now(), std::move(sle)));
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
inline SharedPointerType
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    initialFetch(key_type const& key, std::scoped_lock<mutex_type> const& l)
{
    auto cit = cache_.find(key);
    if (cit == cache_.end())
        return {};

    Entry& entry = cit->second;
    if (entry.isCached())
    {
        ++hits_;
        entry.touch(clock_.now());
        return entry.ptr.getStrong();
    }
    entry.ptr = entry.lock();
    if (entry.isCached())
    {
        // independent of cache size, so not counted as a hit
        ++cacheCount_;
        entry.touch(clock_.now());
        return entry.ptr.getStrong();
    }

    cache_.erase(cit);
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
    stats_.size.set(getCacheSize());

    {
        beast::insight::Gauge::value_type hitRate(0);
        {
            std::scoped_lock const lock(mutex_);
            auto const total(hits_ + misses_);
            if (total != 0)
                hitRate = (hits_ * 100) / total;
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
inline std::thread
TaggedCache<Key, T, IsKeyCache, SharedWeakUnionPointer, SharedPointerType, Hash, KeyEqual, Mutex>::
    sweepHelper(
        clock_type::time_point const& whenExpire,
        [[maybe_unused]] clock_type::time_point const& now,
        typename KeyValueCacheType::map_type& partition,
        SweptPointersVector& stuffToSweep,
        std::atomic<int>& allRemovals,
        std::scoped_lock<std::recursive_mutex> const&)
{
    return std::thread([&, this]() {
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

        allRemovals += cacheRemovals;
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
        clock_type::time_point const& whenExpire,
        clock_type::time_point const& now,
        typename KeyOnlyCacheType::map_type& partition,
        SweptPointersVector&,
        std::atomic<int>& allRemovals,
        std::scoped_lock<std::recursive_mutex> const&)
{
    return std::thread([&, this]() {
        // NOLINTBEGIN https://github.com/XRPLF/rippled/issues/7056
        int cacheRemovals = 0;
        int mapRemovals = 0;
        // NOLINTEND

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

        allRemovals += cacheRemovals;
    });
}

}  // namespace xrpl
