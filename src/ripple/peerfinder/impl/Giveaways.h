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

#ifndef RIPPLE_PEERFINDER_GIVEAWAYS_H_INCLUDED
#define RIPPLE_PEERFINDER_GIVEAWAYS_H_INCLUDED

namespace ripple {
namespace PeerFinder {

/** Holds a rotating set of endpoint messages to give away. */
class Giveaways
{
public:
    typedef std::vector <Endpoint const*> Bucket;
    typedef boost::array <Bucket, Tuning::maxHops + 1> Buckets;

    Endpoints m_endpoints;
    std::size_t m_remain;
    Buckets m_buckets;

    void prepare ()
    {
        for (Buckets::iterator iter (m_buckets.begin());
            iter != m_buckets.end(); ++iter)
            iter->reserve (m_endpoints.size ());
    }

public:
    bool is_consistent ()
    {
        // Make sure the counts add up
        std::size_t count (0);
        for (Buckets::const_iterator iter (m_buckets.begin());
            iter != m_buckets.end(); ++iter)
            count += iter->size();
        return count == m_remain;
    }

    void refill ()
    {
        // Empty out the buckets
        for (Buckets::iterator iter (m_buckets.begin());
            iter != m_buckets.end(); ++iter)
            iter->clear();
        // Put endpoints back into buckets
        for (Endpoints::const_iterator iter (m_endpoints.begin());
            iter != m_endpoints.end(); ++iter)
        {
            Endpoint const& ep (*iter);
            consistency_check (ep.hops <= Tuning::maxHops);
            m_buckets [ep.hops].push_back (&ep);
        }
        // Shuffle the buckets
        for (Buckets::iterator iter (m_buckets.begin());
            iter != m_buckets.end(); ++iter)
            std::random_shuffle (iter->begin(), iter->end());
        m_remain = m_endpoints.size();
        consistency_check (is_consistent ());
    }

public:
    explicit Giveaways (Endpoints const& endpoints)
        : m_endpoints (endpoints)
        , m_remain (0)
    {
        prepare();
    }

#if BEAST_COMPILER_SUPPORTS_MOVE_SEMANTICS
    Giveaways (Endpoints&& endpoints)
        : m_endpoints (endpoints)
        , m_remain (0)
    {
        prepare();
    }
#endif

    /** Append up to `n` Endpoint to the specified container.
        The entries added to the container will have hops incremented.
    */
    template <typename EndpointContainer>
    void append (Endpoints::size_type n, EndpointContainer& c)
    { 
        n = std::min (n, m_endpoints.size());
        c.reserve (c.size () + n);
        if (m_remain < n)
            refill ();
        for (cyclic_iterator <Buckets::iterator> iter (
            m_buckets.begin (), m_buckets.begin (), m_buckets.end()); n;)
        {
            Bucket& bucket (*iter++);
            if (! bucket.empty ())
            {
                c.emplace_back (*bucket.back ());
                bucket.pop_back ();
                ++c.back ().hops;
                --n;
                --m_remain;
            }
        }
        consistency_check (is_consistent ());
    }

    /** Retrieve a fresh set of endpoints, preferring high hops.
        The entries added to the container will have hops incremented.
    */
    template <typename EndpointContainer>
    void reverse_append (Endpoints::size_type n, EndpointContainer& c)
    { 
        n = std::min (n, m_endpoints.size());
        c.reserve (c.size () + n);
        if (m_remain < n)
            refill ();
        for (cyclic_iterator <Buckets::reverse_iterator> iter (
            m_buckets.rbegin (), m_buckets.rbegin (), m_buckets.rend()); n;)
        {
            Bucket& bucket (*iter++);
            if (! bucket.empty ())
            {
                c.emplace_back (*bucket.back ());
                bucket.pop_back ();
                ++c.back ().hops;
                --n;
                --m_remain;
            }
        }
        consistency_check (is_consistent ());
    }
};

}
}

#endif
