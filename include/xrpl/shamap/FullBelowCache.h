#pragma once

#include <xrpl/basics/KeyCache.h>
#include <xrpl/basics/TaggedCache.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/utility/Journal.h>

#include <atomic>
#include <string>

namespace xrpl {

namespace detail {

/** Remembers which tree keys have all descendants resident.
    This optimizes the process of acquiring a complete tree.
*/
class BasicFullBelowCache
{
private:
    using CacheType = KeyCache;

public:
    // Need to be named before converting
    // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
    enum { DefaultCacheTargetSize = 0 };

    using key_type = uint256;
    using clock_type = typename CacheType::clock_type;

    /** Construct the cache.

        @param name A label for diagnostics and stats reporting.
        @param collector The collector to use for reporting stats.
        @param targetSize The cache target size.
        @param targetExpirationSeconds The expiration time for items.
    */
    BasicFullBelowCache(
        std::string const& name,
        clock_type& clock,
        beast::Journal j,
        beast::insight::Collector::ptr const& collector = beast::insight::NullCollector::New(),
        std::size_t targetSize = DefaultCacheTargetSize,
        std::chrono::seconds expiration = std::chrono::minutes{2})
        : m_cache_(name, targetSize, expiration, clock, j, collector), m_gen_(1)
    {
    }

    /** Return the clock associated with the cache. */
    clock_type&
    clock()
    {
        return m_cache_.clock();
    }

    /** Return the number of elements in the cache.
        Thread safety:
            Safe to call from any thread.
    */
    std::size_t
    size() const
    {
        return m_cache_.size();
    }

    /** Remove expired cache items.
        Thread safety:
            Safe to call from any thread.
    */
    void
    sweep()
    {
        m_cache_.sweep();
    }

    /** Refresh the last access time of an item, if it exists.
        Thread safety:
            Safe to call from any thread.
        @param key The key to refresh.
        @return `true` If the key exists.
    */
    bool
    touchIfExists(key_type const& key)
    {
        return m_cache_.touchIfExists(key);
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
        m_cache_.insert(key);
    }

    /** generation determines whether cached entry is valid */
    std::uint32_t
    getGeneration(void) const
    {
        return m_gen_;
    }

    void
    clear()
    {
        m_cache_.clear();
        ++m_gen_;
    }

    void
    reset()
    {
        m_cache_.clear();
        m_gen_ = 1;
    }

private:
    CacheType m_cache_;
    std::atomic<std::uint32_t> m_gen_;
};

}  // namespace detail

using FullBelowCache = detail::BasicFullBelowCache;

}  // namespace xrpl
