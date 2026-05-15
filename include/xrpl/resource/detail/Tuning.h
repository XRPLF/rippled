#pragma once

#include <chrono>

namespace xrpl::Resource {

/** Tunable constants. */

// balance at which a warning is issued
static constexpr auto kWarningThreshold = 5000;

// balance at which the consumer is disconnected
static constexpr auto kDropThreshold = 25000;

// seconds in exponential decay window (power of two)
static constexpr auto kDecayWindowSeconds = 32;

// minimum balance to include a load source in gossip
static constexpr auto kMinimumGossipBalance = 1000;

// The number of seconds until an inactive table item is removed
static constexpr std::chrono::seconds kSecondsUntilExpiration{300};

// Number of seconds until imported gossip expires
static constexpr std::chrono::seconds kGossipExpirationSeconds{30};

}  // namespace xrpl::Resource
