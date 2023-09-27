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

#ifndef RIPPLE_CORE_TIMEKEEPER_H_INCLUDED
#define RIPPLE_CORE_TIMEKEEPER_H_INCLUDED

#include <ripple/basics/chrono.h>
#include <ripple/beast/clock/abstract_clock.h>
#include <atomic>

namespace ripple {

/** Manages various times used by the server. */
class TimeKeeper : public beast::abstract_clock<NetClock>
{
private:
    std::atomic<std::chrono::seconds> closeOffset_{};

    // Adjust system_clock::time_point for NetClock epoch
    static constexpr time_point
    adjust(std::chrono::system_clock::time_point when)
    {
        return time_point(std::chrono::duration_cast<duration>(
            when.time_since_epoch() - days(10957)));
    }

public:
    virtual ~TimeKeeper() = default;

    /** Returns the current time, using the server's clock.

        It's possible for servers to have a different value for network
        time, especially if they do not use some external mechanism for
        time synchronization (e.g. NTP or SNTP). This is fine.

        This estimate is not directly visible to other servers over the
        protocol, but it is possible for them to make an educated guess
        if this server publishes proposals or validations.

        @note The network time is adjusted for the "Ripple epoch" which
              was arbitrarily defined as 2000-01-01T00:00:00Z by Arthur
              Britto and David Schwartz during early development of the
              code. No rationale has been provided for this curious and
              annoying, but otherwise unimportant, choice.
    */
    [[nodiscard]] time_point
    now() const override
    {
        return adjust(std::chrono::system_clock::now());
    }

    /** Returns the predicted close time, in network time.

        The predicted close time represents the notional "center" of the
        network. Each server assumes that its clock is correct and tries
        to pull the close time towards its measure of network time.
    */
    [[nodiscard]] time_point
    closeTime() const
    {
        return now() + closeOffset_.load();
    }

    // This may return a negative value
    [[nodiscard]] std::chrono::seconds
    closeOffset() const
    {
        return closeOffset_.load();
    }

    /** Adjust the close time, based on the network's view of time. */
    std::chrono::seconds
    adjustCloseTime(std::chrono::seconds by)
    {
        using namespace std::chrono_literals;

        auto offset = closeOffset_.load();

        if (by == 0s && offset == 0s)
            return offset;

        // The close time adjustment is serialized externally to this
        // code. The compare/exchange only serves as a weak check and
        // should not fail. Even if it does, it's safe to simply just
        // skip the adjustment.
        closeOffset_.compare_exchange_strong(offset, [by, offset]() {
            // Ignore small offsets and push the close time
            // towards our wall time.
            if (by > 1s)
                return offset + ((by + 3s) / 4);

            if (by < -1s)
                return offset + ((by - 3s) / 4);

            return (offset * 3) / 4;
        }());

        return closeOffset_.load();
    }
};

}  // namespace ripple

#endif
