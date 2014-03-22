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

#ifndef RIPPLE_TESTOVERLAY_PEERTYPE_H_INCLUDED
#define RIPPLE_TESTOVERLAY_PEERTYPE_H_INCLUDED

#include <cassert>

namespace TestOverlay
{

/** A peer in the overlay network. */
template <class Config>
class PeerType
    : public Config
    , public beast::Uncopyable
{
public:
    typedef typename Config::Peer       Peer;
    typedef typename Config::Payload    Payload;
    typedef typename Config::PeerLogic  PeerLogic;
    typedef typename Config::Message    Message;
    typedef typename Config::Network    Network;
    typedef typename Config::State      State;
    typedef typename State::UniqueID    UniqueID;
    typedef ConnectionType <Config>     Connection;
    typedef std::vector <Connection>    Connections;
    
    typedef boost::unordered_set <UniqueID> MessageTable;

    explicit PeerType (Network& network)
        : m_network (network)
        , m_id (network.state().nextPeerID())
        , m_logic (*this)
    {
    }

    /** Return the pending Results data associated with this peer. */
    /** @{ */
    Results& results ()
    {
        return m_results;
    }

    Results const& results () const
    {
        return m_results;
    }
    /** @} */

    /** Return the unique ID associated with this peer. */
    UniqueID id () const
    {
        return m_id;
    }

    /** Return the network this peer belongs to. */
    /** @{ */
    Network& network ()
    {
        return m_network;
    }

    Network const& network () const
    {
        return m_network;
    }
    /** @} */

    /** Return the container holding active connections. */
    /** @{ */
    Connections& connections ()
    {
        return m_connections;
    }

    Connections const& connections () const
    {
        return m_connections;
    }
    /** @} */

    /** Return the container holding the message ids seen by this peer. */
    /** @{ */
    MessageTable& msg_table ()
    {
        return m_msg_table;
    }

    MessageTable const& msg_table () const
    {
        return m_msg_table;
    }
    /** @} */

    /** Establish an outgoing connection to peer.
        @return `true` if the peer is not us and not connected already.
    */
    bool connect_to (Peer& peer)
    {
        if (&peer == this)
            return false;
        typename Connections::iterator const iter (std::find_if (
            connections().begin(), connections().end (),
                typename Connection::IsPeer (peer)));
        if (iter != connections().end())
            return false;
        assert (std::find_if (peer.connections().begin(),
            peer.connections().end(),
                typename Connection::IsPeer (*this))
                    == peer.connections().end ());
        connections().push_back (Connection (peer, false));
        peer.connections().push_back (Connection (*this, true));
        return true;
    }

    /** Disconnect from a peer.
        @return `true` if the peer was found and disconnected.
    */
    bool disconnect (Peer& peer)
    {
        if (&peer == this)
            return false;
        typename  Connections::iterator const iter1 (std::find_if (
            connections().begin(), connections().end (),
                typename Connection::IsPeer (peer)));
        if (iter1 == connections().end())
            return false;
        typename Connections::iterator const iter2 (std::find_if (
            peer.connections().begin(), peer.connections().end (),
                typename Connection::IsPeer (*this)));
        assert (iter2 != peer.connections().end());
        connections().erase (iter1);
        peer.connections().erase (iter2);
        return true;
    }

    //--------------------------------------------------------------------------

    /** Send a new message to a specific connection.
        A new message with an unused id is created with the given payload.
    */
    void send (Peer& peer, Payload const& payload)
    {
        Message const m (network().state().nextMessageID(), payload);
        assert (msg_table().insert (m.id()).second);
        assert (send_to (peer,
            Message (network().state().nextMessageID(),
                payload)));
    }

    /** Send a message to a specific connection.
        The message already has an id and associated payload.
    */
    bool send (Peer& peer, Message const& m)
    {
        return send_to (peer, m);
    }

    /** Send a new message to all connections.
        A new message with an unused id is created with the given payload.
    */
    void send_all (Payload const& payload)
    {
        Message const m (network().state().nextMessageID(), payload);
        assert (msg_table().insert (m.id()).second);
        assert (send_all_if (m,
            typename Connection::Any ()));
    };

    /** Send a message to all connections.
        The message already has an id and associated payload.
    */
    bool send_all (Message const& m)
    {
        return send_all_if (m,
            typename Connection::Any ());
    };

    /** Create a new message and send it to each connection that passes the predicate.
        Predicate is a UnaryPredicate that takes a Connection parameter.
        A new message with an unused id is created with the given payload.
    */
    template <class Predicate>
    void send_all_if (Payload const& payload, Predicate p)
    {
        Message const m (network().state().nextMessageID(), payload);
        assert (msg_table().insert (m.id()).second);
        assert (send_all_if (m, p));
    }

    /** Send an existing message to all connections that pass the predicate.
        @return `true` if at least one message was sent.
    */
    template <class Predicate>
    bool send_all_if (Message const& m, Predicate p)
    {
        bool sent = false;
        for (typename Connections::iterator iter (connections().begin());
            iter != connections().end(); ++iter)
            if (p (*iter))
                sent = send_to (iter->peer(), m) || sent;
        return sent;
    }

private:
    // Low level send function, everything goes through this.
    // Returns true if the message was sent.
    //
    bool send_to (Peer& peer, Message const& m)
    {
        // already seen it?
        if (peer.msg_table().count(m.id()) != 0)
        {
            ++results().dropped;
            return false;
        }
        typename Connections::iterator const iter (std::find_if (
            peer.connections().begin(), peer.connections().end (),
                typename Connection::IsPeer (*this)));
        assert (iter != peer.connections().end());
        assert (peer.msg_table().insert(m.id()).second);
        iter->pending().push_back (m);
        ++results().sent;
        return true;
    }

public:
    //--------------------------------------------------------------------------

    /** Called once on each Peer object before every iteration. */
    void pre_step ()
    {
        m_logic.pre_step ();
    }

    /** Called once on each Peer object during every iteration. */
    void step ()
    {
        // Call logic with current messages
        for (typename Connections::iterator iter (connections().begin());
            iter != connections().end(); ++iter)
        {
            Connection& c (*iter);
            for (typename Connection::Messages::iterator iter (
                c.messages().begin()); iter != c.messages().end(); ++iter)
            {
                Message const& m (*iter);
                assert (msg_table().count (m.id()) == 1);
                m_logic.receive (c, m);
                ++results().received;
            }
        }

        m_logic.step ();
    }

    /** Called once on each Peer object after every iteration. */
    void post_step ()
    {
        // Move pending messages to current messages
        for (typename Connections::iterator iter (connections().begin());
            iter != connections().end(); ++iter)
        {
            Connection& c (*iter);
            c.messages().clear ();
            c.messages().swap (c.pending());
        }

        m_logic.post_step ();
    }

private:
    Results m_results;
    Network& m_network;
    UniqueID const m_id;
    Connections m_connections;
    MessageTable m_msg_table;
    PeerLogic m_logic; // must come last
};

}

#endif
