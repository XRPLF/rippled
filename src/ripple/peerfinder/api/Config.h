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

#ifndef RIPPLE_PEERFINDER_CONFIG_H_INCLUDED
#define RIPPLE_PEERFINDER_CONFIG_H_INCLUDED

namespace ripple {
namespace PeerFinder {

/** PeerFinder configuration settings. */
struct Config
{
    /** The largest number of public peer slots to allow.
        This includes both inbound and outbound, but does not include
        fixed peers.
    */
    int maxPeers;

    /** The number of automatic outbound connections to maintain.
        Outbound connections are only maintained if autoConnect
        is `true`. The value can be fractional; The decision to round up
        or down will be made using a per-process pseudorandom number and
        a probability proportional to the fractional part.
        Example:
            If outPeers is 9.3, then 30% of nodes will maintain 9 outbound
            connections, while 70% of nodes will maintain 10 outbound
            connections.
    */
    double outPeers;

    /** `true` if we want to accept incoming connections. */
    bool wantIncoming;

    /** `true` if we want to establish connections automatically */
    bool autoConnect;

    /** The listening port number. */
    std::uint16_t listeningPort;

    /** The set of features we advertise. */
    std::string features;
    
    //--------------------------------------------------------------------------

    /** Create a configuration with default values. */
    Config ();

    /** Returns a suitable value for outPeers according to the rules. */
    double calcOutPeers () const;

    /** Adjusts the values so they follow the business rules. */
    void applyTuning ();

    /** Write the configuration into a property stream */
    void onWrite (beast::PropertyStream::Map& map);
};

}
}

#endif
