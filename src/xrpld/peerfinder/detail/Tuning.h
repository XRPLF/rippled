#pragma once

#include <array>

/** Heuristically tuned constants. */
/** @{ */
namespace xrpl::PeerFinder::Tuning {

// Need to be named before converting
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum {
    //---------------------------------------------------------
    //
    // Automatic Connection Policy
    //
    //---------------------------------------------------------

    /** Time to wait between making batches of connection attempts */
    SecondsPerConnect = 10

    /** Maximum number of simultaneous connection attempts. */
    ,
    MaxConnectAttempts = 20

    /** The percentage of total peer slots that are outbound.
        The number of outbound peers will be the larger of the
        minOutCount and outPercent * Config::maxPeers specially
        rounded.
    */
    ,
    OutPercent = 15

    /** A hard minimum on the number of outgoing connections.
        This is enforced outside the Logic, so that the unit test
        can use any settings it wants.
    */
    ,
    MinOutCount = 10

    /** The default value of Config::maxPeers. */
    ,
    DefaultMaxPeers = 21

    /** Max redirects we will accept from one connection.
        Redirects are limited for security purposes, to prevent
        the address caches from getting flooded.
    */
    ,
    MaxRedirects = 30
};

//------------------------------------------------------------------------------
//
// Fixed
//
//------------------------------------------------------------------------------

static std::array<int, 10> const kCONNECTION_BACKOFF{{1, 1, 2, 3, 5, 8, 13, 21, 34, 55}};

//------------------------------------------------------------------------------
//
// Bootcache
//
//------------------------------------------------------------------------------

// Need to be named before converting
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum {
    // Threshold of cache entries above which we trim.
    BootcacheSize = 1000

    // The percentage of addresses we prune when we trim the cache.
    ,
    BootcachePrunePercent = 10
};

// The cool down wait between database updates
// Ideally this should be larger than the time it takes a full
// peer to send us a set of addresses and then disconnect.
//
static std::chrono::seconds const kBOOTCACHE_COOLDOWN_TIME(60);

//------------------------------------------------------------------------------
//
// Livecache
//
//------------------------------------------------------------------------------

// Drop incoming messages with hops greater than this number
std::uint32_t constexpr kMAX_HOPS = 6;

// How many Endpoint to send in each mtENDPOINTS
std::uint32_t constexpr kNUMBER_OF_ENDPOINTS = 2 * kMAX_HOPS;

// The most Endpoint we will accept in mtENDPOINTS
std::uint32_t constexpr kNUMBER_OF_ENDPOINTS_MAX =
    std::max<decltype(kNUMBER_OF_ENDPOINTS)>(kNUMBER_OF_ENDPOINTS * 2, 64);

// Number of addresses we provide when redirecting.
std::uint32_t constexpr kREDIRECT_ENDPOINT_COUNT = 10;

// How often we send or accept mtENDPOINTS messages per peer
// (we use a prime number of purpose)
std::chrono::seconds constexpr kSECONDS_PER_MESSAGE(151);

// How long an Endpoint will stay in the cache
// This should be a small multiple of the broadcast frequency
std::chrono::seconds constexpr kLIVE_CACHE_SECONDS_TO_LIVE(30);

// How much time to wait before trying an outgoing address again.
// Note that we ignore the port for purposes of comparison.
std::chrono::seconds constexpr kRECENT_ATTEMPT_DURATION(60);

}  // namespace xrpl::PeerFinder::Tuning
/** @} */
