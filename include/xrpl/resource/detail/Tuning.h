#pragma once

#include <chrono>

namespace xrpl::Resource {

/** Tunable constants. */
// Need to be named before converting
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum {
    // Balance at which a warning is issued
    WarningThreshold = 5000

    // Balance at which the consumer is disconnected
    ,
    DropThreshold = 25000

    // The number of seconds in the exponential decay window
    // (This should be a power of two)
    ,
    DecayWindowSeconds = 32

    // The minimum balance required in order to include a load source in gossip
    ,
    MinimumGossipBalance = 1000
};

// The number of seconds until an inactive table item is removed
std::chrono::seconds constexpr kSECONDS_UNTIL_EXPIRATION{300};

// Number of seconds until imported gossip expires
std::chrono::seconds constexpr kGOSSIP_EXPIRATION_SECONDS{30};

}  // namespace xrpl::Resource
