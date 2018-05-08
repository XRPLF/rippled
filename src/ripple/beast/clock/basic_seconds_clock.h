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

#include <ripple/basics/date.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace beast {

namespace detail {

class seconds_clock_worker
{
public:
    virtual void sample () = 0;
    virtual ~seconds_clock_worker() = default;
};

//------------------------------------------------------------------------------

// Updates the clocks
class seconds_clock_thread
{
public:
    using mutex = std::mutex;
    using cond_var = std::condition_variable;
    using lock_guard = std::lock_guard <mutex>;
    using unique_lock = std::unique_lock <mutex>;
    using clock_type = std::chrono::steady_clock;
    using seconds = std::chrono::seconds;
    using thread = std::thread;
    using workers = std::vector <seconds_clock_worker*>;

    bool stop_;
    mutex mutex_;
    cond_var cond_;
    workers workers_;
    thread thread_;

    seconds_clock_thread ()
        : stop_ (false)
    {
        thread_ = thread (&seconds_clock_thread::run, this);
    }

    ~seconds_clock_thread ()
    {
        stop();
    }

    void add (seconds_clock_worker& w)
    {
        lock_guard lock (mutex_);
        workers_.push_back (&w);
    }

    void remove (seconds_clock_worker& w)
    {
        lock_guard lock (mutex_);
        workers_.erase (std::find (
            workers_.begin (), workers_.end(), &w));
    }

    void stop()
    {
        if (thread_.joinable())
        {
            {
                lock_guard lock (mutex_);
                stop_ = true;
            }
            cond_.notify_all();
            thread_.join();
        }
    }

    void run()
    {
        unique_lock lock (mutex_);;

        for (;;)
        {
            for (auto iter : workers_)
                iter->sample();

            using namespace std::chrono;
            clock_type::time_point const when (
                date::floor <seconds> (
                    clock_type::now().time_since_epoch()) +
                        seconds (1));

            if (cond_.wait_until (lock, when, [this]{ return stop_; }))
                return;
        }
    }

    static seconds_clock_thread& instance ()
    {
        static seconds_clock_thread singleton;
        return singleton;
    }
};

}

//------------------------------------------------------------------------------

/** Called before main exits to terminate the utility thread.
    This is a workaround for Visual Studio 2013:
    http://connect.microsoft.com/VisualStudio/feedback/details/786016/creating-a-global-c-object-that-used-thread-join-in-its-destructor-causes-a-lockup
    http://stackoverflow.com/questions/10915233/stdthreadjoin-hangs-if-called-after-main-exits-when-using-vs2012-rc
*/
inline
void
basic_seconds_clock_main_hook()
{
#ifdef _MSC_VER
    detail::seconds_clock_thread::instance().stop();
#endif
}

/** A clock whose minimum resolution is one second.

    The purpose of this class is to optimize the performance of the now()
    member function call. It uses a dedicated thread that wakes up at least
    once per second to sample the requested trivial clock.

    @tparam Clock A type meeting these requirements:
        http://en.cppreference.com/w/cpp/concept/Clock
*/
template <class Clock>
class basic_seconds_clock
{
public:
    explicit basic_seconds_clock() = default;

    using rep = typename Clock::rep;
    using period = typename Clock::period;
    using duration = typename Clock::duration;
    using time_point = typename Clock::time_point;

    static bool const is_steady = Clock::is_steady;

    static time_point now()
    {
        // Make sure the thread is constructed before the
        // worker otherwise we will crash during destruction
        // of objects with static storage duration.
        struct initializer
        {
            initializer ()
            {
                detail::seconds_clock_thread::instance();
            }
        };
        static initializer init;

        struct worker : detail::seconds_clock_worker
        {
            time_point m_now;
            std::mutex mutex_;

            worker()
                : m_now(Clock::now())
            {
                detail::seconds_clock_thread::instance().add(*this);
            }

            ~worker()
            {
                detail::seconds_clock_thread::instance().remove(*this);
            }

            time_point now()
            {
                std::lock_guard<std::mutex> lock (mutex_);
                return m_now;
            }

            void sample()
            {
                std::lock_guard<std::mutex> lock (mutex_);
                m_now = Clock::now();
            }
        };

        static worker w;

        return w.now();
    }
};

}

#endif
