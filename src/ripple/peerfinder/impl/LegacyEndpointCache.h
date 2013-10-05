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

#ifndef RIPPLE_PEERFINDER_LEGACYENDPOINTCACHE_H_INCLUDED
#define RIPPLE_PEERFINDER_LEGACYENDPOINTCACHE_H_INCLUDED

namespace ripple {
namespace PeerFinder {

/** A container for managing the cache of legacy endpoints. */
class LegacyEndpointCache
{
private:
    typedef boost::multi_index_container <
        LegacyEndpoint, boost::multi_index::indexed_by <
            boost::multi_index::hashed_unique <
                BOOST_MULTI_INDEX_MEMBER(PeerFinder::LegacyEndpoint,IPEndpoint,address),
                IPEndpoint::hasher>
        >
    > MapType;

    MapType m_map;

public:
    typedef std::vector <LegacyEndpoint const*> FlattenedList;

    LegacyEndpointCache ()
    {
    }

    ~LegacyEndpointCache ()
    {
    }

    /** Attempt to insert the endpoint.
        The caller is responsible for making sure the address is valid.
        The return value provides a reference to the new or existing endpoint.
        The bool indicates whether or not the insertion took place.
    */
    std::pair <LegacyEndpoint&, bool> insert (IPEndpoint const& address)
    {
        std::pair <MapType::iterator, bool> result (
            m_map.insert (LegacyEndpoint (address)));
        return std::make_pair (*result.first, result.second);
    }

    /** Returns a pointer to the legacy endpoint or nullptr. */
    LegacyEndpoint const* find (IPEndpoint const& address)
    {
        MapType::iterator iter (m_map.find (address));
        if (iter != m_map.end())
            return &*iter;
        return nullptr;
    }

    /** Updates the metadata following a connection attempt.
        @param canAccept A flag indicating if the connection succeeded.
    */
    void checked (IPEndpoint const& address, bool canAccept)
    {
        LegacyEndpoint const* endpoint (find (address));
        if (endpoint != nullptr)
        {
            endpoint->checked = true;
            endpoint->canAccept = canAccept;
        }
    }

    struct Compare
    {
        bool operator() (LegacyEndpoint const* lhs,
                         LegacyEndpoint const* rhs) const
        {
            return lhs->lastGet < rhs->lastGet;
        }
    };

    /** Appends up to n addresses for establishing outbound peers. */
    void get (std::size_t n, std::vector <IPEndpoint>& result) const
    {
        FlattenedList list (flatten());
        std::random_shuffle (list.begin(), list.end());
        std::sort (list.begin(), list.end(), Compare());
        n = std::min (n, list.size());
        RelativeTime const now (RelativeTime::fromStartup());
        for (FlattenedList::iterator iter (list.begin());
            n-- && iter!=list.end(); ++iter)
        {
            result.push_back ((*iter)->address);
            (*iter)->lastGet = now;
        }   
    }

    /** Returns a flattened array of pointers to the legacy endpoints. */
    FlattenedList flatten () const
    {
        FlattenedList list;
        list.reserve (m_map.size());
        for (MapType::iterator iter (m_map.begin());
            iter != m_map.end(); ++iter)
            list.push_back (&*iter);
        return list;
    }
};

}
}

#endif
