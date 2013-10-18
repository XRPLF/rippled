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

#ifndef RIPPLE_PEERFINDER_CACHE_H_INCLUDED
#define RIPPLE_PEERFINDER_CACHE_H_INCLUDED

namespace ripple {
namespace PeerFinder {

/** The Endpoint cache holds the short-lived relayed Endpoint messages.
*/
class Cache
{
private:
    typedef boost::unordered_map <
        IPAddress, CachedEndpoint, IPAddress::hasher> Table;

    Journal m_journal;

    Table m_endpoints;

    // Tracks all the cached endpoints stored in the endpoint table
    // in oldest-to-newest order. The oldest item is at the head.
    List <CachedEndpoint> m_list;

public:
    explicit Cache (Journal journal)
        : m_journal (journal)
    {
    }

    ~Cache ()
    {
    }

    std::size_t size() const
    {
        return m_endpoints.size();
    }

    // Cycle the tables
    void cycle(DiscreteTime now)
    {
        List <CachedEndpoint>::iterator iter (m_list.begin());

        while (iter != m_list.end())
        {
            if (iter->whenExpires > now)
                break;

            CachedEndpoint &ep (*iter);

            // We need to remove the entry from the list before
            // we remove it from the table.
            iter = m_list.erase(iter);

            m_journal.debug << "Cache entry for " <<
                ep.message.address << " expired.";

            m_endpoints.erase (ep.message.address);
        }
    }

    // Insert or update an existing entry with the new message
    void insert (Endpoint const& message, DiscreteTime now)
    {
        std::pair <Table::iterator, bool> result (
            m_endpoints.emplace (message.address, CachedEndpoint(message, now)));

        if (!result.second)
        { // There was already an entry for this endpoint. Update it.
            CachedEndpoint& entry (result.first->second);

            entry.message.hops = std::min (entry.message.hops, message.hops);

            // Copy the other fields based on uptime
            if (entry.message.uptimeMinutes < message.uptimeMinutes)
            {
                entry.message.incomingSlotsAvailable    = message.incomingSlotsAvailable;
                entry.message.incomingSlotsMax          = message.incomingSlotsMax;
                entry.message.uptimeMinutes             = message.uptimeMinutes;
                entry.message.featureList               = message.featureList;
            }

            entry.whenExpires = now + cacheSecondsToLive;

            // It must already be in the list. Remove it in preparation.
            m_list.erase (m_list.iterator_to(entry));
        }

        CachedEndpoint& entry (result.first->second);

        m_journal.debug << "Cache entry for " << message.address <<
            " is valid until " << entry.whenExpires <<
            " (" << entry.message.incomingSlotsAvailable <<
            "/" << entry.message.incomingSlotsMax << ")";

        m_list.push_back (entry);
    }
};

}
}

#endif
