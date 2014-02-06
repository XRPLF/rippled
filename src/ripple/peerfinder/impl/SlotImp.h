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

#ifndef RIPPLE_PEERFINDER_SLOTIMP_H_INCLUDED
#define RIPPLE_PEERFINDER_SLOTIMP_H_INCLUDED

#include "../api/Slot.h"

namespace ripple {
namespace PeerFinder {

class SlotImp : public Slot
{
public:
    bool const m_inbound;
    bool const m_fixed;
    bool m_cluster;
    State m_state;
    IP::Endpoint m_remote_endpoint;
    boost::optional <IP::Endpoint> m_local_endpoint;
    boost::optional <RipplePublicKey> m_public_key;

    // inbound
    SlotImp (IP::Endpoint const& local_endpoint,
        IP::Endpoint const& remote_endpoint, bool fixed)
        : m_inbound (true)
        , m_fixed (fixed)
        , m_cluster (false)
        , m_state (accept)
        , m_remote_endpoint (remote_endpoint)
        , m_local_endpoint (local_endpoint)
    {
    }

    // outbound
    SlotImp (IP::Endpoint const& remote_endpoint, bool fixed)
        : m_inbound (false)
        , m_fixed (fixed)
        , m_cluster (false)
        , m_state (connect)
        , m_remote_endpoint (remote_endpoint)
    {
    }

    ~SlotImp ()
    {
    }

    bool inbound () const
    {
        return m_inbound;
    }

    bool fixed () const
    {
        return m_fixed;
    }

    bool cluster () const
    {
        return m_cluster;
    }

    State state () const
    {
        return m_state;
    }

    IP::Endpoint const& remote_endpoint () const
    {
        return m_remote_endpoint;
    }

    boost::optional <IP::Endpoint> const& local_endpoint () const
    {
        return m_local_endpoint;
    }

    boost::optional <RipplePublicKey> const& public_key () const
    {
        return m_public_key;
    }

    //--------------------------------------------------------------------------

    void state (State state_)
    {
        // The state must be different
        assert (state_ != m_state);

        // You can't transition into the initial states
        assert (state_ != accept && state_ != connect);

        // Can only become connected from outbound connect state
        assert (state_ != connected || (! m_inbound && m_state == connect));

        // Can only become active from the accept or connected state
        assert (state_ != active || (m_state == accept || m_state == connected));

        // Can't gracefully close on an outbound connection attempt
        assert (state_ != closing || m_state != connect);

        m_state = state_;
    }

    void local_endpoint (IP::Endpoint const& endpoint)
    {
        m_local_endpoint = endpoint;
    }

    void remote_endpoint (IP::Endpoint const& endpoint)
    {
        m_remote_endpoint = endpoint;
    }

    void public_key (RipplePublicKey const& key)
    {
        m_public_key = key;
    }

    void cluster (bool cluster_)
    {
        m_cluster = cluster_;
    }
};

//------------------------------------------------------------------------------

Slot::~Slot ()
{
}

}
}

#endif
