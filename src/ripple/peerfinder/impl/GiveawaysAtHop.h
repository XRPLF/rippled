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

#ifndef RIPPLE_PEERFINDER_GIVEAWAYSATHOP_H_INCLUDED
#define RIPPLE_PEERFINDER_GIVEAWAYSATHOP_H_INCLUDED

namespace ripple {
namespace PeerFinder {

/** A GiveawaysAtHop contains a list of all the endpoints that are a particular
    number of hops away from us.
*/
class GiveawaysAtHop
{
public:
    typedef std::vector<CachedEndpoint*>::iterator iterator;

private:
    // List of endpoints that haven't been seen during this iteration
    std::vector <CachedEndpoint*> m_list;

    // List of endpoints that have been used during this iteration
    std::vector <CachedEndpoint*> m_used;

    // This iterator tracks where we are in the list between calls. It is
    // set to the beginning of the list by calling reset().
    iterator m_position;

public:
    // This function adds a new endpoint to the list of endpoints
    // that we will be returning.
    void add (CachedEndpoint &endpoint)
    {
        if (endpoint.message.hops < maxPeerHopCount)
        {
            if (endpoint.color)
                m_list.push_back(&endpoint);
            else
                m_used.push_back(&endpoint);
        }
    }

    // Shuffles the list of peers we are about to hand out.
    void shuffle ()
    {
        std::random_shuffle (m_list.begin (), m_list.end ());
    }

    // Prepare to begin iterating over the entire set of peers again.
    void reset ()
    {
        // We need to add any entries from the stale vector in the tail
        // end of the fresh vector. We do not need to shuffle them.
        if (!m_used.empty())
        {
            m_list.insert(m_list.end (), m_used.begin (), m_used.end ());
            m_used.clear();
        }

        // We need to start from the beginning again.
        m_position = m_list.begin();
    }

    // This is somewhat counterintuitive, but it doesn't really "begin"
    // iteration, but allows us to resume it.
    iterator begin ()
    {
        return m_position;
    }

    // The iterator to the last fresh endpoint we have available. Once we get
    // to this point, we have provided this peer with all endpoints in our list.
    iterator end ()
    {
        return m_list.end();
    }

    // Removes the specified item from the "fresh" list of endpoints and returns
    // an iterator to the next one to use. This means that the peer decided
    // to use this iterator.
    iterator erase (iterator iter)
    {
        // NIKB FIXME change node color, if it's going from fresh to stale

        m_used.push_back(*iter);

        return m_list.erase(iter);
    }

    // Reserves entries to allow inserts to be efficient.
    void reserve (size_t n)
    {
        m_used.reserve (n);
        m_list.reserve (n);
    }
};


}
}

#endif
