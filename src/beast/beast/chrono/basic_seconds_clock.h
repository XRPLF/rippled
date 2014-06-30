//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_CHRONO_BASIC_SECONDS_CLOCK_H_INCLUDED
#define BEAST_CHRONO_BASIC_SECONDS_CLOCK_H_INCLUDED

#include <beast/chrono/chrono_util.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

extern "C" int main(int argc, char** argv);

namespace beast {

/** A clock whose minimum resolution is one second.
    The purpose of this class is to optimize the performance of the now()
    member function call. It uses a dedicated thread that wakes up at least
    once per second to sample the requested trivial clock.
    @tparam TrivialClock The clock to sample.
*/
template <class TrivialClock>
class basic_seconds_clock
{
public:
    typedef std::chrono::seconds      duration;
    typedef typename duration::period period;
    typedef typename duration::rep    rep;
    typedef std::chrono::time_point <basic_seconds_clock> time_point;

    static bool const is_steady = TrivialClock::is_steady;

    ~basic_seconds_clock() = delete;
    basic_seconds_clock() = delete;
    static time_point now();

private:
    static std::mutex& mut()
    {
        static std::mutex mut;
        return mut;
    }

    static std::condition_variable& cv()
    {
        static std::condition_variable cv;
        return cv;
    }

    enum {uninitialized, started, stopping, stopped};

    static unsigned& state()
    {
        static unsigned state = uninitialized;
        return state;
    }

    static time_point& get_now()
    {
        static time_point now;
        return now;
    }

    static void stop();
    static void update_clock();
    static unsigned init();

    friend int ::main(int argc, char** argv);
};

/** 
*  If uninitialized, set to stopped.
*  If started, stop.
*  If stopping, wait for stopped then return.
*  If stopped, do nothing.
*/
template <class TrivialClock>
void
basic_seconds_clock<TrivialClock>::stop()
{
    std::unique_lock<std::mutex> lk(mut());
    if (state() == uninitialized)
    {
        state() = stopped;
    }
    else if (state() != stopped)
    {
        if (state() == started)
        {
            state() = stopping;
            cv().notify_all();
        }
        while (state() != stopped)
            cv().wait(lk);
    }
}

template <class TrivialClock>
typename basic_seconds_clock<TrivialClock>::time_point
basic_seconds_clock<TrivialClock>::now()
{
    struct initializer
    {
        ~initializer() { stop(); }
        initializer()  { init(); }
        initializer(initializer const&) = delete;
        initializer& operator=(initializer const&) = delete;
    };
    static initializer start;
    std::lock_guard<std::mutex> lk(mut());
    return get_now();
}

template <class TrivialClock>
void
basic_seconds_clock<TrivialClock>::update_clock()
{
    using namespace std::chrono;
    std::unique_lock<std::mutex> lk(mut());
    while (state() == started)
    {
        get_now() = time_point
                      (floor<duration>(TrivialClock::now().time_since_epoch()));
        cv().wait_for(lk, seconds(1));
    }
    state() = stopped;
    cv().notify_all();
}

template <class TrivialClock>
unsigned
basic_seconds_clock<TrivialClock>::init()
{
    using namespace std::chrono;
    // Run these first to put them last in the atexit chain
    cv();
    mut();
    get_now() = time_point
                      (floor<duration>(TrivialClock::now().time_since_epoch()));
    state() = started;
    std::thread(&update_clock).detach();
    return 0;
}

}

#endif
