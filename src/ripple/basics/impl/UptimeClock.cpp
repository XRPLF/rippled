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

#include <ripple/basics/UptimeClock.h>

namespace ripple {

std::atomic<UptimeClock::rep> UptimeClock::now_{0};  // seconds since start
std::atomic<bool> UptimeClock::stop_{false};         // stop update thread

// On rippled shutdown, cancel and wait for the update thread
UptimeClock::update_thread::~update_thread()
{
    if (joinable())
    {
        stop_ = true;
        // This join() may take up to a 1s, but happens only
        // once at rippled shutdown.
        join();
    }
}

// Launch the update thread
UptimeClock::update_thread
UptimeClock::start_clock()
{
    return update_thread{[] {
        using namespace std;
        using namespace std::chrono;

        // Wake up every second and update now_
        auto next = system_clock::now() + 1s;
        while (!stop_)
        {
            this_thread::sleep_until(next);
            next += 1s;
            ++now_;
        }
    }};
}

// This actually measures time since first use, instead of since rippled start.
// However the difference between these two epochs is a small fraction of a
// second and unimportant.

UptimeClock::time_point
UptimeClock::now()
{
    // start the update thread on first use
    static const auto init = start_clock();

    // Return the number of seconds since rippled start
    return time_point{duration{now_}};
}

}  // namespace ripple
