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
};

//------------------------------------------------------------------------------

// Updates the clocks
class seconds_clock_thread
{
public:
    typedef std::mutex mutex;
    typedef std::condition_variable cond_var;
    typedef std::lock_guard <mutex> lock_guard;
    typedef std::unique_lock <mutex> unique_lock;
    typedef std::chrono::steady_clock clock_type;
    typedef std::chrono::seconds seconds;
    typedef std::thread thread;
    typedef std::vector <seconds_clock_worker*> workers;

    bool m_stop;
    mutex m_mutex;
    cond_var m_cond;
    workers m_workers;
    thread m_thread;

    seconds_clock_thread ()
        : m_stop (false)
    {
        m_thread = thread (std::bind(
            &seconds_clock_thread::run, this));
    }

    ~seconds_clock_thread ()
    {
        stop();
    }

    void add (seconds_clock_worker& w)
    {
        lock_guard lock (m_mutex);
        m_workers.push_back (&w);
    }

    void remove (seconds_clock_worker& w)
    {
        lock_guard lock (m_mutex);
        m_workers.erase (std::find (
            m_workers.begin (), m_workers.end(), &w));
    }

    void stop()
    {
        if (m_thread.joinable())
        {
            {
                lock_guard lock (m_mutex);
                m_stop = true;
            }
            m_cond.notify_all();
            m_thread.join();
        }
    }

    void run()
    {
        unique_lock lock (m_mutex);;

        for (;;)
        {
            for (auto iter : m_workers)
                iter->sample();

            clock_type::time_point const when (
                floor <seconds> (
                    clock_type::now().time_since_epoch()) +
                        seconds (1));

            if (m_cond.wait_until (lock, when, [this]{ return m_stop; }))
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
    @tparam TrivialClock The clock to sample.
*/
template <class TrivialClock>
class basic_seconds_clock
{
public:
    typedef std::chrono::seconds resolution;
    typedef typename resolution::rep rep;
    typedef typename resolution::period period;
    typedef std::chrono::duration <rep, period> duration;
    typedef std::chrono::time_point <basic_seconds_clock> time_point;

    static bool const is_steady = TrivialClock::is_steady;

    static time_point now ()
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
            typedef std::mutex mutex;
            typedef std::lock_guard <mutex> lock_guard;

            time_point m_now;
            mutex m_mutex;

            static time_point get_now ()
            {
                return time_point (floor <resolution> (
                    TrivialClock::now().time_since_epoch()));
            }

            worker ()
                : m_now (get_now ())
            {
                detail::seconds_clock_thread::instance().add (*this);
            }

            ~worker ()
            {
                detail::seconds_clock_thread::instance().remove (*this);
            }

            time_point now()
            {
                lock_guard lock (m_mutex);
                return m_now;
            }

            void sample ()
            {
                lock_guard lock (m_mutex);
                m_now = get_now ();
            }
        };

        static worker w;

        return w.now ();
    }
};

}

#endif
