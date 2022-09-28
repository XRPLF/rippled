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

#ifndef RIPPLE_SHAMAP_FULLBELOWCACHE_H_INCLUDED
#define RIPPLE_SHAMAP_FULLBELOWCACHE_H_INCLUDED

#include <ripple/basics/Log.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/basics/base_uint.h>
#include <ripple/basics/hardened_hash.h>
#include <ripple/beast/clock/abstract_clock.h>
#include <ripple/beast/insight/Collector.h>
#include <ripple/beast/insight/Insight.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/beast/utility/WrappedSink.h>
#include <ripple/beast/utility/atomic_shared_ptr.h>
#include <ripple/json/json_value.h>
#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace ripple {

namespace detail {

template <
    class Key,
    class Hash = hardened_hash<>,
    class KeyEqual = std::equal_to<Key>>
class FullBelowCacheImpl
{
public:
    using key_type = Key;
    using clock_type = beast::abstract_clock<std::chrono::steady_clock>;

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
        {
        }

        beast::insight::Hook hook;
        beast::insight::Gauge size;
        beast::insight::Gauge hit_rate;
    };

    Hash hash_;

    // Used for logging
    std::string name_;

    beast::WrappedSink sink_;
    beast::Journal journal_;
    clock_type& clock_;
    Stats m_stats;

    // Desired number of cache entries (0 = ignore)
    std::size_t const targetSize_;

    // Desired maximum cache age
    std::chrono::seconds const targetAge_;

    /** Map partitions:

        The idea is to partition the key space into multiple, independent
        maps, each with their own lock. This helps to increase concurrency
        since it's possible to operate on multiple partitions at a time.
     */
    struct Partition
    {
        std::mutex mutable mutex;
        std::size_t const index;
        hardened_hash_map<Key, clock_type::time_point, Hash, KeyEqual> items;

        Partition(std::size_t i) : index(i)
        {
        }
    };

    /** The partitions that, together, map the entire key space. */
    std::array<Partition, 64> partitions_;

    /** Number of items where we have either a strong or weak pointer to. */
    std::atomic<std::size_t> size_ = 0;

    /** The number of times that we found an item in the cache */
    std::atomic<std::uint64_t> hits_ = 0;
    std::atomic<std::uint64_t> misses_ = 0;

    Partition&
    getPartition(Key const& key) noexcept
    {
        return partitions_[hash_(key) % partitions_.size()];
    }

private:
    template <std::size_t... Is>
    FullBelowCacheImpl(
        std::string name,
        std::size_t targetSize,
        std::chrono::seconds expiration,
        clock_type& clock,
        beast::Journal journal,
        beast::insight::Collector::ptr const& collector,
        std::index_sequence<Is...>)
        : name_(std::move(name))
        , sink_(journal, "[" + name_ + "] ")
        , journal_(sink_)
        , clock_(clock)
        , m_stats(
              name_,
              [this]() {
                  m_stats.size.set(size());
                  m_stats.hit_rate.set([this]() {
                      auto const h = hits_.load();
                      auto const m = misses_.load();

                      auto ret = h + m;

                      if (ret != 0)
                          ret = (h * 100) / ret;

                      return ret;
                  }());
              },
              collector)
        , targetSize_(targetSize)
        , targetAge_(expiration)
        , partitions_{(Is)...}
    {
    }

public:
    FullBelowCacheImpl(
        std::string name,
        std::size_t targetSize,
        std::chrono::seconds expiration,
        clock_type& clock,
        beast::Journal journal,
        beast::insight::Collector::ptr const& collector =
            beast::insight::NullCollector::New())
        : FullBelowCacheImpl(
              std::move(name),
              targetSize,
              expiration,
              clock,
              journal,
              collector,
              std::make_index_sequence<64>{})
    {
    }

    /** Returns the total number cached items. */
    std::size_t
    size() const
    {
        return size_.load();
    }

    /** Returns the name of the FullBelowCacheImpl instance. */
    std::string const&
    name() const
    {
        return name_;
    }

    /** Refresh the last access time on a key if present.
        @return `true` If the key was found.
    */
    bool
    touch_if_exists(Key const& key)
    {
        auto& p = getPartition(key);

        {
            std::lock_guard lock(p.mutex);

            if (auto const it = p.items.find(key); it != p.items.end())
            {
                it->second = clock_.now();
                ++hits_;
                return true;
            }
        }

        ++misses_;
        return false;
    }

    bool
    insert(Key const& key)
    {
        auto& p = getPartition(key);

        std::lock_guard l(p.mutex);

        auto const now = clock_.now();

        auto [it, inserted] = p.items.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(key),
            std::forward_as_tuple(now));

        if (inserted)
        {
            size_.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        it->second = now;
        return false;
    }

    /** Erases all elements that match the predicate. */
    void
    sweep()
    {
        // Calculate the expiration time
        auto const expire = [this]() {
            using namespace std::chrono_literals;

            auto exp = targetAge_;

            // If the size of the cache exceeds the target size, then we adjust
            // the expiry timeout down to prune data faster.
            if (exp != std::chrono::seconds::zero() && targetSize_ != 0)
            {
                if (auto const size = size_.load(); size > targetSize_)
                    exp = exp * targetSize_ / size;
            }

            return std::max<std::chrono::seconds>(1s, exp);
        }();

        auto eraser = [this, expire](
                          Partition& partition,
                          clock_type::time_point const& now) {
            std::size_t decTotal = 0;

            std::lock_guard lock(partition.mutex);

            auto it = partition.items.begin();

            while (it != partition.items.end())
            {
                if (it->second + expire <= now)
                {
                    ++decTotal;
                    it = partition.items.erase(it);
                    continue;
                }

                // This item is fine.
                ++it;
            }

            if (decTotal)
                size_.fetch_sub(decTotal, std::memory_order_relaxed);
        };

        auto const start = clock_.now();

        // For systems with a small number of cores always use the single
        // threaded algorithm.
        if (std::thread::hardware_concurrency() <= 4)
        {
            for (auto& p : partitions_)
                eraser(p, start);
        }
        else
        {
            // We want to limit the number of threads to avoid resource
            // starvation.
            std::array<std::thread, 4> threads{};

            for (std::size_t i = 0; i != threads.size(); ++i)
            {
                threads[i] = std::thread(
                    [this, start, &eraser, step = threads.size()](
                        std::size_t j) {
                        while (j < partitions_.size())
                        {
                            eraser(partitions_[j], start);
                            j += step;
                        }
                    },
                    i);
            }

            for (auto& t : threads)
                t.join();
        }

        if (auto const d = clock_.now() - start; d >= std::chrono::seconds(2))
        {
            auto const secs =
                std::chrono::duration_cast<std::chrono::seconds>(d);
            auto const usecs =
                std::chrono::duration_cast<std::chrono::milliseconds>(d) -
                std::chrono::duration_cast<std::chrono::milliseconds>(secs);

            JLOG(journal_.info())
                << "sweep: Iteration over " << size_.load() << " items took "
                << secs.count() << "." << usecs.count() << " seconds.";
        }
    }

    Json::Value
    info() const
    {
        Json::Value v{Json::objectValue};

        v["name"] = name_;
        v["partitions"] = static_cast<std::uint32_t>(partitions_.size());
        v["total_size"] = std::to_string(size_.load());
        v["cache_hits"] = std::to_string(hits_.load());
        v["cache_misses"] = std::to_string(misses_.load());
        v["target_size"] = std::to_string(targetSize_);
        v["target_age"] = std::to_string(targetAge_.count());

        return v;
    }
};

}  // namespace detail

/** Remembers which tree keys have all descendants resident.
    This optimizes the process of acquiring a complete tree.
*/
class FullBelowCache
{
    using KeyCache = detail::FullBelowCacheImpl<uint256>;

public:
    using key_type = KeyCache::key_type;
    using clock_type = KeyCache::clock_type;

    /** Construct the cache.

        @param name A label for diagnostics and stats reporting.
        @param collector The collector to use for reporting stats.
        @param targetSize The cache target size.
        @param targetExpirationSeconds The expiration time for items.
    */
    FullBelowCache(
        std::string const& name,
        clock_type& clock,
        beast::Journal j,
        std::size_t targetSize,
        std::chrono::seconds expiration = std::chrono::minutes{2},
        beast::insight::Collector::ptr const& collector =
            beast::insight::NullCollector::New())
        : name_(name)
        , clock_(clock)
        , journal_(j)
        , collector_(collector)
        , targetSize_(targetSize)
        , expiration_(expiration)
    {
        clear();
    }

    /** Return the number of elements in the cache.
        Thread safety:
            Safe to call from any thread.
    */
    std::size_t
    size() const
    {
        if (auto c = cache_.load())
            return c->size();

        return 0;
    }

    /** Remove expired cache items.

        Thread safety:
            Safe to call from any thread.
    */
    void
    sweep()
    {
        if (auto c = cache_.load())
            c->sweep();
    }

    /** Refresh the last access time of an item, if it exists.

        Thread safety:
            Safe to call from any thread.

        @param key The key to refresh.
        @return `true` If the key exists.
    */
    bool
    touch_if_exists(key_type const& key)
    {
        if (auto c = cache_.load())
            return c->touch_if_exists(key);

        return false;
    }

    /** Insert a key into the cache.
        If the key already exists, the last access time will still
        be refreshed.
        Thread safety:
            Safe to call from any thread.
        @param key The key to insert.
    */
    void
    insert(key_type const& key)
    {
        if (auto c = cache_.load())
            c->insert(key);
    }

    /** Returns the generation that can determine if a cached entry is valid. */
    std::uint32_t
    generation() const
    {
        return generation_;
    }

    /** Clears the cache.

        This effectively replaces the cache with an entirely new instance, and
        destroys the old instance.

        It is safe to call this method from any thread.
     */
    void
    clear()
    {
        auto c = cache_.load();

        ++generation_;

        cache_.store(std::make_shared<KeyCache>(
            "FullBelow: " + name_,
            targetSize_,
            expiration_,
            clock_,
            journal_,
            collector_));

        if (c)
        {
            std::thread t(
                [](std::shared_ptr<KeyCache> cache) {
                    // just invoke the destructor of cache
                },
                std::move(c));
            t.detach();
        }
    }

    Json::Value
    info() const
    {
        if (auto c = cache_.load())
        {
            auto ret = c->info();
            ret["generation"] = std::to_string(generation_);
            return ret;
        }

        return {Json::objectValue};
    }

private:
    std::string const name_;
    clock_type& clock_;
    beast::Journal journal_;
    beast::insight::Collector::ptr collector_;
    std::size_t const targetSize_;
    std::chrono::seconds const expiration_;

    std::atomic<std::uint32_t> generation_ = 0;
    std::atomic<std::shared_ptr<KeyCache>> cache_;
};

}  // namespace ripple

#endif
