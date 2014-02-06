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

#include <unordered_map>

namespace ripple {
namespace PeerFinder {

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
class Livecache
{
public:
    struct Entry;

    typedef List <Entry> EntryList;

    struct Entry : public EntryList::Node
    {
        Entry (Endpoint const& endpoint_,
            clock_type::time_point const& whenExpires_)
            : endpoint (endpoint_)
            , whenExpires (whenExpires_)
        {
        }

        Endpoint endpoint;
        clock_type::time_point whenExpires;
    };

    typedef std::set <Endpoint, LessEndpoints> SortedTable;
    typedef std::unordered_map <IPAddress, Entry> AddressTable;

    clock_type& m_clock;
    Journal m_journal;
    AddressTable m_byAddress;
    SortedTable m_bySorted;

    // Tracks all the cached endpoints stored in the endpoint table
    // in oldest-to-newest order. The oldest item is at the head.
    EntryList m_list;

public:
    /** Create the cache. */
    Livecache (
        clock_type& clock,
        Journal journal)
        : m_clock (clock)
        , m_journal (journal)
    {
    }

    /** Returns `true` if the cache is empty. */
    bool empty () const
    {
        return m_byAddress.empty ();
    }

    /** Returns the number of entries in the cache. */
    AddressTable::size_type size() const
    {
        return m_byAddress.size();
    }

    /** Erase entries whose time has expired. */
    void sweep ()
    {
        auto const now (m_clock.now ());
        AddressTable::size_type count (0);
        for (EntryList::iterator iter (m_list.begin());
            iter != m_list.end();)
        {
            // Short circuit the loop since the list is sorted
            if (iter->whenExpires > now)
                break;
            Entry& entry (*iter);
            if (m_journal.trace) m_journal.trace << leftw (18) <<
                "Livecache expired " << entry.endpoint.address;
            // Must erase from list before map
            iter = m_list.erase (iter);
            meets_postcondition (m_bySorted.erase (
                entry.endpoint) == 1);
            meets_postcondition (m_byAddress.erase (
                entry.endpoint.address) == 1);
            ++count;
        }

        if (count > 0)
        {
            if (m_journal.debug) m_journal.debug << leftw (18) <<
                "Livecache expired " << count <<
                ((count > 1) ? "entries" : "entry");
        }
    }

    /** Creates or updates an existing entry based on a new message. */
    void insert (Endpoint endpoint)
    {
        // Caller is responsible for validation
        check_precondition (endpoint.hops <= Tuning::maxHops);
        auto now (m_clock.now ());
        auto const whenExpires (now + Tuning::liveCacheSecondsToLive);
        std::pair <AddressTable::iterator, bool> result (
            m_byAddress.emplace (std::piecewise_construct,
                std::make_tuple (endpoint.address),
                    std::make_tuple (endpoint, whenExpires)));
        Entry& entry (result.first->second);
        // Drop duplicates at higher hops
        if (! result.second && (endpoint.hops > entry.endpoint.hops))
        {
            std::size_t const excess (
                endpoint.hops - entry.endpoint.hops);
            if (m_journal.trace) m_journal.trace << leftw(18) <<
                "Livecache drop " << endpoint.address <<
                " at hops +" << excess;
            return;
        }
        // Update metadata if the address already exists
        if (! result.second)
        {
            meets_postcondition (m_bySorted.erase (
                result.first->second.endpoint) == 1);
            if (endpoint.hops < entry.endpoint.hops)
            {
                if (m_journal.debug) m_journal.debug << leftw (18) <<
                    "Livecache update " << endpoint.address <<
                    " at hops " << endpoint.hops;
                entry.endpoint.hops = endpoint.hops;
            }
            else
            {
                if (m_journal.trace) m_journal.trace << leftw (18) <<
                    "Livecache refresh " << endpoint.address <<
                    " at hops " << endpoint.hops;
            }

            entry.endpoint.features = endpoint.features;
            entry.whenExpires = whenExpires;

            m_list.erase (m_list.iterator_to(entry));
        }
        else
        {
            if (m_journal.debug) m_journal.debug << leftw (18) <<
                "Livecache insert " << endpoint.address <<
                " at hops " << endpoint.hops;
        }
        meets_postcondition (m_bySorted.insert (entry.endpoint).second);
        m_list.push_back (entry);
    }

    /** Returns the full set of endpoints in a Giveaways class. */
    Giveaways giveaways()
    {
        Endpoints endpoints;
        endpoints.reserve (m_list.size());
        for (EntryList::const_iterator iter (m_list.cbegin());
            iter != m_list.cend(); ++iter)
        {
            endpoints.push_back (iter->endpoint);
            endpoints.back ().hops;
        }
        if (! endpoints.empty())
            return Giveaways (endpoints);
        return Giveaways (endpoints);
    }

    /** Returns an ordered list all entries with unique addresses. */
    Endpoints fetch_unique () const
    {
        Endpoints result;
        if (m_bySorted.empty ())
            return result;
        result.reserve (m_bySorted.size ());
        Endpoint const& front (*m_bySorted.begin());
        IP::Address prev (front.address.address());
        result.emplace_back (front);
        for (SortedTable::const_iterator iter (++m_bySorted.begin());
            iter != m_bySorted.end(); ++iter)
        {
            IP::Address const addr (iter->address.address());
            if (addr != prev)
            {
                result.emplace_back (*iter);
                ++result.back().hops;
                prev = addr;
            }
        }
        return result;
    }

    /** Produce diagnostic output. */
    void dump (Journal::ScopedStream& ss) const
    {
        ss << std::endl << std::endl <<
            "Livecache (size " << m_byAddress.size() << ")";
        for (AddressTable::const_iterator iter (m_byAddress.begin());
            iter != m_byAddress.end(); ++iter)
        {
            Entry const& entry (iter->second);
            ss << std::endl <<
                entry.endpoint.address << ", " <<
                entry.endpoint.hops << " hops";
        }
    }

    /** Returns a histogram of message counts by hops. */
    typedef boost::array <int, Tuning::maxHops + 1> Histogram;
    Histogram histogram () const
    {
        Histogram h;
        for (Histogram::iterator iter (h.begin());
            iter != h.end(); ++iter)
            *iter = 0;
        for (EntryList::const_iterator iter (m_list.begin());
            iter != m_list.end(); ++iter)
            ++h[iter->endpoint.hops];
        return h;
    }
};

}
}

#endif
