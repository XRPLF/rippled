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

#ifndef RIPPLE_PEERFINDER_PEER_H_INCLUDED
#define RIPPLE_PEERFINDER_PEER_H_INCLUDED

namespace ripple {
namespace PeerFinder {

/** Metadata for an open peer socket. */
class Peer
{
public:
    enum State
    {
        /** Accepted inbound connection, no handshake. */
        stateAccept,

        /** Outbound connection attempt. */
        stateConnect,

        /** Outbound connection, no handshake. */
        stateConnected,

        /** Active peer (handshake completed). */
        stateActive,

        /** Graceful close in progress. */
        stateClosing
    };

    Peer (IPAddress const& remote_address, bool inbound, bool fixed)
        : m_inbound (inbound)
        , m_remote_address (remote_address)
        , m_state (inbound ? stateAccept : stateConnect)
        , m_fixed (fixed)
        , m_cluster (false)
        , checked (inbound ? false : true)
        , canAccept (inbound ? false : true)
        , connectivityCheckInProgress (false)
    {
    }

    /** Returns the local address on the socket if known. */
    IPAddress const& local_address () const
    {
        return m_local_address;
    }

    /** Sets the local address on the socket. */
    void local_address (IPAddress const& address)
    {
        consistency_check (is_unspecified (m_local_address));
        m_local_address = address;
    }

    /** Returns the remote address on the socket. */
    IPAddress const& remote_address () const
    {
        return m_remote_address;
    }

    /** Returns `true` if this is an inbound connection. */
    bool inbound () const
    {
        return m_inbound;
    }

    /** Returns `true` if this is an outbound connection. */
    bool outbound () const
    {
        return ! m_inbound;
    }

    /** Marks a connection as belonging to a fixed peer. */
    void fixed (bool fix)
    {
        m_fixed = fix;
    }

    /** Marks `true` if this is a connection belonging to a fixed peer. */
    bool fixed () const
    {
        return m_fixed;
    }

    void cluster (bool cluster)
    {
        m_cluster = cluster;
    }
    
    bool cluster () const
    {
        return m_cluster;
    }
    
    State state() const
    {
        return m_state;
    }

    void state (State s)
    {
        m_state = s;
    }

    PeerID const& id () const
    {
        return m_id;
    }

    void activate (PeerID const& id, DiscreteTime now)
    {
        m_state = stateActive;
        m_id = id;

        whenSendEndpoints = now;
        whenAcceptEndpoints = now;
    }

private:
    // `true` if the connection is incoming
    bool const m_inbound;

    // The local address on the socket, when it is known.
    IPAddress m_local_address;

    // The remote address on the socket.
    IPAddress m_remote_address;

    // Current state of this connection
    State m_state;

    // The public key. Valid after a handshake.
    PeerID m_id;

    // Set to indicate that this is a fixed peer.
    bool m_fixed;

    // Set to indicate that this is a peer that belongs in our cluster
    // and does not consume a slot. Valid after a handshake.
    bool m_cluster;
    
    //--------------------------------------------------------------------------

public:
    // DEPRECATED public data members

    // Tells us if we checked the connection. Outbound connections
    // are always considered checked since we successfuly connected.
    bool checked;

    // Set to indicate if the connection can receive incoming at the
    // address advertised in mtENDPOINTS. Only valid if checked is true.
    bool canAccept;

    // Set to indicate that a connection check for this peer is in
    // progress. Valid always.
    bool connectivityCheckInProgress;
    
    // The time after which we will send the peer mtENDPOINTS
    DiscreteTime whenSendEndpoints;

    // The time after which we will accept mtENDPOINTS from the peer
    // This is to prevent flooding or spamming. Receipt of mtENDPOINTS
    // sooner than the allotted time should impose a load charge.
    //
    DiscreteTime whenAcceptEndpoints;

    // The set of all recent IPAddress that we have seen from this peer.
    // We try to avoid sending a peer the same addresses they gave us.
    //
    //std::set <IPAddress> received;
};

}
}

#endif
