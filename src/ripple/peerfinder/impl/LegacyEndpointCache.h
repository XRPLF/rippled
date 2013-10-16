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
public:
    typedef std::vector <LegacyEndpoint const*> FlattenedList;

private:
    typedef boost::multi_index_container <
        LegacyEndpoint, boost::multi_index::indexed_by <
            boost::multi_index::hashed_unique <
                BOOST_MULTI_INDEX_MEMBER(PeerFinder::LegacyEndpoint,IPEndpoint,address),
                IPEndpoint::hasher>
        >
    > MapType;

    MapType m_map;
    Store& m_store;
    Journal m_journal;
    int m_mutationCount;

    //--------------------------------------------------------------------------

    /** Updates the database with the cache contents. */
    void update ()
    {
        FlattenedList list (flatten());
        m_store.updateLegacyEndpoints (list);
        m_journal.debug << "Updated " << list.size() << " legacy endpoints";
    }

    /** Increments the mutation count and updates the database if needed. */
    void mutate ()
    {
        // This flag keeps us from updating while we are loading
        if (m_mutationCount == -1)
            return;
        
        if (++m_mutationCount >= legacyEndpointMutationsPerUpdate)
        {
            update();
            m_mutationCount = 0;
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

    /** Prune comparison function, strict weak ordering on desirability. */
    struct PruneLess
    {
        static int checkedScore (LegacyEndpoint const& ep)
        {
            return ep.checked ? (ep.canAccept ? 2 : 1) : 0;
        }

        bool operator() (LegacyEndpoint const* lhs,
                         LegacyEndpoint const* rhs) const
        {
            // prefer checked and canAccept
            int const checkedCompare (
                checkedScore (*lhs) - checkedScore (*rhs));
            if (checkedCompare > 0)
                return true;
            else if (checkedScore < 0)
                return false;

            // prefer newer entries
            if (lhs->whenInserted > rhs->whenInserted)
                return true;
            else if (lhs->whenInserted < rhs->whenInserted)
                return false;

            return false;
        }
    };

    /** Sort endpoints by desirability and discard the bottom half. */
    void prune()
    {
        FlattenedList list (flatten());
        if (list.size () < 3)
            return;
        std::random_shuffle (list.begin(), list.end());
        std::sort (list.begin(), list.end(), PruneLess());
        FlattenedList::const_iterator pos (list.begin() + list.size()/2 + 1);
        std::size_t const n (m_map.size() - (pos - list.begin()));
        MapType map;
        for (FlattenedList::const_iterator iter (list.begin());
            iter != pos; ++iter)
            map.insert (**iter);
        std::swap (map, m_map);
        m_journal.info << "Pruned " << n << " legacy endpoints";
        mutate();
    }

    /** Get comparison function. */
    struct GetLess
    {
        bool operator() (LegacyEndpoint const* lhs,
                         LegacyEndpoint const* rhs) const
        {
            // Always prefer entries we tried longer ago. This should
            // cycle through the entire cache before re-using an address
            // for making a connection attempt.
            //
            if (lhs->lastGet < rhs->lastGet)
                return true;
            else if (lhs->lastGet > rhs->lastGet)
                return false;

            // Fall back to the prune desirability comparison
            return PruneLess() (lhs, rhs);
        }
    };

public:
    LegacyEndpointCache (Store& store, Journal journal)
        : m_store (store)
        , m_journal (journal)
        , m_mutationCount (-1)
    {
    }

    ~LegacyEndpointCache ()
    {
    }

    std::size_t size() const
    {
        return m_map.size();
    }

    /** Load the legacy endpoints cache from the database. */
    void load (DiscreteTime now)
    {
        typedef std::vector <IPEndpoint> List;
        List list;
        m_store.loadLegacyEndpoints (list);
        std::size_t n (0);
        for (List::const_iterator iter (list.begin());
            iter != list.end(); ++iter)
        {
            std::pair <LegacyEndpoint const&, bool> result (insert (*iter, now));
            if (result.second)
                ++n;
        }
        m_journal.debug << "Loaded " << n << " legacy endpoints";
        m_mutationCount = 0;
    }

    /** Attempt to insert the endpoint.
        The caller is responsible for making sure the address is valid.
        The return value provides a reference to the new or existing endpoint.
        The bool indicates whether or not the insertion took place.
    */
    std::pair <LegacyEndpoint const&, bool> insert (IPEndpoint const& address, DiscreteTime now)
    {
        std::pair <MapType::iterator, bool> result (
            m_map.insert (LegacyEndpoint (address, now)));
        if (m_map.size() > legacyEndpointCacheSize)
            prune();
        if (result.second)
            mutate();
        return std::make_pair (*result.first, result.second);
    }

    /** Returns a pointer to the legacy endpoint if it exists, else nullptr. */
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
            mutate();
        }
    }

    /** Appends up to n addresses for establishing outbound peers.
        Also updates the lastGet field of the LegacyEndpoint so we will avoid
        re-using the address until we have tried all the others.
    */
    void get (std::size_t n, std::vector <IPEndpoint>& result, DiscreteTime now) const
    {
        FlattenedList list (flatten());
        std::random_shuffle (list.begin(), list.end());
        std::sort (list.begin(), list.end(), GetLess());
        n = std::min (n, list.size());
        for (FlattenedList::iterator iter (list.begin());
            n-- && iter!=list.end(); ++iter)
        {
            result.push_back ((*iter)->address);
            (*iter)->lastGet = now;
        }
    }
};

}
}

#endif
