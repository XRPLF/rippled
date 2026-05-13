#pragma once

#include <chrono>

namespace xrpl::Resource {

/** Tunable constants. */

// balance at which a warning is issued
static constexpr auto kWARNING_THRESHOLD = 5000;

// balance at which the consumer is disconnected
static constexpr auto kDROP_THRESHOLD = 25000;

// seconds in exponential decay window (power of two)
static constexpr auto kDECAY_WINDOW_SECONDS = 32;

// minimum balance to include a load source in gossip
static constexpr auto kMINIMUM_GOSSIP_BALANCE = 1000;

// The number of seconds until an inactive table item is removed
static constexpr std::chrono::seconds kSECONDS_UNTIL_EXPIRATION{300};

// Number of seconds until imported gossip expires
static constexpr std::chrono::seconds kGOSSIP_EXPIRATION_SECONDS{30};

}  // namespace xrpl::Resource
