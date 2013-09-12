//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_TESTOVERLAY_PEERLOGICBASE_H_INCLUDED
#define RIPPLE_TESTOVERLAY_PEERLOGICBASE_H_INCLUDED

namespace TestOverlay
{

/** Base class for all PeerLogic implementations.
    This provides stubs for all necessary functions, although
    they don't actually do anything.
*/
template <class Config>
class PeerLogicBase : public Config
{
public:
    typedef typename Config::Peer           Peer; 
    typedef typename Peer::Connection       Connection;
    typedef typename Connection::Message    Message;

    explicit PeerLogicBase (Peer& peer)
        : m_peer (peer)
    {
    }

    /** Return the Peer associated with this logic. */
    /** @{ */
    Peer& peer ()
    {
        return m_peer;
    }

    Peer const& peer () const
    {
        return m_peer;
    }
    /** @} */

    // Called to process a message
    void receive (Connection const& c, Message const& m)
    {
    }

    // Called before taking a step
    void pre_step ()
    {
    }

    // Called during a step
    void step ()
    {
    }

    // Called after a step is taken
    void post_step ()
    {
    }

private:
    Peer& m_peer;
};

}

#endif
