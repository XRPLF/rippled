//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2021, Howard Hinnant <howard.hinnant@gmail.com>

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

#include <ripple/beast/clock/basic_seconds_clock.h>

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace beast {

namespace {

// Updates the clock
class seconds_clock_thread
{
    using Clock = basic_seconds_clock::Clock;

    bool stop_;
    std::mutex mut_;
    std::condition_variable cv_;
    std::thread thread_;
    Clock::time_point tp_;

public:
    ~seconds_clock_thread();
    seconds_clock_thread();

    Clock::time_point
    now();

private:
    void
    run();
};

seconds_clock_thread::~seconds_clock_thread()
{
    assert(thread_.joinable());
    {
        std::lock_guard lock(mut_);
        stop_ = true;
    }  // publish stop_ asap so if waiting thread times-out, it will see it
    cv_.notify_one();
    thread_.join();
}

seconds_clock_thread::seconds_clock_thread() : stop_{false}, tp_{Clock::now()}
{
    thread_ = std::thread(&seconds_clock_thread::run, this);
}

seconds_clock_thread::Clock::time_point
seconds_clock_thread::now()
{
    std::lock_guard lock(mut_);
    return tp_;
}

void
seconds_clock_thread::run()
{
    std::unique_lock lock(mut_);
    while (true)
    {
        using namespace std::chrono;

        tp_ = Clock::now();
        auto const when = floor<seconds>(tp_) + 1s;
        if (cv_.wait_until(lock, when, [this] { return stop_; }))
            return;
    }
}

}  // unnamed namespace

basic_seconds_clock::time_point
basic_seconds_clock::now()
{
    static seconds_clock_thread clk;
    return clk.now();
}

}  // namespace beast
