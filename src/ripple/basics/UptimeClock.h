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

#ifndef RIPPLE_BASICS_UPTIMETIMER_H_INCLUDED
#define RIPPLE_BASICS_UPTIMETIMER_H_INCLUDED

#include <atomic>
#include <chrono>
#include <ratio>
#include <thread>

namespace ripple {

/** Tracks program uptime to seconds precision.

    The timer caches the current time as a performance optimization.
    This allows clients to query the current time thousands of times
    per second.
*/

class UptimeClock
{
public:
    using rep        = int;
    using period     = std::ratio<1>;
    using duration   = std::chrono::duration<rep, period>;
    using time_point = std::chrono::time_point<UptimeClock>;
    static constexpr bool is_steady = std::chrono::system_clock::is_steady;

    explicit UptimeClock() = default;

    static time_point now();  // seconds since rippled program start

private:
    static std::atomic<rep>  now_;
    static std::atomic<bool> stop_;

    struct update_thread
        : private std::thread
    {
        ~update_thread();
        update_thread(update_thread&&) = default;

        using std::thread::thread;
    };

    static update_thread start_clock();
};

} // ripple

#endif
