//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_CORE_TEST_NETWORKTYPE_H_INCLUDED
#define RIPPLE_CORE_TEST_NETWORKTYPE_H_INCLUDED

namespace TestOverlay
{

template <class ConfigParam>
class NetworkType : public ConfigParam
{
public:
    typedef ConfigParam Config;

    using typename Config::SizeType;
    using typename Config::State;
    using typename Config::Peer;

    typedef std::vector <ScopedPointer <Peer> > Peers;

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
        m_peers.push_back (peer);
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

}

#endif
