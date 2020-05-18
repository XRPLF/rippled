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

#include <array>

namespace ripple {
namespace PeerFinder {

/** Heuristically tuned constants. */
/** @{ */
namespace Tuning {

enum {
    //---------------------------------------------------------
    //
    // Automatic Connection Policy
    //
    //---------------------------------------------------------

    /** Time to wait between making batches of connection attempts */
    secondsPerConnect = 10

    /** Maximum number of simultaneous connection attempts. */
    ,
    maxConnectAttempts = 20

    /** The percentage of total peer slots that are outbound.
        The number of outbound peers will be the larger of the
        minOutCount and outPercent * Config::maxPeers specially
        rounded.
    */
    ,
    outPercent = 15

    /** A hard minimum on the number of outgoing connections.
        This is enforced outside the Logic, so that the unit test
        can use any settings it wants.
    */
    ,
    minOutCount = 10

    /** The default value of Config::maxPeers. */
    ,
    defaultMaxPeers = 21

    /** Max redirects we will accept from one connection.
        Redirects are limited for security purposes, to prevent
        the address caches from getting flooded.
    */
    ,
    maxRedirects = 30
};

//------------------------------------------------------------------------------
//
// Fixed
//
//------------------------------------------------------------------------------

static std::array<int, 10> const connectionBackoff{
    {1, 1, 2, 3, 5, 8, 13, 21, 34, 55}};

//------------------------------------------------------------------------------
//
// Bootcache
//
//------------------------------------------------------------------------------

enum {
    // Threshold of cache entries above which we trim.
    bootcacheSize = 1000

    // The percentage of addresses we prune when we trim the cache.
    ,
    bootcachePrunePercent = 10
};

// The cool down wait between database updates
// Ideally this should be larger than the time it takes a full
// peer to send us a set of addresses and then disconnect.
//
static std::chrono::seconds const bootcacheCooldownTime(60);

//------------------------------------------------------------------------------
//
// Livecache
//
//------------------------------------------------------------------------------

enum {
    // Drop incoming messages with hops greater than this number
    maxHops = 6

    // How many Endpoint to send in each mtENDPOINTS
    ,
    numberOfEndpoints = 2 * maxHops

    // The most Endpoint we will accept in mtENDPOINTS
    ,
    numberOfEndpointsMax = 20

    // The number of peers that we want by default, unless an
    // explicit value is set in the config file.
    ,
    defaultMaxPeerCount = 21

    /** Number of addresses we provide when redirecting. */
    ,
    redirectEndpointCount = 10
};

// How often we send or accept mtENDPOINTS messages per peer
// (we use a prime number of purpose)
static std::chrono::seconds const secondsPerMessage(61);

// How long an Endpoint will stay in the cache
// This should be a small multiple of the broadcast frequency
static std::chrono::seconds const liveCacheSecondsToLive(30);

//
//
//

// How much time to wait before trying an outgoing address again.
// Note that we ignore the port for purposes of comparison.
static std::chrono::seconds const recentAttemptDuration(60);

}  // namespace Tuning
/** @} */

}  // namespace PeerFinder
}  // namespace ripple

#endif
