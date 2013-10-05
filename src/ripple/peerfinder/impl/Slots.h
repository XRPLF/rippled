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

#ifndef RIPPLE_PEERFINDER_SLOTS_H_INCLUDED
#define RIPPLE_PEERFINDER_SLOTS_H_INCLUDED

namespace ripple {
namespace PeerFinder {

class Slots
{
public:
    Slots ();

    void update (Config const& config);
    void addPeer (Config const& config, bool inbound);
    void dropPeer (Config const& config, bool inbound);
    uint32 uptimeMinutes () const;

    // Most recent time when we went from 0 to 1 peers
    Time startTime;

    // Current total of connected peers that have HELLOed
    int peerCount;

    // The portion of peers which are incoming connections
    int inboundCount;

    // The portion of peers which are outgoing connections
    int outboundCount;

    // The number of outgoing peer connections we want (calculated)
    int outDesired;

    // The number of available incoming slots (calculated)
    int inboundSlots;

    // The maximum number of incoming slots (calculated)
    int inboundSlotsMaximum;

private:
    bool m_roundUpwards;
};

}
}

#endif
