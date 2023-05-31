//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#ifndef RIPPLE_PEERCLIENT_COMMUNICATIONMETER_H_INCLUDED
#define RIPPLE_PEERCLIENT_COMMUNICATIONMETER_H_INCLUDED

#include <boost/units/quantity.hpp>
#include <boost/units/systems/information/byte.hpp>

#include <array>
#include <chrono>
#include <ostream>

namespace ripple {

namespace units {

// We must define our own unscaled byte base unit to be able to print
// auto-scaled (e.g. kB, MB, GB) byte values in units of bytes instead of
// bits.
struct byte_unit
    : public boost::units::
          base_unit<byte_unit, boost::units::information_dimension, 1>
{
};
using storage = boost::units::make_system<byte_unit>::type;
using byte = boost::units::unit<boost::units::information_dimension, storage>;

// GCC 11.3 does not like this nested in a class namespace.
BOOST_UNITS_STATIC_CONSTANT(bytes, byte);

}  // namespace units

/**
 * Record and print statistics of one direction:
 *
 * - Total time elapsed
 * - Total number of messages
 * - Total bytes transferred
 * - Number of messages per minute over the last minute
 * - Bytes per minute over the last minute
 */
class CommunicationMeter
{
private:
    using Clock = std::chrono::steady_clock;
    using Time = std::chrono::time_point<Clock>;

    constexpr static std::size_t NBUCKETS = 12;
    constexpr static auto INTERVAL = std::chrono::seconds(5);
    constexpr static char const* WINDOW_NAME = "one minute";

    std::size_t nrequests_ = 0;
    boost::units::quantity<units::byte> nbytes_ = 0 * units::bytes;
    std::array<std::size_t, NBUCKETS> histRequests_{};
    std::array<boost::units::quantity<units::byte>, NBUCKETS> histBytes_{};
    unsigned int ibucket_ = 0;
    Time start_ = Clock::now();
    Time horizon_ = Clock::now() + INTERVAL;

public:
    void
    addMessage(std::size_t nbytes)
    {
        return addMessage(nbytes * units::bytes);
    }

    void
    addMessage(boost::units::quantity<units::byte> nbytes);

private:
    friend std::ostream&
    operator<<(std::ostream& out, CommunicationMeter const& meter);
};

}  // namespace ripple

namespace boost {
namespace units {

template <>
struct base_unit_info<ripple::units::byte_unit>
{
    static BOOST_CONSTEXPR const char*
    name()
    {
        return ("byte");
    }
    static BOOST_CONSTEXPR const char*
    symbol()
    {
        return ("B");
    }
};

}  // namespace units
}  // namespace boost

#endif
