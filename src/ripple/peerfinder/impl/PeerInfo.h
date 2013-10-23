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

#ifndef RIPPLE_PEERFINDER_PEERINFO_H_INCLUDED
#define RIPPLE_PEERFINDER_PEERINFO_H_INCLUDED

namespace ripple {
namespace PeerFinder {

//typedef AgedHistory <std::set <Endpoint> > Endpoints;

//--------------------------------------------------------------------------

// we keep one of these for each connected peer
struct PeerInfo
{
    PeerInfo (PeerID const& id_,
              IPAddress const& address_,
              bool inbound_,
              DiscreteTime now)
        : id (id_)
        , address (address_)
        , inbound (inbound_)
        , checked (inbound_ ? false : true)
        , canAccept (inbound_ ? false : true)
        , connectivityCheckInProgress (false)
        , whenSendEndpoints (now)
        , whenAcceptEndpoints (now)
    {
    }

    PeerID id;
    IPAddress address;
    bool inbound;

    // Tells us if we checked the connection. Outbound connections
    // are always considered checked since we successfuly connected.
    bool mutable checked;

    // Set to indicate if the connection can receive incoming at the
    // address advertised in mtENDPOINTS. Only valid if checked is true.
    bool mutable canAccept;

    // Set to indicate that a connection check for this peer is in
    // progress. Valid always.
    bool mutable connectivityCheckInProgress;

    // The time after which we will send the peer mtENDPOINTS
    DiscreteTime mutable whenSendEndpoints;

    // The time after which we will accept mtENDPOINTS from the peer
    // This is to prevent flooding or spamming. Receipt of mtENDPOINTS
    // sooner than the allotted time should impose a load charge.
    //
    DiscreteTime mutable whenAcceptEndpoints;

    // The set of all recent IPAddress that we have seen from this peer.
    // We try to avoid sending a peer the same addresses they gave us.
    //
    CycledSet <IPAddress,
               IPAddress::hasher,
               IPAddress::key_equal> mutable received;
};

}
}

#endif
