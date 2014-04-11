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

#ifndef RIPPLE_TESTOVERLAY_NETWORKTYPE_H_INCLUDED
#define RIPPLE_TESTOVERLAY_NETWORKTYPE_H_INCLUDED

namespace TestOverlay {

template <class ConfigParam>
class NetworkType : public ConfigParam
{
public:
    typedef ConfigParam Config;

    typedef typename Config::Peer       Peer;
    typedef typename Config::State      State;
    typedef typename Config::SizeType   SizeType;

    typedef std::vector <std::unique_ptr <Peer> > Peers;

    NetworkType ()
        : m_steps (0)
    {
        typename Config::InitPolicy () (*this);
    }

    /** Return the number of steps taken in the simulation. */
    SizeType steps () const
    {
        return m_steps;
    }

    /** Return the size of the network measured in peers. */
    SizeType size () const
    {
        return m_peers.size ();
    }

    /** Retrieve the state information associated with the Config. */
    State& state ()
    {
        return m_state;
    }

    /** Create new Peer. */
    Peer& createPeer ()
    {
        Peer* peer (new Peer (*this));
        m_peers.push_back (std::unique_ptr <Peer> (peer));
        return *peer;
    }
    
    /** Retrieve the container holding the set of peers. */
    Peers& peers ()
    {
        return m_peers;
    }

    /** Run the network for 1 iteration. */
    Results step ()
    {
        Results results;
        for (typename Peers::iterator iter = m_peers.begin ();
            iter!= m_peers.end (); ++iter)
            (*iter)->pre_step ();
        for (typename Peers::iterator iter = m_peers.begin ();
            iter!= m_peers.end (); ++iter)
            (*iter)->step ();
        ++results.steps;
        ++m_steps;
        for (typename Peers::iterator iter = m_peers.begin ();
            iter!= m_peers.end (); ++iter)
        {
            Peer& peer (**iter);
            peer.post_step ();
            results = results + peer.results();
            peer.results() = Results();
        }
        return results;
    }

    /** Run the network until a condition is met.
        Requirements:
            p (*this) is well-formed and returns bool.
    */
    template <class Predicate>
    Results step_until (Predicate p)
    {
        Results results;
        while (! p (*this))
            results += step ();
        return results;
    }

    //--------------------------------------------------------------------------

    /** A UnaryPredicate that returns true after # steps have passed. */
    class Steps
    {
    public:
        explicit Steps (SizeType steps)
            : m_steps (steps)
        {
        }

        bool operator() (NetworkType const&)
        {
            if (m_steps == 0)
                return true;
            --m_steps;
            return false;
        }
    
    private:
        SizeType m_steps;
    };

private:
    State m_state;
    SizeType m_steps;
    Peers m_peers;
};

//------------------------------------------------------------------------------

}

#endif
