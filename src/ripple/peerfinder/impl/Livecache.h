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

#ifndef RIPPLE_PEERFINDER_LIVECACHE_H_INCLUDED
#define RIPPLE_PEERFINDER_LIVECACHE_H_INCLUDED

#include <ripple/basics/Log.h>
#include <ripple/basics/random.h>
#include <ripple/beast/container/aged_unordered_map.h>
#include <ripple/beast/utility/maybe_const.h>
#include <ripple/peerfinder/PeerfinderManager.h>
#include <ripple/peerfinder/impl/Tuning.h>
#include <ripple/peerfinder/impl/iosformat.h>
#include <boost/intrusive/list.hpp>
#include <boost/iterator/transform_iterator.hpp>

#include <algorithm>

namespace ripple {
namespace PeerFinder {

template <class>
class Livecache;

//------------------------------------------------------------------------------

/** The Livecache holds the short-lived relayed Endpoint messages.

    Since peers only advertise themselves when they have open slots,
    we want these messags to expire rather quickly after the peer becomes
    full.

    Addresses added to the cache are not connection-tested to see if
    they are connectible (with one small exception regarding neighbors).
    Therefore, these addresses are not suitable for persisting across
    launches or for bootstrapping, because they do not have verifiable
    and locally observed uptime and connectibility information.
*/
template <class Allocator = std::allocator<char>>
class Livecache
{
private:
    using cache_type = beast::aged_unordered_map<
        beast::IP::Endpoint,
        std::uint32_t,
        std::chrono::steady_clock,
        std::hash<beast::IP::Endpoint>,
        std::equal_to<beast::IP::Endpoint>,
        Allocator>;

    beast::Journal m_journal;
    cache_type m_cache;

public:
    using const_iterator = typename cache_type::const_iterator;
    using allocator_type = Allocator;

    /** Create the cache. */
    Livecache(
        clock_type& clock,
        beast::Journal journal,
        Allocator alloc = Allocator());

    /** Returns `true` if the cache is empty. */
    bool
    empty() const
    {
        return m_cache.empty();
    }

    /** Returns the number of entries in the cache. */
    typename cache_type::size_type
    size() const
    {
        return m_cache.size();
    }

    const_iterator
    cbegin()
    {
        return m_cache.cbegin();
    }

    const_iterator
    cend()
    {
        return m_cache.cend();
    }

    const_iterator
    find(beast::IP::Endpoint const& ep)
    {
        return m_cache.find(ep);
    }

    /** Erase entries whose time has expired. */
    void
    expire();

    /** Creates or updates an existing Element based on a new message. */
    void
    insert(beast::IP::Endpoint const& ep, std::uint32_t hops);

    /** Output statistics. */
    void
    onWrite(beast::PropertyStream::Map& map);
};

//------------------------------------------------------------------------------

template <class Allocator>
Livecache<Allocator>::Livecache(
    clock_type& clock,
    beast::Journal journal,
    Allocator alloc)
    : m_journal(journal), m_cache(clock, alloc)
{
}

template <class Allocator>
void
Livecache<Allocator>::expire()
{
    std::size_t n(0);
    typename cache_type::time_point const expired(
        m_cache.clock().now() - Tuning::liveCacheSecondsToLive);
    for (auto iter(m_cache.chronological.begin());
         iter != m_cache.chronological.end() && iter.when() <= expired;)
    {
        iter = m_cache.erase(iter);
        ++n;
    }
    if (n > 0)
    {
        JLOG(m_journal.debug()) << beast::leftw(18) << "Livecache expired " << n
                                << ((n > 1) ? " entries" : " entry");
    }
}

template <class Allocator>
void
Livecache<Allocator>::insert(beast::IP::Endpoint const& ep, std::uint32_t hops)
{
    auto result = m_cache.emplace(ep, hops);
    if (result.second)
    {
        JLOG(m_journal.debug())
            << beast::leftw(18) << "Livecache insert " << ep;
        return;
    }

    m_cache.touch(result.first);

    if (hops < result.first->second)
        m_cache[ep] = hops;

    JLOG(m_journal.trace()) << beast::leftw(18) << "Livecache refresh " << ep;
}

template <class Allocator>
void
Livecache<Allocator>::onWrite(beast::PropertyStream::Map& map)
{
    typename cache_type::time_point const expired(
        m_cache.clock().now() - Tuning::liveCacheSecondsToLive);
    map["size"] = size();
    beast::PropertyStream::Set set("entries", map);
    for (auto iter(m_cache.cbegin()); iter != m_cache.cend(); ++iter)
    {
        beast::PropertyStream::Map item(set);
        item["address"] = iter->first.to_string();
        std::stringstream ss;
        ss << (iter.when() - expired).count();
        item["expires"] = ss.str();
    }
}

}  // namespace PeerFinder
}  // namespace ripple

#endif
