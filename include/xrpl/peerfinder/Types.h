#pragma once

#include <xrpl/beast/clock/abstract_clock.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/peerfinder/detail/Tuning.h>

#include <chrono>
#include <cstdint>
#include <vector>

namespace xrpl::peer_finder {

using clock_type = beast::AbstractClock<std::chrono::steady_clock>;

/**
 * Represents a set of addresses.
 */
using IPAddresses = std::vector<beast::ip::Endpoint>;

//------------------------------------------------------------------------------

/**
 * Describes a connectable peer address along with some metadata.
 */
struct Endpoint
{
    Endpoint() = default;

    Endpoint(beast::ip::Endpoint ep, std::uint32_t hops);

    std::uint32_t hops = 0;
    beast::ip::Endpoint address;
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

}  // namespace xrpl::peer_finder
