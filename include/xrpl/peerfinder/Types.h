#pragma once

#include <xrpl/beast/clock/abstract_clock.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/peerfinder/detail/Tuning.h>

#include <chrono>
#include <cstdint>
#include <vector>

namespace xrpl::PeerFinder {

using clock_type = beast::AbstractClock<std::chrono::steady_clock>;

/**
 * Represents a set of addresses.
 */
using IPAddresses = std::vector<beast::IP::Endpoint>;

//------------------------------------------------------------------------------

/**
 * Describes a connectable peer address along with some metadata.
 */
struct Endpoint
{
    Endpoint() = default;

    Endpoint(beast::IP::Endpoint ep, std::uint32_t hops);

    std::uint32_t hops = 0;
    beast::IP::Endpoint address;
};

inline bool
operator<(Endpoint const& lhs, Endpoint const& rhs)
{
    return lhs.address < rhs.address;
}

/**
 * A set of Endpoint used for connecting.
 */
using Endpoints = std::vector<Endpoint>;

}  // namespace xrpl::PeerFinder
