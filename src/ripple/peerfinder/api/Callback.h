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

#ifndef RIPPLE_PEERFINDER_CALLBACK_H_INCLUDED
#define RIPPLE_PEERFINDER_CALLBACK_H_INCLUDED

namespace ripple {
namespace PeerFinder {

/** The Callback receives PeerFinder notifications.
    The notifications are sent on a thread owned by the PeerFinder,
    so it is best not to do too much work in here. Just post functor
    to another worker thread or job queue and return.
*/
struct Callback
{
    /** Sends a set of Endpoint records to the specified peer. */
    virtual void sendPeerEndpoints (PeerID const& id,
        std::vector <Endpoint> const& endpoints) = 0;

    /** Initiate outgoing Peer connections to the specified set of endpoints. */
    virtual void connectPeerEndpoints (std::vector <IPAddress> const& list) = 0;

    /** Impose a load charge on the specified peer. */
    virtual void chargePeerLoadPenalty (PeerID const& id) = 0;
};

}
}

#endif
