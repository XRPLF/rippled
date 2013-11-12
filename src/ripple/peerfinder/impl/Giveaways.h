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

/** The Giveaways holds a vector of HopVectors, one of each hop.
*/
class Giveaways
{
    std::vector <GiveawaysAtHop> m_hopVector;
    bool m_shuffled;

public:
    typedef std::vector <GiveawaysAtHop>::iterator iterator;
    typedef std::vector <GiveawaysAtHop>::reverse_iterator reverse_iterator;

    Giveaways()
        : m_hopVector(maxPeerHopCount)
        , m_shuffled(false)
    {

    }

    // Add the endpoint to the appropriate hop vector.
    void add (CachedEndpoint &endpoint)
    {
        if (endpoint.message.hops < maxPeerHopCount)
            m_hopVector[endpoint.message.hops].add(endpoint);
    }

    // Resets the Giveaways, preparing to allow a new peer to iterate over it.
    void reset ()
    {
        for (size_t i = 0; i != m_hopVector.size(); i++)
        {
            if (!m_shuffled)
                m_hopVector[i].shuffle ();

            m_hopVector[i].reset ();
        }

        // Once this has been called, the hop vectors have all been shuffled
        // and we do not need to shuffle them again for the lifetime of this
        // instance.
        m_shuffled = true;
    }

    // Provides an iterator that starts from hop 0 and goes all the way to
    // the max hop.
    iterator begin ()
    {
        return m_hopVector.begin();
    }

    iterator end ()
    {
        return m_hopVector.end();
    }

    // Provides an iterator that starts from the max hop and goes all the way
    // down to hop 0.
    reverse_iterator rbegin ()
    {
        return m_hopVector.rbegin();
    }

    reverse_iterator rend ()
    {
        return m_hopVector.rend();
    }
};

}
}

#endif
