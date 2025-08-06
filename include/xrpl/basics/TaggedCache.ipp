//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_BASICS_TAGGEDCACHE_IPP_INCLUDED
#define RIPPLE_BASICS_TAGGEDCACHE_IPP_INCLUDED

#include <xrpl/basics/IntrusivePointer.ipp>
#include <xrpl/basics/TaggedCache.h>
#include <xrpl/beast/core/CurrentThreadName.h>

namespace ripple {

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
    : m_journal(journal)
    , m_clock(clock)
    , m_stats(name, std::bind(&TaggedCache::collect_metrics, this), collector)
    , m_name(name)
    , m_target_size(size)
    , m_target_age(expiration)
    , m_cache_count(0)
    , m_hits(0)
    , m_misses(0)
{
    partitionLocks_ = std::vector<mutex_type>(m_cache.partitions());
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
TaggedCache<
    Key,
    T,
    IsKeyCache,
    SharedWeakUnionPointer,
    SharedPointerType,
    Hash,
    KeyEqual,
    Mutex>::clock() -> clock_type&
{
    return m_clock;
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
TaggedCache<
    Key,
    T,
    IsKeyCache,
    SharedWeakUnionPointer,
    SharedPointerType,
    Hash,
    KeyEqual,
    Mutex>::size() const
{
    std::size_t totalSize = 0;
    for (size_t i = 0; i < partitionLocks_.size(); ++i)
    {
        std::lock_guard<Mutex> lock(partitionLocks_[i]);
        totalSize += m_cache.map()[i].size();
    }
    return totalSize;
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
TaggedCache<
    Key,
    T,
    IsKeyCache,
    SharedWeakUnionPointer,
    SharedPointerType,
    Hash,
    KeyEqual,
    Mutex>::getCacheSize() const
{
    return m_cache_count.load(std::memory_order_relaxed);
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
TaggedCache<
    Key,
    T,
    IsKeyCache,
    SharedWeakUnionPointer,
    SharedPointerType,
    Hash,
    KeyEqual,
    Mutex>::getHitRate()
{
    auto hits = m_hits.load(std::memory_order_relaxed);
    auto misses = m_misses.load(std::memory_order_relaxed);
    float total = float(hits + misses);
    return hits * (100.0f / std::max(1.0f, total));
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
TaggedCache<
    Key,
    T,
    IsKeyCache,
    SharedWeakUnionPointer,
    SharedPointerType,
    Hash,
    KeyEqual,
    Mutex>::clear()
{
    for (auto& mutex : partitionLocks_)
        mutex.lock();
    m_cache.clear();
    for (auto& mutex : partitionLocks_)
        mutex.unlock();
    m_cache_count.store(0, std::memory_order_relaxed);
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
TaggedCache<
    Key,
    T,
    IsKeyCache,
    SharedWeakUnionPointer,
    SharedPointerType,
    Hash,
    KeyEqual,
    Mutex>::reset()
{
    for (auto& mutex : partitionLocks_)
        mutex.lock();
    m_cache.clear();
    for (auto& mutex : partitionLocks_)
        mutex.unlock();
    m_cache_count.store(0, std::memory_order_relaxed);
    m_hits.store(0, std::memory_order_relaxed);
    m_misses.store(0, std::memory_order_relaxed);
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
TaggedCache<
    Key,
    T,
    IsKeyCache,
    SharedWeakUnionPointer,
    SharedPointerType,
    Hash,
    KeyEqual,
    Mutex>::touch_if_exists(KeyComparable const& key)
{
    std::lock_guard<Mutex> lock(lockPartition(key));
    auto const iter(m_cache.find(key));
    if (iter == m_cache.end())
    {
        ++m_stats.misses;
        return false;
    }
    iter->second.touch(m_clock.now());
    ++m_stats.hits;
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
TaggedCache<
    Key,
    T,
    IsKeyCache,
    SharedWeakUnionPointer,
    SharedPointerType,
    Hash,
    KeyEqual,
    Mutex>::sweep()
{
    // Keep references to all the stuff we sweep
    // For performance, each worker thread should exit before the swept data
    // is destroyed but still within the main cache lock.
    std::vector<SweptPointersVector> allStuffToSweep(m_cache.partitions());

    clock_type::time_point const now(m_clock.now());
    clock_type::time_point when_expire;

    auto const start = std::chrono::steady_clock::now();
    {
        if (m_target_size == 0 ||
            (static_cast<int>(m_cache.size()) <= m_target_size))
        {
            when_expire = now - m_target_age;
        }
        else
        {
            when_expire = now - m_target_age * m_target_size / m_cache.size();

            clock_type::duration const minimumAge(std::chrono::seconds(1));
            if (when_expire > (now - minimumAge))
                when_expire = now - minimumAge;

            JLOG(m_journal.trace())
                << m_name << " is growing fast " << m_cache.size() << " of "
                << m_target_size << " aging at " << (now - when_expire).count()
                << " of " << m_target_age.count();
        }

        std::vector<std::thread> workers;
        workers.reserve(m_cache.partitions());
        std::atomic<int> allRemovals = 0;

        for (std::size_t p = 0; p < m_cache.partitions(); ++p)
        {
            workers.push_back(sweepHelper(
                when_expire,
                now,
                m_cache.map()[p],
                allStuffToSweep[p],
                allRemovals,
                partitionLocks_[p]));
        }
        for (std::thread& worker : workers)
            worker.join();

        int removals = allRemovals.load(std::memory_order_relaxed);
        m_cache_count.fetch_sub(removals, std::memory_order_relaxed);
    }
    // At this point allStuffToSweep will go out of scope outside the lock
    // and decrement the reference count on each strong pointer.
    JLOG(m_journal.debug())
        << m_name << " TaggedCache sweep lock duration "
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
TaggedCache<
    Key,
    T,
    IsKeyCache,
    SharedWeakUnionPointer,
    SharedPointerType,
    Hash,
    KeyEqual,
    Mutex>::del(key_type const& key, bool valid)
{
    // Remove from cache, if !valid, remove from map too. Returns true if
    // removed from cache

    std::lock_guard<Mutex> lock(lockPartition(key));

    auto cit = m_cache.find(key);

    if (cit == m_cache.end())
        return false;

    Entry& entry = cit->second;

    bool ret = false;

    if (entry.isCached())
    {
        m_cache_count.fetch_sub(1, std::memory_order_relaxed);
        entry.ptr.convertToWeak();
        ret = true;
    }

    if (!valid || entry.isExpired())
        m_cache.erase(cit);

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
template <class R>
inline bool
TaggedCache<
    Key,
    T,
    IsKeyCache,
    SharedWeakUnionPointer,
    SharedPointerType,
    Hash,
    KeyEqual,
    Mutex>::
    canonicalize(
        key_type const& key,
        SharedPointerType& data,
        R&& replaceCallback)
{
    // Return canonical value, store if needed, refresh in cache
    // Return values: true=we had the data already

    std::lock_guard<Mutex> lock(lockPartition(key));
    auto cit = m_cache.find(key);
    if (cit == m_cache.end())
    {
        m_cache.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(key),
            std::forward_as_tuple(m_clock.now(), data));
        m_cache_count.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    Entry& entry = cit->second;
    entry.touch(m_clock.now());

    auto shouldReplace = [&] {
        if constexpr (std::is_invocable_r_v<bool, R>)
        {
            // The reason for this extra complexity is for intrusive
            // strong/weak combo getting a strong is relatively expensive
            // and not needed for many cases.
            return replaceCallback();
        }
        else
        {
            return replaceCallback(entry.ptr.getStrong());
        }
    };

    if (entry.isCached())
    {
        if (shouldReplace())
        {
            entry.ptr = data;
        }
        else
        {
            data = entry.ptr.getStrong();
        }

        return true;
    }

    auto cachedData = entry.lock();

    if (cachedData)
    {
        if (shouldReplace())
        {
            entry.ptr = data;
        }
        else
        {
            entry.ptr.convertToStrong();
            data = cachedData;
        }

        m_cache_count.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    entry.ptr = data;
    m_cache_count.fetch_add(1, std::memory_order_relaxed);

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
inline bool
TaggedCache<
    Key,
    T,
    IsKeyCache,
    SharedWeakUnionPointer,
    SharedPointerType,
    Hash,
    KeyEqual,
    Mutex>::
    canonicalize_replace_cache(
        key_type const& key,
        SharedPointerType const& data)
{
    return canonicalize(
        key, const_cast<SharedPointerType&>(data), []() { return true; });
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
TaggedCache<
    Key,
    T,
    IsKeyCache,
    SharedWeakUnionPointer,
    SharedPointerType,
    Hash,
    KeyEqual,
    Mutex>::
    canonicalize_replace_client(key_type const& key, SharedPointerType& data)
{
    return canonicalize(key, data, []() { return false; });
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
TaggedCache<
    Key,
    T,
    IsKeyCache,
    SharedWeakUnionPointer,
    SharedPointerType,
    Hash,
    KeyEqual,
    Mutex>::fetch(key_type const& key)
{
    std::lock_guard<Mutex> lock(lockPartition(key));

    auto ret = initialFetch(key);
    if (!ret)
        m_misses.fetch_add(1, std::memory_order_relaxed);
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
TaggedCache<
    Key,
    T,
    IsKeyCache,
    SharedWeakUnionPointer,
    SharedPointerType,
    Hash,
    KeyEqual,
    Mutex>::insert(key_type const& key, T const& value)
    -> std::enable_if_t<!IsKeyCache, ReturnType>
{
    static_assert(
        std::is_same_v<std::shared_ptr<T>, SharedPointerType> ||
        std::is_same_v<intr_ptr::SharedPtr<T>, SharedPointerType>);

    if constexpr (std::is_same_v<std::shared_ptr<T>, SharedPointerType>)
    {
        auto p = std::make_shared<T>(std::cref(value));
        return canonicalize_replace_client(key, p);
    }
    if constexpr (std::is_same_v<intr_ptr::SharedPtr<T>, SharedPointerType>)
    {
        auto p = intr_ptr::make_shared<T>(std::cref(value));
        return canonicalize_replace_client(key, p);
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
TaggedCache<
    Key,
    T,
    IsKeyCache,
    SharedWeakUnionPointer,
    SharedPointerType,
    Hash,
    KeyEqual,
    Mutex>::insert(key_type const& key)
    -> std::enable_if_t<IsKeyCache, ReturnType>
{
    clock_type::time_point const now(m_clock.now());
    std::lock_guard<Mutex> lock(lockPartition(key));
    auto [it, inserted] = m_cache.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(key),
        std::forward_as_tuple(now));
    if (!inserted)
        it->second.last_access = now;
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
TaggedCache<
    Key,
    T,
    IsKeyCache,
    SharedWeakUnionPointer,
    SharedPointerType,
    Hash,
    KeyEqual,
    Mutex>::retrieve(key_type const& key, T& data)
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
TaggedCache<
    Key,
    T,
    IsKeyCache,
    SharedWeakUnionPointer,
    SharedPointerType,
    Hash,
    KeyEqual,
    Mutex>::getKeys() const -> std::vector<key_type>
{
    std::vector<key_type> v;

    {
        v.reserve(m_cache.size());
        for (std::size_t i = 0; i < partitionLocks_.size(); ++i)
        {
            std::lock_guard<Mutex> lock(partitionLocks_[i]);
            for (auto const& entry : m_cache.map()[i])
                v.push_back(entry.first);
        }
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
TaggedCache<
    Key,
    T,
    IsKeyCache,
    SharedWeakUnionPointer,
    SharedPointerType,
    Hash,
    KeyEqual,
    Mutex>::rate() const
{
    auto hits = m_hits.load(std::memory_order_relaxed);
    auto misses = m_misses.load(std::memory_order_relaxed);
    auto const tot = hits + misses;
    if (tot == 0)
        return 0.0;
    return double(hits) / tot;
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
TaggedCache<
    Key,
    T,
    IsKeyCache,
    SharedWeakUnionPointer,
    SharedPointerType,
    Hash,
    KeyEqual,
    Mutex>::fetch(key_type const& digest, Handler const& h)
{
    std::lock_guard<Mutex> lock(lockPartition(digest));

    if (auto ret = initialFetch(digest))
        return ret;

    auto sle = h();
    if (!sle)
        return {};

    m_misses.fetch_add(1, std::memory_order_relaxed);
    auto const [it, inserted] =
        m_cache.emplace(digest, Entry(m_clock.now(), std::move(sle)));
    if (!inserted)
        it->second.touch(m_clock.now());
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
TaggedCache<
    Key,
    T,
    IsKeyCache,
    SharedWeakUnionPointer,
    SharedPointerType,
    Hash,
    KeyEqual,
    Mutex>::initialFetch(key_type const& key)
{
    std::lock_guard<Mutex> lock(lockPartition(key));

    auto cit = m_cache.find(key);
    if (cit == m_cache.end())
        return {};

    Entry& entry = cit->second;
    if (entry.isCached())
    {
        m_hits.fetch_add(1, std::memory_order_relaxed);
        entry.touch(m_clock.now());
        return entry.ptr.getStrong();
    }
    entry.ptr = entry.lock();
    if (entry.isCached())
    {
        // independent of cache size, so not counted as a hit
        m_cache_count.fetch_add(1, std::memory_order_relaxed);
        entry.touch(m_clock.now());
        return entry.ptr.getStrong();
    }

    m_cache.erase(cit);

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
TaggedCache<
    Key,
    T,
    IsKeyCache,
    SharedWeakUnionPointer,
    SharedPointerType,
    Hash,
    KeyEqual,
    Mutex>::collect_metrics()
{
    m_stats.size.set(getCacheSize());

    {
        beast::insight::Gauge::value_type hit_rate(0);
        {
            auto const hits = m_hits.load(std::memory_order_relaxed);
            auto const misses = m_misses.load(std::memory_order_relaxed);
            auto const total = hits + misses;
            if (total != 0)
                hit_rate = (hits * 100) / total;
        }
        m_stats.hit_rate.set(hit_rate);
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
TaggedCache<
    Key,
    T,
    IsKeyCache,
    SharedWeakUnionPointer,
    SharedPointerType,
    Hash,
    KeyEqual,
    Mutex>::
    sweepHelper(
        clock_type::time_point const& when_expire,
        [[maybe_unused]] clock_type::time_point const& now,
        typename KeyValueCacheType::map_type& partition,
        SweptPointersVector& stuffToSweep,
        std::atomic<int>& allRemovals,
        Mutex& partitionLock)
{
    return std::thread([&, this]() {
        beast::setCurrentThreadName("sweep-KeyValueCache");

        int cacheRemovals = 0;
        int mapRemovals = 0;

        std::lock_guard<Mutex> lock(partitionLock);

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
                else if (cit->second.last_access <= when_expire)
                {
                    // strong, expired
                    ++cacheRemovals;
                    if (cit->second.ptr.use_count() == 1)
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
            JLOG(m_journal.debug())
                << "TaggedCache partition sweep " << m_name
                << ": cache = " << partition.size() << "-" << cacheRemovals
                << ", map-=" << mapRemovals;
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
TaggedCache<
    Key,
    T,
    IsKeyCache,
    SharedWeakUnionPointer,
    SharedPointerType,
    Hash,
    KeyEqual,
    Mutex>::
    sweepHelper(
        clock_type::time_point const& when_expire,
        clock_type::time_point const& now,
        typename KeyOnlyCacheType::map_type& partition,
        SweptPointersVector&,
        std::atomic<int>& allRemovals,
        Mutex& partitionLock)
{
    return std::thread([&, this]() {
        beast::setCurrentThreadName("sweep-KeyOnlyCache");

        int cacheRemovals = 0;
        int mapRemovals = 0;

        std::lock_guard<Mutex> lock(partitionLock);

        // Keep references to all the stuff we sweep
        // so that we can destroy them outside the lock.
        {
            auto cit = partition.begin();
            while (cit != partition.end())
            {
                if (cit->second.last_access > now)
                {
                    cit->second.last_access = now;
                    ++cit;
                }
                else if (cit->second.last_access <= when_expire)
                {
                    cit = partition.erase(cit);
                }
                else
                {
                    ++cit;
                }
            }
        }

        if (mapRemovals || cacheRemovals)
        {
            JLOG(m_journal.debug())
                << "TaggedCache partition sweep " << m_name
                << ": cache = " << partition.size() << "-" << cacheRemovals
                << ", map-=" << mapRemovals;
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
inline Mutex&
TaggedCache<
    Key,
    T,
    IsKeyCache,
    SharedWeakUnionPointer,
    SharedPointerType,
    Hash,
    KeyEqual,
    Mutex>::lockPartition(key_type const& key) const
{
    return partitionLocks_[m_cache.partition_index(key)];
}

}  // namespace ripple

#endif
