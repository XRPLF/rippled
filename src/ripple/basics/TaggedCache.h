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

#ifndef RIPPLE_BASICS_TAGGEDCACHE_H_INCLUDED
#define RIPPLE_BASICS_TAGGEDCACHE_H_INCLUDED

#include <ripple/basics/Log.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/basics/hardened_hash.h>
#include <ripple/beast/clock/abstract_clock.h>
#include <ripple/beast/insight/Insight.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace ripple {

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
    class Hash = hardened_hash<>,
    class KeyEqual = std::equal_to<Key>,
    class Mutex = std::recursive_mutex>
class TaggedCache
{
public:
    using mutex_type = Mutex;
    using key_type = Key;
    using mapped_type = T;
    using clock_type = beast::abstract_clock<std::chrono::steady_clock>;

public:
    TaggedCache(
        std::string const& name,
        int size,
        clock_type::duration expiration,
        clock_type& clock,
        beast::Journal journal,
        beast::insight::Collector::ptr const& collector =
            beast::insight::NullCollector::New())
        : m_journal(journal)
        , m_clock(clock)
        , m_stats(
              name,
              std::bind(&TaggedCache::collect_metrics, this),
              collector)
        , m_name(name)
        , m_target_size(size)
        , m_target_age(expiration)
        , m_cache_count(0)
        , m_hits(0)
        , m_misses(0)
    {
    }

public:
    /** Return the clock associated with the cache. */
    clock_type&
    clock()
    {
        return m_clock;
    }

    /** Returns the number of items in the container. */
    std::size_t
    size() const
    {
        std::lock_guard lock(m_mutex);
        return m_cache.size();
    }

    void
    setTargetSize(int s)
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

    clock_type::duration
    getTargetAge() const
    {
        std::lock_guard lock(m_mutex);
        return m_target_age;
    }

    void
    setTargetAge(clock_type::duration s)
    {
        std::lock_guard lock(m_mutex);
        m_target_age = s;
        JLOG(m_journal.debug())
            << m_name << " target age set to " << m_target_age.count();
    }

    int
    getCacheSize() const
    {
        std::lock_guard lock(m_mutex);
        return m_cache_count;
    }

    int
    getTrackSize() const
    {
        std::lock_guard lock(m_mutex);
        return m_cache.size();
    }

    float
    getHitRate()
    {
        std::lock_guard lock(m_mutex);
        auto const total = static_cast<float>(m_hits + m_misses);
        return m_hits * (100.0f / std::max(1.0f, total));
    }

    void
    clear()
    {
        std::lock_guard lock(m_mutex);
        m_cache.clear();
        m_cache_count = 0;
    }

    void
    reset()
    {
        std::lock_guard lock(m_mutex);
        m_cache.clear();
        m_cache_count = 0;
        m_hits = 0;
        m_misses = 0;
    }

    /** Refresh the last access time on a key if present.
        @return `true` If the key was found.
    */
    template <class KeyComparable>
    bool
    touch_if_exists(KeyComparable const& key)
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

    using SweptPointersVector = std::pair<
        std::vector<std::shared_ptr<mapped_type>>,
        std::vector<std::weak_ptr<mapped_type>>>;

    void
    sweep()
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
                when_expire =
                    now - m_target_age * m_target_size / m_cache.size();

                clock_type::duration const minimumAge(std::chrono::seconds(1));
                if (when_expire > (now - minimumAge))
                    when_expire = now - minimumAge;

                JLOG(m_journal.trace())
                    << m_name << " is growing fast " << m_cache.size() << " of "
                    << m_target_size << " aging at "
                    << (now - when_expire).count() << " of "
                    << m_target_age.count();
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

    bool
    del(const key_type& key, bool valid)
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

    /** Replace aliased objects with originals.

        Due to concurrency it is possible for two separate objects with
        the same content and referring to the same unique "thing" to exist.
        This routine eliminates the duplicate and performs a replacement
        on the callers shared pointer if needed.

        @param key The key corresponding to the object
        @param data A shared pointer to the data corresponding to the object.
        @param replace Function that decides if cache should be replaced

        @return `true` If the key already existed.
    */
public:
    bool
    canonicalize(
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

    bool
    canonicalize_replace_cache(
        const key_type& key,
        std::shared_ptr<T> const& data)
    {
        return canonicalize(
            key,
            const_cast<std::shared_ptr<T>&>(data),
            [](std::shared_ptr<T> const&) { return true; });
    }

    bool
    canonicalize_replace_client(const key_type& key, std::shared_ptr<T>& data)
    {
        return canonicalize(
            key, data, [](std::shared_ptr<T> const&) { return false; });
    }

    std::shared_ptr<T>
    fetch(const key_type& key)
    {
        std::lock_guard<mutex_type> l(m_mutex);
        auto ret = initialFetch(key, l);
        if (!ret)
            ++m_misses;
        return ret;
    }

    /** Insert the element into the container.
        If the key already exists, nothing happens.
        @return `true` If the element was inserted
    */
    template <class ReturnType = bool>
    auto
    insert(key_type const& key, T const& value)
        -> std::enable_if_t<!IsKeyCache, ReturnType>
    {
        auto p = std::make_shared<T>(std::cref(value));
        return canonicalize_replace_client(key, p);
    }

    template <class ReturnType = bool>
    auto
    insert(key_type const& key) -> std::enable_if_t<IsKeyCache, ReturnType>
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

    // VFALCO NOTE It looks like this returns a copy of the data in
    //             the output parameter 'data'. This could be expensive.
    //             Perhaps it should work like standard containers, which
    //             simply return an iterator.
    //
    bool
    retrieve(const key_type& key, T& data)
    {
        // retrieve the value of the stored data
        auto entry = fetch(key);

        if (!entry)
            return false;

        data = *entry;
        return true;
    }

    mutex_type&
    peekMutex()
    {
        return m_mutex;
    }

    std::vector<key_type>
    getKeys() const
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

    // CachedSLEs functions.
    /** Returns the fraction of cache hits. */
    double
    rate() const
    {
        std::lock_guard lock(m_mutex);
        auto const tot = m_hits + m_misses;
        if (tot == 0)
            return 0;
        return double(m_hits) / tot;
    }

    /** Fetch an item from the cache.
        If the digest was not found, Handler
        will be called with this signature:
            std::shared_ptr<SLE const>(void)
    */
    template <class Handler>
    std::shared_ptr<T>
    fetch(key_type const& digest, Handler const& h)
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

private:
    std::shared_ptr<T>
    initialFetch(key_type const& key, std::lock_guard<mutex_type> const& l)
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

    void
    collect_metrics()
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

private:
    struct Stats
    {
        template <class Handler>
        Stats(
            std::string const& prefix,
            Handler const& handler,
            beast::insight::Collector::ptr const& collector)
            : hook(collector->make_hook(handler))
            , size(collector->make_gauge(prefix, "size"))
            , hit_rate(collector->make_gauge(prefix, "hit_rate"))
            , hits(0)
            , misses(0)
        {
        }

        beast::insight::Hook hook;
        beast::insight::Gauge size;
        beast::insight::Gauge hit_rate;

        std::size_t hits;
        std::size_t misses;
    };

    class KeyOnlyEntry
    {
    public:
        clock_type::time_point last_access;

        explicit KeyOnlyEntry(clock_type::time_point const& last_access_)
            : last_access(last_access_)
        {
        }

        void
        touch(clock_type::time_point const& now)
        {
            last_access = now;
        }
    };

    class ValueEntry
    {
    public:
        std::shared_ptr<mapped_type> ptr;
        std::weak_ptr<mapped_type> weak_ptr;
        clock_type::time_point last_access;

        ValueEntry(
            clock_type::time_point const& last_access_,
            std::shared_ptr<mapped_type> const& ptr_)
            : ptr(ptr_), weak_ptr(ptr_), last_access(last_access_)
        {
        }

        bool
        isWeak() const
        {
            return ptr == nullptr;
        }
        bool
        isCached() const
        {
            return ptr != nullptr;
        }
        bool
        isExpired() const
        {
            return weak_ptr.expired();
        }
        std::shared_ptr<mapped_type>
        lock()
        {
            return weak_ptr.lock();
        }
        void
        touch(clock_type::time_point const& now)
        {
            last_access = now;
        }
    };

    typedef
        typename std::conditional<IsKeyCache, KeyOnlyEntry, ValueEntry>::type
            Entry;

    using KeyOnlyCacheType =
        hardened_partitioned_hash_map<key_type, KeyOnlyEntry, Hash, KeyEqual>;

    using KeyValueCacheType =
        hardened_partitioned_hash_map<key_type, ValueEntry, Hash, KeyEqual>;

    using cache_type =
        hardened_partitioned_hash_map<key_type, Entry, Hash, KeyEqual>;

    [[nodiscard]] std::thread
    sweepHelper(
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

    [[nodiscard]] std::thread
    sweepHelper(
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
    };

    beast::Journal m_journal;
    clock_type& m_clock;
    Stats m_stats;

    mutex_type mutable m_mutex;

    // Used for logging
    std::string m_name;

    // Desired number of cache entries (0 = ignore)
    int m_target_size;

    // Desired maximum cache age
    clock_type::duration m_target_age;

    // Number of items cached
    int m_cache_count;
    cache_type m_cache;  // Hold strong reference to recent objects
    std::uint64_t m_hits;
    std::uint64_t m_misses;
};

}  // namespace ripple

#endif
