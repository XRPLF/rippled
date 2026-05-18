#pragma once

#include <array>

/** Heuristically tuned constants. */
/** @{ */
namespace xrpl::PeerFinder::Tuning {

//---------------------------------------------------------
//
// Automatic Connection Policy
//
//---------------------------------------------------------

/** Time to wait between making batches of connection attempts */
static constexpr auto kSecondsPerConnect = 10;

/** Maximum number of simultaneous connection attempts. */
static constexpr auto kMaxConnectAttempts = 20;

/** The percentage of total peer slots that are outbound.
    The number of outbound peers will be the larger of the
    minOutCount and outPercent * Config::maxPeers specially
    rounded.
*/
static constexpr auto kOutPercent = 15;

/** A hard minimum on the number of outgoing connections.
    This is enforced outside the Logic, so that the unit test
    can use any settings it wants.
*/
static constexpr auto kMinOutCount = 10;

/** The default value of Config::maxPeers. */
static constexpr auto kDefaultMaxPeers = 21;

/** Max redirects we will accept from one connection.
    Redirects are limited for security purposes, to prevent
    the address caches from getting flooded.
*/
static constexpr auto kMaxRedirects = 30;

//------------------------------------------------------------------------------
//
// Fixed
//
//------------------------------------------------------------------------------

static std::array<int, 10> const kConnectionBackoff{{1, 1, 2, 3, 5, 8, 13, 21, 34, 55}};

//------------------------------------------------------------------------------
//
// Bootcache
//
//------------------------------------------------------------------------------

// Threshold of cache entries above which we trim.
static constexpr auto kBootcacheSize = 1000;

// The percentage of addresses we prune when we trim the cache.
static constexpr auto kBootcachePrunePercent = 10;

// The cool down wait between database updates
// Ideally this should be larger than the time it takes a full
// peer to send us a set of addresses and then disconnect.
//
static std::chrono::seconds const kBootcacheCooldownTime(60);

//------------------------------------------------------------------------------
//
// Livecache
//
//------------------------------------------------------------------------------

// Drop incoming messages with hops greater than this number
constexpr std::uint32_t kMaxHops = 6;

// How many Endpoint to send in each mtENDPOINTS
constexpr std::uint32_t kNumberOfEndpoints = 2 * kMaxHops;

// The most Endpoint we will accept in mtENDPOINTS
constexpr std::uint32_t kNumberOfEndpointsMax =
    std::max<decltype(kNumberOfEndpoints)>(kNumberOfEndpoints * 2, 64);

// Number of addresses we provide when redirecting.
constexpr std::uint32_t kRedirectEndpointCount = 10;

// How often we send or accept mtENDPOINTS messages per peer
// (we use a prime number of purpose)
constexpr std::chrono::seconds kSecondsPerMessage(151);

// How long an Endpoint will stay in the cache
// This should be a small multiple of the broadcast frequency
constexpr std::chrono::seconds kLiveCacheSecondsToLive(30);

// How much time to wait before trying an outgoing address again.
// Note that we ignore the port for purposes of comparison.
constexpr std::chrono::seconds kRecentAttemptDuration(60);

}  // namespace xrpl::PeerFinder::Tuning
/** @} */
