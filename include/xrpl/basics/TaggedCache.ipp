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

#include <xrpl/basics/TaggedCache.h>

namespace ripple {

template <
    class Key,
    class T,
    bool IsKeyCache,
    class Hash,
    class KeyEqual,
    class Mutex>
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::TaggedCache(
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
}

/** Return the clock associated with the cache. */
template <
    class Key,
    class T,
    bool IsKeyCache,
    class Hash,
    class KeyEqual,
    class Mutex>
auto
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::clock() -> clock_type&
{
    return m_clock;
}

/** Returns the number of items in the container. */
template <
    class Key,
    class T,
    bool IsKeyCache,
    class Hash,
    class KeyEqual,
    class Mutex>
std::size_t
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::size() const
{
    std::lock_guard lock(m_mutex);
    return m_cache.size();
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class Hash,
    class KeyEqual,
    class Mutex>
void
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::setTargetSize(int s)
{
    std::lock_guard lock(m_mutex);
    m_target_size = s;

    if (s > 0)
    {
        for (auto& partition : m_cache.map())
        {
            partition.rehash(static_cast<std::size_t>(
                (s + (s >> 2)) /
                    (partition.max_load_factor() * m_cache.partitions()) +
                1));
        }
    }

    JLOG(m_journal.debug()) << m_name << " target size set to " << s;
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class Hash,
    class KeyEqual,
    class Mutex>
auto
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::getTargetAge() const
    -> clock_type::duration
{
    std::lock_guard lock(m_mutex);
    return m_target_age;
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class Hash,
    class KeyEqual,
    class Mutex>
void
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::setTargetAge(
    clock_type::duration s)
{
    std::lock_guard lock(m_mutex);
    m_target_age = s;
    JLOG(m_journal.debug())
        << m_name << " target age set to " << m_target_age.count();
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class Hash,
    class KeyEqual,
    class Mutex>
int
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::getCacheSize() const
{
    std::lock_guard lock(m_mutex);
    return m_cache_count;
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class Hash,
    class KeyEqual,
    class Mutex>
int
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::getTrackSize() const
{
    std::lock_guard lock(m_mutex);
    return m_cache.size();
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class Hash,
    class KeyEqual,
    class Mutex>
float
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::getHitRate()
{
    std::lock_guard lock(m_mutex);
    auto const total = static_cast<float>(m_hits + m_misses);
    return m_hits * (100.0f / std::max(1.0f, total));
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class Hash,
    class KeyEqual,
    class Mutex>
void
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::clear()
{
    std::lock_guard lock(m_mutex);
    m_cache.clear();
    m_cache_count = 0;
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class Hash,
    class KeyEqual,
    class Mutex>
void
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::reset()
{
    std::lock_guard lock(m_mutex);
    m_cache.clear();
    m_cache_count = 0;
    m_hits = 0;
    m_misses = 0;
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class Hash,
    class KeyEqual,
    class Mutex>
template <class KeyComparable>
bool
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::touch_if_exists(
    KeyComparable const& key)
{
    std::lock_guard lock(m_mutex);
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
    class Hash,
    class KeyEqual,
    class Mutex>
void
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::sweep()
{
    // Keep references to all the stuff we sweep
    // For performance, each worker thread should exit before the swept data
    // is destroyed but still within the main cache lock.
    std::vector<SweptPointersVector> allStuffToSweep(m_cache.partitions());

    clock_type::time_point const now(m_clock.now());
    clock_type::time_point when_expire;

    auto const start = std::chrono::steady_clock::now();
    {
        std::lock_guard lock(m_mutex);

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
                lock));
        }
        for (std::thread& worker : workers)
            worker.join();

        m_cache_count -= allRemovals;
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
    class Hash,
    class KeyEqual,
    class Mutex>
bool
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::del(
    const key_type& key,
    bool valid)
{
    // Remove from cache, if !valid, remove from map too. Returns true if
    // removed from cache
    std::lock_guard lock(m_mutex);

    auto cit = m_cache.find(key);

    if (cit == m_cache.end())
        return false;

    Entry& entry = cit->second;

    bool ret = false;

    if (entry.isCached())
    {
        --m_cache_count;
        entry.ptr.reset();
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
    class Hash,
    class KeyEqual,
    class Mutex>
bool
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::canonicalize(
    const key_type& key,
    std::shared_ptr<T>& data,
    std::function<bool(std::shared_ptr<T> const&)>&& replace)
{
    // Return canonical value, store if needed, refresh in cache
    // Return values: true=we had the data already
    std::lock_guard lock(m_mutex);

    auto cit = m_cache.find(key);

    if (cit == m_cache.end())
    {
        m_cache.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(key),
            std::forward_as_tuple(m_clock.now(), data));
        ++m_cache_count;
        return false;
    }

    Entry& entry = cit->second;
    entry.touch(m_clock.now());

    if (entry.isCached())
    {
        if (replace(entry.ptr))
        {
            entry.ptr = data;
            entry.weak_ptr = data;
        }
        else
        {
            data = entry.ptr;
        }

        return true;
    }

    auto cachedData = entry.lock();

    if (cachedData)
    {
        if (replace(entry.ptr))
        {
            entry.ptr = data;
            entry.weak_ptr = data;
        }
        else
        {
            entry.ptr = cachedData;
            data = cachedData;
        }

        ++m_cache_count;
        return true;
    }

    entry.ptr = data;
    entry.weak_ptr = data;
    ++m_cache_count;

    return false;
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class Hash,
    class KeyEqual,
    class Mutex>
bool
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::
    canonicalize_replace_cache(
        const key_type& key,
        std::shared_ptr<T> const& data)
{
    return canonicalize(
        key,
        const_cast<std::shared_ptr<T>&>(data),
        [](std::shared_ptr<T> const&) { return true; });
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class Hash,
    class KeyEqual,
    class Mutex>
bool
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::
    canonicalize_replace_client(const key_type& key, std::shared_ptr<T>& data)
{
    return canonicalize(
        key, data, [](std::shared_ptr<T> const&) { return false; });
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class Hash,
    class KeyEqual,
    class Mutex>
std::shared_ptr<T>
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::fetch(
    const key_type& key)
{
    std::lock_guard<mutex_type> l(m_mutex);
    auto ret = initialFetch(key, l);
    if (!ret)
        ++m_misses;
    return ret;
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class Hash,
    class KeyEqual,
    class Mutex>
template <class ReturnType>
auto
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::insert(
    key_type const& key,
    T const& value) -> std::enable_if_t<!IsKeyCache, ReturnType>
{
    auto p = std::make_shared<T>(std::cref(value));
    return canonicalize_replace_client(key, p);
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class Hash,
    class KeyEqual,
    class Mutex>
template <class ReturnType>
auto
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::insert(
    key_type const& key) -> std::enable_if_t<IsKeyCache, ReturnType>
{
    std::lock_guard lock(m_mutex);
    clock_type::time_point const now(m_clock.now());
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
    class Hash,
    class KeyEqual,
    class Mutex>
bool
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::retrieve(
    const key_type& key,
    T& data)
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
    class Hash,
    class KeyEqual,
    class Mutex>
auto
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::peekMutex()
    -> mutex_type&
{
    return m_mutex;
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class Hash,
    class KeyEqual,
    class Mutex>
auto
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::getKeys() const
    -> std::vector<key_type>
{
    std::vector<key_type> v;

    {
        std::lock_guard lock(m_mutex);
        v.reserve(m_cache.size());
        for (auto const& _ : m_cache)
            v.push_back(_.first);
    }

    return v;
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class Hash,
    class KeyEqual,
    class Mutex>
double
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::rate() const
{
    std::lock_guard lock(m_mutex);
    auto const tot = m_hits + m_misses;
    if (tot == 0)
        return 0;
    return double(m_hits) / tot;
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class Hash,
    class KeyEqual,
    class Mutex>
template <class Handler>
std::shared_ptr<T>
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::fetch(
    key_type const& digest,
    Handler const& h)
{
    {
        std::lock_guard l(m_mutex);
        if (auto ret = initialFetch(digest, l))
            return ret;
    }

    auto sle = h();
    if (!sle)
        return {};

    std::lock_guard l(m_mutex);
    ++m_misses;
    auto const [it, inserted] =
        m_cache.emplace(digest, Entry(m_clock.now(), std::move(sle)));
    if (!inserted)
        it->second.touch(m_clock.now());
    return it->second.ptr;
}
// End CachedSLEs functions.

template <
    class Key,
    class T,
    bool IsKeyCache,
    class Hash,
    class KeyEqual,
    class Mutex>
std::shared_ptr<T>
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::initialFetch(
    key_type const& key,
    std::lock_guard<mutex_type> const& l)
{
    auto cit = m_cache.find(key);
    if (cit == m_cache.end())
        return {};

    Entry& entry = cit->second;
    if (entry.isCached())
    {
        ++m_hits;
        entry.touch(m_clock.now());
        return entry.ptr;
    }
    entry.ptr = entry.lock();
    if (entry.isCached())
    {
        // independent of cache size, so not counted as a hit
        ++m_cache_count;
        entry.touch(m_clock.now());
        return entry.ptr;
    }

    m_cache.erase(cit);
    return {};
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class Hash,
    class KeyEqual,
    class Mutex>
void
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::collect_metrics()
{
    m_stats.size.set(getCacheSize());

    {
        beast::insight::Gauge::value_type hit_rate(0);
        {
            std::lock_guard lock(m_mutex);
            auto const total(m_hits + m_misses);
            if (total != 0)
                hit_rate = (m_hits * 100) / total;
        }
        m_stats.hit_rate.set(hit_rate);
    }
}

template <
    class Key,
    class T,
    bool IsKeyCache,
    class Hash,
    class KeyEqual,
    class Mutex>
std::thread
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::sweepHelper(
    clock_type::time_point const& when_expire,
    [[maybe_unused]] clock_type::time_point const& now,
    typename KeyValueCacheType::map_type& partition,
    SweptPointersVector& stuffToSweep,
    std::atomic<int>& allRemovals,
    std::lock_guard<std::recursive_mutex> const&)
{
    return std::thread([&, this]() {
        int cacheRemovals = 0;
        int mapRemovals = 0;

        // Keep references to all the stuff we sweep
        // so that we can destroy them outside the lock.
        stuffToSweep.first.reserve(partition.size());
        stuffToSweep.second.reserve(partition.size());
        {
            auto cit = partition.begin();
            while (cit != partition.end())
            {
                if (cit->second.isWeak())
                {
                    // weak
                    if (cit->second.isExpired())
                    {
                        stuffToSweep.second.push_back(
                            std::move(cit->second.weak_ptr));
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
                        stuffToSweep.first.push_back(
                            std::move(cit->second.ptr));
                        ++mapRemovals;
                        cit = partition.erase(cit);
                    }
                    else
                    {
                        // remains weakly cached
                        cit->second.ptr.reset();
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
    class Hash,
    class KeyEqual,
    class Mutex>
std::thread
TaggedCache<Key, T, IsKeyCache, Hash, KeyEqual, Mutex>::sweepHelper(
    clock_type::time_point const& when_expire,
    clock_type::time_point const& now,
    typename KeyOnlyCacheType::map_type& partition,
    SweptPointersVector&,
    std::atomic<int>& allRemovals,
    std::lock_guard<std::recursive_mutex> const&)
{
    return std::thread([&, this]() {
        int cacheRemovals = 0;
        int mapRemovals = 0;

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

}  // namespace ripple

#endif
