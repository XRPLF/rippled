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

#ifndef RIPPLE_TEST_MANUALTIMEKEEPER_H_INCLUDED
#define RIPPLE_TEST_MANUALTIMEKEEPER_H_INCLUDED

#include <ripple/core/TimeKeeper.h>
#include <mutex>

namespace ripple {
namespace test {

class ManualTimeKeeper : public TimeKeeper
{
public:
    ManualTimeKeeper();

    void
    run(std::vector<std::string> const& servers) override;

    time_point
    now() const override;

    time_point
    closeTime() const override;

    void
    adjustCloseTime(std::chrono::duration<std::int32_t> amount) override;

    std::chrono::duration<std::int32_t>
    nowOffset() const override;

    std::chrono::duration<std::int32_t>
    closeOffset() const override;

    void
    set(time_point now);

private:
    // Adjust system_clock::time_point for NetClock epoch
    static time_point
    adjust(std::chrono::system_clock::time_point when);

    std::mutex mutable mutex_;
    std::chrono::duration<std::int32_t> closeOffset_;
    time_point now_;
};

}  // namespace test
}  // namespace ripple

#endif
