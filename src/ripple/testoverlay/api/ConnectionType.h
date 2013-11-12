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

#ifndef RIPPLE_TESTOVERLAY_CONNECTIONTYPE_H_INCLUDED
#define RIPPLE_TESTOVERLAY_CONNECTIONTYPE_H_INCLUDED

namespace TestOverlay
{

/** A connection between two nodes. */
template <class Config>
class ConnectionType : public Config
{
public:
    typedef typename Config::Peer    Peer;
    typedef typename Config::Message Message;
    typedef typename Config::State   State;
    typedef typename State::UniqueID UniqueID;

    typedef std::vector <Message> Messages;
    typedef boost::unordered_set <UniqueID> MessageTable;

    /** Create the 'no connection' object. */
    ConnectionType ()
        : m_peer (nullptr)
    {
    }

    ConnectionType (Peer& peer, bool inbound)
        : m_peer (&peer)
        , m_inbound (inbound)
    {
    }

    ConnectionType (ConnectionType const& other)
        : m_peer (other.m_peer)
        , m_inbound (other.m_inbound)
    {
    }

    ConnectionType& operator= (ConnectionType const& other)
    {
        m_peer = other.m_peer;
        m_inbound = other.m_inbound;
        return *this;
    }

    /** Returns `true` if there is no connection. */
    bool empty () const
    {
        return m_peer == nullptr;
    }

    /** Returns `true` if this is an inbound connection.
        If there is no connection, the return value is undefined.
    */
    bool inbound () const
    {
        return m_inbound;
    }

    /** Returns the peer on the other end.
        If there is no connection, the return value is undefined.
    */
    /** @{ */
    Peer& peer ()
    {
        return *m_peer;
    }

    Peer const& peer () const
    {
        return *m_peer;
    }
    /** @} */

    /** Returns a container with the current step's incoming messages. */
    /** @{ */
    Messages& messages ()
    {
        return m_messages;
    }

    Messages const& messages () const
    {
        return m_messages;
    }
    /** @} */

    /** Returns a container with the next step's incoming messages.
        During each step, peers process the current step's message
        list, but post new messages to the pending messages list.
        This way, new messages will always process in the next step
        and not the current one.
    */
    /** @{ */
    Messages& pending ()
    {
        return m_pending;
    }

    Messages const& pending () const
    {
        return m_pending;
    }
    /** @} */


    //--------------------------------------------------------------------------

    /** A UnaryPredicate that always returns true. */
    class Any
    {
    public:
        bool operator() (ConnectionType const&) const
        {
            return true;
        }
    };

    //--------------------------------------------------------------------------

    /** A UnaryPredicate that returns `true` if the peer matches. */
    class IsPeer
    {
    public:
        explicit IsPeer (Peer const& peer)
            : m_peer (&peer)
        {
        }

        bool operator() (ConnectionType const& connection) const
        {
            return &connection.peer () == m_peer;
        }

    private:
        Peer const* m_peer;
    };

    //--------------------------------------------------------------------------

    /** A UnaryPredicate that returns `true` if the peer does not match. */
    class IsNotPeer
    {
    public:
        explicit IsNotPeer (Peer const& peer)
            : m_peer (&peer)
        {
        }

        bool operator() (ConnectionType const& connection) const
        {
            return &connection.peer () != m_peer;
        }

    private:
        Peer const* m_peer;
    };

    //--------------------------------------------------------------------------

private:
    Peer* m_peer;
    bool m_inbound;
    Messages m_messages;
    Messages m_pending;
};

}

#endif
