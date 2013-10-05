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

#ifndef RIPPLE_PEERFINDER_ENDPOINTCACHE_H_INCLUDED
#define RIPPLE_PEERFINDER_ENDPOINTCACHE_H_INCLUDED

namespace ripple {
namespace PeerFinder {

/** The Endpoint cache holds the short-lived relayed Endpoint messages.
*/
class Cache
{
private:
    typedef boost::unordered_map <
        IPEndpoint, CachedEndpoint, IPEndpoint::hasher> Table;

    Journal m_journal;

    Table m_now;
    Table m_prev;

    // Refresh the existing entry with a new message
    void refresh (CachedEndpoint& entry, Endpoint const& message)
    {
        entry.message.hops = std::min (entry.message.hops, message.hops);

        // Copy the other fields based on uptime
        if (entry.message.uptimeMinutes < message.uptimeMinutes)
        {
            entry.message.incomingSlotsAvailable    = message.incomingSlotsAvailable;
            entry.message.incomingSlotsMax          = message.incomingSlotsMax;
            entry.message.uptimeMinutes             = message.uptimeMinutes;
            entry.message.featureList               = message.featureList;
        }
    }

public:
    explicit Cache (Journal journal)
        : m_journal (journal)
    {
    }

    ~Cache ()
    {
    }

    // Cycle the tables
    void cycle()
    {
        std::swap (m_now, m_prev);
        m_now.clear();
    }

    // Insert or update an existing entry with the new message
    void insert (Endpoint const& message)
    {
        Table::iterator iter (m_prev.find (message.address));
        if (iter != m_prev.end())
        {
        }
        else
        {
            std::pair <Table::iterator, bool> result (
                m_now.emplace (message.address, message));
            if (!result.second)
                refresh (result.first->second, message);
        }
    }
};

}
}

#endif
