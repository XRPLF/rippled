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

#ifndef RIPPLE_PEERFINDER_TUNING_H_INCLUDED
#define RIPPLE_PEERFINDER_TUNING_H_INCLUDED

namespace ripple {
namespace PeerFinder {

// Tunable constants
enum
{
    //---------------------------------------------------------
    //
    // Connection policy settings
    //

    // How often we will try to make outgoing connections
    secondsPerConnect               = 10

    // The largest connections we will attempt simultaneously
    ,maxAddressesPerAttempt         = 30

    //---------------------------------------------------------
    //
    // Endpoint settings
    //

    // How often we send or accept mtENDPOINTS messages per peer
    ,secondsPerMessage             = 5

    // How many Endpoint to send in each mtENDPOINTS
    ,numberOfEndpoints               = 10

    // The most Endpoint we will accept in mtENDPOINTS
    ,numberOfEndpointsMax            = 20

    // How long an Endpoint will stay in the cache
    // This should be a small multiple of the broadcast frequency
    ,cacheSecondsToLive     = 60

    // The maximum number of hops that we allow. Peers farther
    // away than this are dropped.
    ,maxPeerHopCount        = 10

    //---------------------------------------------------------
    //
    // LegacyEndpoint Settings
    //

    // How many legacy endpoints to keep in our cache
    ,legacyEndpointCacheSize            = 1000

    // How many cache mutations between each database update
    ,legacyEndpointMutationsPerUpdate   = 50
};

}
}

#endif
