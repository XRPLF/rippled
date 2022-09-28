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
#include <ripple/basics/base_uint.h>
#include <ripple/basics/hardened_hash.h>
#include <ripple/beast/clock/abstract_clock.h>
#include <ripple/beast/insight/Collector.h>
#include <ripple/beast/insight/Insight.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/beast/utility/WrappedSink.h>
#include <ripple/json/json_value.h>
#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace ripple {

/** Cache that can be used

    Once items are added to the cache, they are kept alive (i.e. the cache keeps
    a shared_ptr to the item) until the items time out. At that point the cache
    relinquishes the shared_ptr, but continues to keep track of the items using
    a weak_ptr.

    The cache keeps objects alive in
    the map. The map allows multiple code paths that reference objects with the
    same key to get the same actual object.

    So long as data is in the cache, it will stay in memory. If it stays in
    memory even after it is ejected from the cache, the map will track it.

    @tparam Key the type used as the key for the cache
    @tparam Value the mapped type; may not be `void`.
    @tparam N the number of partitions to segment the keyspace in.
    @tparam Hash the hash function to use for this cache.
    @tparam KeyEqual the equality operator for keys.

    @note The number of partitions N defaults to 64. Evidence suggests that
          larger values do not have a measurable impact on performance, but
          smaller values may make sense in some cases.

    @note Callers must not modify data objects that are stored in the cache
          unless they hold their own lock over all cache operations.
*/
template <
    class Key,
    class Value,
    std::size_t N = 64,
    class Hash = hardened_hash<>,
    class KeyEqual = std::equal_to<Key>>
class TaggedCache
{
    static_assert(
        !std::is_same_v<Value, void>,
        "You may not use void as the value for TaggedCache");

    static_assert(
        N != 0 && N <= 256,
        "The number of partitions for TaggedCache must be between 1 and 256");

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

    struct Entry
    {
        clock_type::time_point last_access;
        std::variant<std::shared_ptr<Value>, std::weak_ptr<Value>> ptr;

        Entry(
            clock_type::time_point const& last_access_,
            std::shared_ptr<Value> const& ptr_)
            : last_access(last_access_), ptr(ptr_)
        {
        }

        void
        touch(clock_type::time_point const& now)
        {
            last_access = now;
        }

        /** Returns true if the entry holds a strong pointer. */
        bool
        pinned() const
        {
            return std::holds_alternative<std::shared_ptr<Value>>(ptr);
        }

        /** Returns true if the data behind this entry is still in memory. */
        bool
        valid() const
        {
            if (std::holds_alternative<std::weak_ptr<Value>>(ptr))
                return !std::get<std::weak_ptr<Value>>(ptr).expired();

            return static_cast<bool>(std::get<std::shared_ptr<Value>>(ptr));
        }

        bool
        unique() const
        {
            if (std::holds_alternative<std::shared_ptr<Value>>(ptr))
                return std::get<std::shared_ptr<Value>>(ptr).use_count() == 1;

            return false;
        }

        /** Returns a (possibly unseated) strong pointer to the data

            @note The item pinned state doesn't change; that is if the item
                  was previously unpinned (i.e. we had a weak pointer to it)
                  isn't "pinned" (i.e.
         */
        std::shared_ptr<Value>
        data()
        {
            if (std::holds_alternative<std::weak_ptr<Value>>(ptr))
                return std::get<std::weak_ptr<Value>>(ptr).lock();

            return std::get<std::shared_ptr<Value>>(ptr);
        }

        /** Returns a strong pointer to the cached data.

            @return A pair of a (possibly unseated) pointer to the data, along
                    with a boolean that indicates whether the data was unpinned
                    prior to this operation.
         * */
        std::pair<std::shared_ptr<Value>, bool>
        pin()
        {
            bool const weak = std::holds_alternative<std::weak_ptr<Value>>(ptr);

            if (weak)
            {
                auto sp = std::get<std::weak_ptr<Value>>(ptr).lock();

                if (!sp)
                    return {nullptr, weak};

                ptr = sp;
            }

            return {std::get<std::shared_ptr<Value>>(ptr), weak};
        }
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
        hardened_hash_map<Key, Entry, Hash, KeyEqual> items;

        Partition(std::size_t i) : index(i)
        {
        }
    };

    /** The partitions that, together, map the entire key space. */
    std::array<Partition, N> partitions_;

    /** Number of items where we have either a strong or weak pointer to. */
    std::atomic<std::size_t> totalSize_ = 0;

    /** Number of items where that we have a strong pointer to.  */
    std::atomic<std::size_t> strongSize_ = 0;

    /** The number of times that we found an item in the cache */
    std::atomic<std::uint64_t> hits_ = 0;
    std::atomic<std::uint64_t> misses_ = 0;

    template <class K>
    Partition&
    getPartition(K const& key) noexcept
    {
        if constexpr (std::is_integral_v<K>)
            return partitions_[key % partitions_.size()];
        else if constexpr (std::is_same_v<
                               K,
                               base_uint<K::bytes * 8, typename K::tag_type>>)
            return partitions_[*key.data() % partitions_.size()];
        else
            return partitions_[hash_(key) % partitions_.size()];
    }

private:
    template <std::size_t... Is>
    TaggedCache(
        std::string name,
        std::size_t size,
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
                  m_stats.size.set(getCacheSize());
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
        , targetSize_(size)
        , targetAge_(expiration)
        , partitions_{(Is)...}
    {
    }

public:
    TaggedCache(
        std::string name,
        std::size_t size,
        std::chrono::seconds expiration,
        clock_type& clock,
        beast::Journal journal,
        beast::insight::Collector::ptr const& collector =
            beast::insight::NullCollector::New())
        : TaggedCache(
              std::move(name),
              size,
              expiration,
              clock,
              journal,
              collector,
              std::make_index_sequence<N>{})
    {
    }

    /** Returns the number of cached items that we hold strong pointers to. */
    std::size_t
    getCacheSize() const
    {
        return strongSize_.load();
    }

    /** Returns the total number cached items. */
    std::size_t
    getTrackSize() const
    {
        return totalSize_.load();
    }

    /** Returns the name of the TaggedCache instance. */
    std::string const&
    name() const
    {
        return name_;
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
    bool
    canonicalize(
        Key const& key,
        std::shared_ptr<Value>& data,
        std::function<bool(std::shared_ptr<Value> const&)>&& replace)
    {
        // Return canonical value, store if needed, refresh in cache
        // Return values: true=we had the data already
        auto& p = getPartition(key);

        std::lock_guard lock(p.mutex);

        auto cit = p.items.find(key);

        if (cit == p.items.end())
        {
            p.items.emplace(
                std::piecewise_construct,
                std::forward_as_tuple(key),
                std::forward_as_tuple(clock_.now(), data));
            strongSize_.fetch_add(1, std::memory_order_relaxed);
            totalSize_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        auto& entry = cit->second;

        entry.touch(clock_.now());

        // If the entry has valid data, check whether we want to replace it.
        if (auto curr = entry.pin(); curr.first)
        {
            if (replace(curr.first))
                entry.ptr = data;
            else
                data = curr.first;

            // If the entry was freshy pinned, track it. This works even if
            // we are replacing the entry: if the original was pinned, we
            // already counted it.
            if (curr.second)
                strongSize_.fetch_add(1, std::memory_order_relaxed);

            return true;
        }

        // The entry had an expired weak pointer; take the new data.
        entry.ptr = data;
        strongSize_.fetch_add(1, std::memory_order_relaxed);

        return false;
    }

    bool
    insert_or_assign(Key const& key, std::shared_ptr<Value> const& data)
    {
        return canonicalize(
            key,
            const_cast<std::shared_ptr<Value>&>(data),
            [](std::shared_ptr<Value> const&) { return true; });
    }

    bool
    retrieve_or_insert(Key const& key, std::shared_ptr<Value>& data)
    {
        return canonicalize(
            key, data, [](std::shared_ptr<Value> const&) { return false; });
    }

    [[nodiscard]] std::shared_ptr<Value>
    fetch(Key const& key, bool remove = false)
    {
        auto& p = getPartition(key);

        std::lock_guard l(p.mutex);

        if (auto cit = p.items.find(key); cit != p.items.end())
        {
            // Get a strong pointer to the object, if possible.
            auto ret = cit->second.pin();

            if (ret.first)
            {
                if (!remove)
                {
                    if (ret.second)
                        strongSize_.fetch_add(1, std::memory_order_relaxed);

                    cit->second.touch(clock_.now());
                }

                ++hits_;
            }

            if (!ret.first || remove)
            {
                // Track state count if the pointer was already pinned.
                if (cit->second.pinned())
                    strongSize_.fetch_sub(1, std::memory_order_relaxed);

                totalSize_.fetch_sub(1, std::memory_order_relaxed);
                ;
                p.items.erase(cit);
            }

            return ret.first;
        }

        misses_++;
        return {};
    }

    /** Insert the element into the container.
        If the key already exists, nothing happens.
        @return `true` If the element was inserted
    */
    bool
    insert(Key const& key, Value const& value)
    {
        auto p = std::make_shared<Value>(std::cref(value));
        return retrieve_or_insert(key, p);
    }

    /** Erases all elements that match the predicate.

        @param pred the predicate that decides whether to delete an item.

        @note The predicate function can inspect and modify the state of the
              internal object (e.g. by pinning or unpinning it).
     * */
    /** @{ */
    template <class Pred>
    void
    erase_if_impl(Pred pred, std::string op = {})
    {
        auto eraser =
            [op, this, now = clock_.now(), &pred](Partition& partition) {
                // Used to delete items outside the lock, if possible.
                std::vector<std::shared_ptr<Value>> destroy;
                destroy.reserve(1000);

                std::size_t decTotal = 0;
                std::size_t decStrong = 0;
                std::size_t incStrong = 0;

                std::lock_guard lock(partition.mutex);

                auto it = partition.items.begin();

                while (it != partition.items.end())
                {
                    // Opportunistically eliminate any items that
                    // reference data that is no longer in memory.
                    if (!it->second.valid())
                    {
                        ++decTotal;
                        it = partition.items.erase(it);
                        continue;
                    }

                    // The predicate could pin or unpin an entry and that's
                    // something we need to track.
                    bool const wasPinned = it->second.pinned();

                    if (pred(it))
                    {
                        if (it->second.pinned())
                        {
                            destroy.emplace_back(it->second.data());

                            if (destroy.size() == 1000)
                            {
                                decltype(destroy) background_destroy;
                                background_destroy.reserve(1000);

                                std::swap(destroy, background_destroy);

                                std::thread t(
                                    [](decltype(background_destroy) d) {
                                        // just invoke the destructor of d
                                    },
                                    std::move(background_destroy));
                                t.detach();
                            }

                            ++decStrong;
                        }

                        if (wasPinned && !it->second.pinned())
                            ++decStrong;
                        else if (!wasPinned && it->second.pinned())
                            ++incStrong;

                        ++decTotal;

                        it = partition.items.erase(it);
                        continue;
                    }

                    if (wasPinned && !it->second.pinned())
                        ++decStrong;
                    else if (!wasPinned && it->second.pinned())
                        ++incStrong;

                    // This item is fine.
                    ++it;
                }

                if (decTotal)
                    totalSize_.fetch_sub(decTotal, std::memory_order_relaxed);

                if (decStrong)
                    strongSize_.fetch_sub(decStrong, std::memory_order_relaxed);

                if (incStrong)
                    strongSize_.fetch_add(incStrong, std::memory_order_relaxed);
            };

        auto const start = clock_.now();

        // For systems with a small number of cores always use the single
        // threaded algorithm.
        if (N <= 4 || std::thread::hardware_concurrency() <= 4)
        {
            for (auto& p : partitions_)
                eraser(p);
        }
        else
        {
            // We want to limit the number of threads to avoid resource
            // starvation.
            std::array<std::thread, 4> threads{};

            for (std::size_t i = 0; i != threads.size(); ++i)
            {
                threads[i] = std::thread(
                    [this, &eraser, step = threads.size()](std::size_t j) {
                        while (j < partitions_.size())
                        {
                            eraser(partitions_[j]);
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
            if (!op.empty())
                op += ": ";

            auto const secs =
                std::chrono::duration_cast<std::chrono::seconds>(d);
            auto const usecs =
                std::chrono::duration_cast<std::chrono::milliseconds>(d) -
                std::chrono::duration_cast<std::chrono::milliseconds>(secs);

            JLOG(journal_.info())
                << op << "Iteration over " << totalSize_.load()
                << " items took " << secs.count() << "." << usecs.count()
                << " seconds.";
        }
    }

    template <
        class Pred,
        class V = Value,
        class = std::enable_if_t<std::is_invocable_r_v<bool, Pred, V&>>>
    void
    erase_if(Pred&& pred)
    {
        erase_if_impl([this, &pred](auto it) {
            if (auto d = it->second.data())
                return pred(*d);

            return false;
        });
    }

    template <
        class Pred,
        class = std::enable_if_t<std::is_invocable_r_v<bool, Pred, Entry&>>>
    void
    erase_if(Pred&& pred, std::string op = {})
    {
        erase_if_impl([&pred](auto& it) { return pred(it->second); }, op);
    }

    template <
        class Pred,
        class = std::enable_if_t<std::is_invocable_r_v<bool, Pred, Key const&>>>
    void
    erase_if(Pred pred, std::string op = {})
    {
        erase_if_impl([&pred](auto it) { return pred(it->first); }, op);
    }
    /** @} */

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
                if (auto const size = totalSize_.load(); size > targetSize_)
                    exp = exp * targetSize_ / size;
            }

            return std::max<std::chrono::seconds>(1s, exp);
        }();

        erase_if(
            [this,
             expire,
             oversized = (totalSize_.load() > targetSize_),
             now = clock_.now()](Entry& e) {
                // If the item wasn't pinned and the cache is oversized,
                // unconditionally remove it.
                if (!e.pinned() && oversized)
                    return true;

                if (targetAge_ != targetAge_.zero() &&
                    e.last_access + expire <= now)
                {
                    // If the cache holds the only strong pointer to this
                    // item, unconditionally remove it.
                    if (e.unique())
                        return true;

                    // Otherwise, relinquish our strong pointer. If the
                    // cache wasn't oversized when we started sweeping we
                    // will keep the item and (presumably) get rid of it
                    // the next time we sweep.
                    if (e.pinned())
                        e.ptr = std::weak_ptr<Value>(
                            std::get<std::shared_ptr<Value>>(e.ptr));

                    return oversized || !e.valid();
                }

                return false;
            },
            "sweep");
    }

    Json::Value
    info() const
    {
        Json::Value v{Json::objectValue};

        v["name"] = name_;
        v["partitions"] = static_cast<std::uint32_t>(partitions_.size());
        v["total_size"] = std::to_string(totalSize_.load());
        v["cache_size"] = std::to_string(strongSize_.load());
        v["cache_hits"] = std::to_string(hits_.load());
        v["cache_misses"] = std::to_string(misses_.load());
        v["target_size"] = std::to_string(targetSize_);
        v["target_age"] = std::to_string(targetAge_.count());

        return v;
    }
};

}  // namespace ripple

#endif
