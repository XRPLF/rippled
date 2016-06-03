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

#ifndef RIPPLE_OVERLAY_PREDICATES_H_INCLUDED
#define RIPPLE_OVERLAY_PREDICATES_H_INCLUDED

#include <ripple/overlay/Message.h>
#include <ripple/overlay/Peer.h>

#include <set>

namespace ripple {

/** Sends a message to all peers */
struct send_always
{
    using return_type = void;

    Message::pointer const& msg;

    send_always(Message::pointer const& m)
        : msg(m)
    { }

    void operator()(std::shared_ptr<Peer> const& peer) const
    {
        peer->send (msg);
    }
};

//------------------------------------------------------------------------------

/** Sends a message to match peers */
template <typename Predicate>
struct send_if_pred
{
    using return_type = void;

    Message::pointer const& msg;
    Predicate const& predicate;

    send_if_pred(Message::pointer const& m, Predicate const& p)
    : msg(m), predicate(p)
    { }

    void operator()(std::shared_ptr<Peer> const& peer) const
    {
        if (predicate (peer))
            peer->send (msg);
    }
};

/** Helper function to aid in type deduction */
template <typename Predicate>
send_if_pred<Predicate> send_if (
    Message::pointer const& m,
        Predicate const &f)
{
    return send_if_pred<Predicate>(m, f);
}

//------------------------------------------------------------------------------

/** Sends a message to non-matching peers */
template <typename Predicate>
struct send_if_not_pred
{
    using return_type = void;

    Message::pointer const& msg;
    Predicate const& predicate;

    send_if_not_pred(Message::pointer const& m, Predicate const& p)
        : msg(m), predicate(p)
    { }

    void operator()(std::shared_ptr<Peer> const& peer) const
    {
        if (!predicate (peer))
            peer->send (msg);
    }
};

/** Helper function to aid in type deduction */
template <typename Predicate>
send_if_not_pred<Predicate> send_if_not (
    Message::pointer const& m,
        Predicate const &f)
{
    return send_if_not_pred<Predicate>(m, f);
}

//------------------------------------------------------------------------------

/** Select the specific peer */
struct match_peer
{
    Peer const* matchPeer;

    match_peer (Peer const* match = nullptr)
        : matchPeer (match)
    { }

    bool operator() (std::shared_ptr<Peer> const& peer) const
    {
        if(matchPeer && (peer.get () == matchPeer))
            return true;

        return false;
    }
};

//------------------------------------------------------------------------------

/** Select all peers (except optional excluded) that are in our cluster */
struct peer_in_cluster
{
    match_peer skipPeer;

    peer_in_cluster (Peer const* skip = nullptr)
        : skipPeer (skip)
    { }

    bool operator() (std::shared_ptr<Peer> const& peer) const
    {
        if (skipPeer (peer))
            return false;

        if (! peer->cluster())
            return false;

        return true;
    }
};

//------------------------------------------------------------------------------

/** Select all peers that are in the specified set */
struct peer_in_set
{
    std::set <Peer::id_t> const& peerSet;

    peer_in_set (std::set<Peer::id_t> const& peers)
        : peerSet (peers)
    { }

    bool operator() (std::shared_ptr<Peer> const& peer) const
    {
        if (peerSet.count (peer->id ()) == 0)
            return false;

        return true;
    }
};

}

#endif
