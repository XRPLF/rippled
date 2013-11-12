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
