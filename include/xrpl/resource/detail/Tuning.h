#ifndef XRPL_RESOURCE_TUNING_H_INCLUDED
#define XRPL_RESOURCE_TUNING_H_INCLUDED

#include <chrono>

namespace ripple {
namespace Resource {

/** Tunable constants. */
enum {
    // Balance at which a warning is issued
    warningThreshold = 5000

    // Balance at which the consumer is disconnected
    ,
    dropThreshold = 25000

    // The number of seconds in the exponential decay window
    // (This should be a power of two)
    ,
    decayWindowSeconds = 32

    // The minimum balance required in order to include a load source in gossip
    ,
    minimumGossipBalance = 1000
};

// The number of seconds until an inactive table item is removed
std::chrono::seconds constexpr secondsUntilExpiration{300};

// Number of seconds until imported gossip expires
std::chrono::seconds constexpr gossipExpirationSeconds{30};

}  // namespace Resource
}  // namespace ripple

#endif
