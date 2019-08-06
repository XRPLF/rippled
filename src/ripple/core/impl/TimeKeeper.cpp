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

#include <ripple/basics/Log.h>
#include <ripple/core/TimeKeeper.h>
#include <ripple/core/impl/SNTPClock.h>
#include <memory>
#include <mutex>

namespace ripple {

class TimeKeeperImpl : public TimeKeeper
{
private:
    beast::Journal j_;
    std::mutex mutable mutex_;
    std::chrono::duration<std::int32_t> closeOffset_;
    std::unique_ptr<SNTPClock> clock_;

    // Adjust system_clock::time_point for NetClock epoch
    static
    time_point
    adjust (std::chrono::system_clock::time_point when)
    {
        return time_point(
            std::chrono::duration_cast<duration>(
                when.time_since_epoch() -
                    days(10957)));
    }

public:
    explicit
    TimeKeeperImpl (beast::Journal j)
        : j_ (j)
        , closeOffset_ {}
        , clock_ (make_SNTPClock(j))
    {
    }

    void
    run (std::vector<
        std::string> const& servers) override
    {
        clock_->run(servers);
    }

    time_point
    now() const override
    {
        std::lock_guard lock(mutex_);
        return adjust(clock_->now());
    }

    time_point
    closeTime() const override
    {
        std::lock_guard lock(mutex_);
        return adjust(clock_->now()) + closeOffset_;
    }

    void
    adjustCloseTime(
        std::chrono::duration<std::int32_t> amount) override
    {
        using namespace std::chrono;
        auto const s = amount.count();
        std::lock_guard lock(mutex_);
        // Take large offsets, ignore small offsets,
        // push the close time towards our wall time.
        if (s > 1)
            closeOffset_ += seconds((s + 3) / 4);
        else if (s < -1)
            closeOffset_ += seconds((s - 3) / 4);
        else
            closeOffset_ = (closeOffset_ * 3) / 4;
        if (closeOffset_.count() != 0)
        {
            if (std::abs (closeOffset_.count()) < 60)
            {
                JLOG(j_.info()) <<
                    "TimeKeeper: Close time offset now " <<
                        closeOffset_.count();
            }
            else
            {
                JLOG(j_.warn()) <<
                    "TimeKeeper: Large close time offset = " <<
                        closeOffset_.count();
            }
        }
    }

    std::chrono::duration<std::int32_t>
    nowOffset() const override
    {
        using namespace std::chrono;
        using namespace std;
        lock_guard lock(mutex_);
        return duration_cast<chrono::duration<int32_t>>(clock_->offset());
    }

    std::chrono::duration<std::int32_t>
    closeOffset() const override
    {
        std::lock_guard lock(mutex_);
        return closeOffset_;
    }
};

//------------------------------------------------------------------------------

std::unique_ptr<TimeKeeper>
make_TimeKeeper (beast::Journal j)
{
    return std::make_unique<TimeKeeperImpl>(j);
}

} // ripple
