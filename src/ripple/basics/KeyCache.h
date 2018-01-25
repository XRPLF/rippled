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

#ifndef RIPPLE_BASICS_KEYCACHE_H_INCLUDED
#define RIPPLE_BASICS_KEYCACHE_H_INCLUDED

#include <ripple/basics/hardened_hash.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/beast/clock/abstract_clock.h>
#include <ripple/beast/insight/Insight.h>
#include <mutex>

namespace ripple {

/** Maintains a cache of keys with no associated data.

    The cache has a target size and an expiration time. When cached items become
    older than the maximum age they are eligible for removal during a
    call to @ref sweep.
*/
// VFALCO TODO Figure out how to pass through the allocator
template <
    class Key,
    class Hash = hardened_hash <>,
    class KeyEqual = std::equal_to <Key>,
    //class Allocator = std::allocator <std::pair <Key const, Entry>>,
    class Mutex = std::mutex
>
class KeyCache
{
public:
    using key_type = Key;
    using clock_type = beast::abstract_clock <std::chrono::steady_clock>;

private:
    struct Stats
    {
        template <class Handler>
        Stats (std::string const& prefix, Handler const& handler,
            beast::insight::Collector::ptr const& collector)
            : hook (collector->make_hook (handler))
            , size (collector->make_gauge (prefix, "size"))
            , hit_rate (collector->make_gauge (prefix, "hit_rate"))
            , hits (0)
            , misses (0)
            { }

        beast::insight::Hook hook;
        beast::insight::Gauge size;
        beast::insight::Gauge hit_rate;

        std::size_t hits;
        std::size_t misses;
    };

    struct Entry
    {
        explicit Entry (clock_type::time_point const& last_access_)
            : last_access (last_access_)
        {
        }

        clock_type::time_point last_access;
    };

    using map_type = hardened_hash_map <key_type, Entry, Hash, KeyEqual>;
    using iterator = typename map_type::iterator;
    using lock_guard = std::lock_guard <Mutex>;

public:
    using size_type = typename map_type::size_type;

private:
    Mutex mutable m_mutex;
    map_type m_map;
    Stats mutable m_stats;
    clock_type& m_clock;
    std::string const m_name;
    size_type m_target_size;
    clock_type::duration m_target_age;

public:
    /** Construct with the specified name.

        @param size The initial target size.
        @param age  The initial expiration time.
    */
    KeyCache (std::string const& name, clock_type& clock,
        beast::insight::Collector::ptr const& collector, size_type target_size = 0,
            clock_type::rep expiration_seconds = 120)
        : m_stats (name,
            std::bind (&KeyCache::collect_metrics, this),
                collector)
        , m_clock (clock)
        , m_name (name)
        , m_target_size (target_size)
        , m_target_age (std::chrono::seconds (expiration_seconds))
    {
    }

    // VFALCO TODO Use a forwarding constructor call here
    KeyCache (std::string const& name, clock_type& clock,
        size_type target_size = 0, clock_type::rep expiration_seconds = 120)
        : m_stats (name,
            std::bind (&KeyCache::collect_metrics, this),
                beast::insight::NullCollector::New ())
        , m_clock (clock)
        , m_name (name)
        , m_target_size (target_size)
        , m_target_age (std::chrono::seconds (expiration_seconds))
    {
    }

    //--------------------------------------------------------------------------

    /** Retrieve the name of this object. */
    std::string const& name () const
    {
        return m_name;
    }

    /** Return the clock associated with the cache. */
    clock_type& clock ()
    {
        return m_clock;
    }

    /** Returns the number of items in the container. */
    size_type size () const
    {
        lock_guard lock (m_mutex);
        return m_map.size ();
    }

    /** Empty the cache */
    void clear ()
    {
        lock_guard lock (m_mutex);
        m_map.clear ();
    }

    void reset ()
    {
        lock_guard lock(m_mutex);
        m_map.clear();
        m_stats.hits = 0;
        m_stats.misses = 0;
    }

    void setTargetSize (size_type s)
    {
        lock_guard lock (m_mutex);
        m_target_size = s;
    }

    void setTargetAge (size_type s)
    {
        lock_guard lock (m_mutex);
        m_target_age = std::chrono::seconds (s);
    }

    /** Returns `true` if the key was found.
        Does not update the last access time.
    */
    template <class KeyComparable>
    bool exists (KeyComparable const& key) const
    {
        lock_guard lock (m_mutex);
        typename map_type::const_iterator const iter (m_map.find (key));
        if (iter != m_map.end ())
        {
            ++m_stats.hits;
            return true;
        }
        ++m_stats.misses;
        return false;
    }

    /** Insert the specified key.
        The last access time is refreshed in all cases.
        @return `true` If the key was newly inserted.
    */
    bool insert (Key const& key)
    {
        lock_guard lock (m_mutex);
        clock_type::time_point const now (m_clock.now ());
        std::pair <iterator, bool> result (m_map.emplace (
            std::piecewise_construct, std::forward_as_tuple (key),
                std::forward_as_tuple (now)));
        if (! result.second)
        {
            result.first->second.last_access = now;
            return false;
        }
        return true;
    }

    /** Refresh the last access time on a key if present.
        @return `true` If the key was found.
    */
    template <class KeyComparable>
    bool touch_if_exists (KeyComparable const& key)
    {
        lock_guard lock (m_mutex);
        iterator const iter (m_map.find (key));
        if (iter == m_map.end ())
        {
            ++m_stats.misses;
            return false;
        }
        iter->second.last_access = m_clock.now ();
        ++m_stats.hits;
        return true;
    }

    /** Remove the specified cache entry.
        @param key The key to remove.
        @return `false` If the key was not found.
    */
    bool erase (key_type const& key)
    {
        lock_guard lock (m_mutex);
        if (m_map.erase (key) > 0)
        {
            ++m_stats.hits;
            return true;
        }
        ++m_stats.misses;
        return false;
    }

    /** Remove stale entries from the cache. */
    void sweep ()
    {
        clock_type::time_point const now (m_clock.now ());
        clock_type::time_point when_expire;

        lock_guard lock (m_mutex);

        if (m_target_size == 0 ||
            (m_map.size () <= m_target_size))
        {
            when_expire = now - m_target_age;
        }
        else
        {
            when_expire = now -
                m_target_age * m_target_size / m_map.size();

            clock_type::duration const minimumAge (
                std::chrono::seconds (1));
            if (when_expire > (now - minimumAge))
                when_expire = now - minimumAge;
        }

        iterator it = m_map.begin ();

        while (it != m_map.end ())
        {
            if (it->second.last_access > now)
            {
                it->second.last_access = now;
                ++it;
            }
            else if (it->second.last_access <= when_expire)
            {
                it = m_map.erase (it);
            }
            else
            {
                ++it;
            }
        }
    }

private:
    void collect_metrics ()
    {
        m_stats.size.set (size ());

        {
            beast::insight::Gauge::value_type hit_rate (0);
            {
                lock_guard lock (m_mutex);
                auto const total (m_stats.hits + m_stats.misses);
                if (total != 0)
                    hit_rate = (m_stats.hits * 100) / total;
            }
            m_stats.hit_rate.set (hit_rate);
        }
    }
};

}

#endif
