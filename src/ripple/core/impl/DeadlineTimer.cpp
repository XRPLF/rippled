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

#include <BeastConfig.h>
#include <ripple/core/DeadlineTimer.h>
#include <ripple/core/ThreadEntry.h>
#include <ripple/beast/clock/chrono_util.h>
#include <ripple/beast/core/Thread.h>
#include <algorithm>
#include <cassert>
#include <mutex>

namespace ripple {

class DeadlineTimer::Manager
    : protected beast::Thread
{
private:
    using Items = beast::List <DeadlineTimer>;

public:
    Manager () : beast::Thread ("DeadlineTimer::Manager")
    {
        startThread ();
    }

    ~Manager ()
    {
        signalThreadShouldExit ();
        notify ();
        waitForThreadToExit ();
        assert (m_items.empty ());
    }

    static
    Manager&
    instance()
    {
        static Manager m;
        return m;
    }

    // Okay to call on an active timer.
    // However, an extra notification may still happen due to concurrency.
    //
    void activate (DeadlineTimer& timer,
        duration recurring,
        time_point when)
    {
        using namespace std::chrono_literals;
        assert (recurring >= 0ms);

        std::lock_guard <std::recursive_mutex> lock {m_mutex};

        if (timer.m_isActive)
        {
            m_items.erase (m_items.iterator_to (timer));

            timer.m_isActive = false;
        }

        timer.recurring_ = recurring;
        timer.notificationTime_ = when;

        insertSorted (timer);
        timer.m_isActive = true;

        notify ();
    }

    // Okay to call this on an inactive timer.
    // This can happen naturally based on concurrency.
    //
    void deactivate (DeadlineTimer& timer)
    {
        std::lock_guard <std::recursive_mutex> lock {m_mutex};

        if (timer.m_isActive)
        {
            m_items.erase (m_items.iterator_to (timer));

            timer.m_isActive = false;

            notify ();
        }
    }

    void run () override
    {
        threadEntry (
            this, &Manager::runImpl, "DeadlineTimer::Manager::run()");
    }

    void runImpl ()
    {
        using namespace std::chrono;
        while (! threadShouldExit ())
        {
            auto const currentTime = time_point_cast<duration>(clock::now());

            auto nextDeadline = currentTime;
            DeadlineTimer* timer {nullptr};

            {
                std::lock_guard <std::recursive_mutex> lock {m_mutex};

                // See if a timer expired
                if (! m_items.empty ())
                {
                    timer = &m_items.front ();

                    // Has this timer expired?
                    if (timer->notificationTime_ <= currentTime)
                    {
                        // Expired, remove it from the list.
                        assert (timer->m_isActive);
                        m_items.pop_front ();

                        // Is the timer recurring?
                        if (timer->recurring_ > 0ms)
                        {
                            // Yes so set the timer again.
                            timer->notificationTime_ =
                                currentTime + timer->recurring_;

                            // Put it back into the list as active
                            insertSorted (*timer);
                        }
                        else
                        {
                            // Not a recurring timer, deactivate it.
                            timer->m_isActive = false;
                        }

                        // NOTE!  Called *inside* lock!
                        timer->m_listener->onDeadlineTimer (*timer);

                        // re-loop
                        nextDeadline = currentTime - 1s;
                    }
                    else
                    {
                        nextDeadline = timer->notificationTime_;

                        // Can't be zero and come into the else clause.
                        assert (nextDeadline > currentTime);

                        // Don't call the listener
                        timer = nullptr;
                    }
                }
            }

            // Note that we have released the lock here.

            if (nextDeadline > currentTime)
            {
                // Wait until interrupt or next timer.
                //
                auto const waitCountMilliSeconds = nextDeadline - currentTime;
                static_assert(
                    std::ratio_equal<decltype(waitCountMilliSeconds)::period,
                    std::milli>::value,
                    "Call to wait() requires units of milliseconds.");

                wait (static_cast<int>(waitCountMilliSeconds.count()));
            }
            else if (nextDeadline == currentTime)
            {
                // Wait until interrupt
                //
                wait ();
            }
            else
            {
                // Do not wait. This can happen if the recurring timer duration
                // is extremely short, or if a listener wastes too much time in
                // their callback.
            }
        }
    }

    // Caller is responsible for locking
    void insertSorted (DeadlineTimer& timer)
    {
        if (! m_items.empty ())
        {
            Items::iterator before {m_items.begin()};

            for (;;)
            {
                if (before->notificationTime_ >= timer.notificationTime_)
                {
                    m_items.insert (before, timer);
                    break;
                }

                ++before;

                if (before == m_items.end ())
                {
                    m_items.push_back (timer);
                    break;
                }
            }
        }
        else
        {
            m_items.push_back (timer);
        }
    }

private:
    std::recursive_mutex m_mutex;
    Items m_items;
};

//------------------------------------------------------------------------------

DeadlineTimer::DeadlineTimer (Listener* listener)
    : m_listener (listener)
    , m_isActive (false)
{
}

DeadlineTimer::~DeadlineTimer ()
{
    Manager::instance().deactivate (*this);
}

void DeadlineTimer::cancel ()
{
    Manager::instance().deactivate (*this);
}

void DeadlineTimer::setExpiration (std::chrono::milliseconds delay)
{
    using namespace std::chrono;
    assert (delay > 0ms);

    auto const when = time_point_cast<duration>(clock::now() + delay);

    Manager::instance().activate (*this, 0ms, when);
}

void DeadlineTimer::setRecurringExpiration (std::chrono::milliseconds interval)
{
    using namespace std::chrono;
    assert (interval > 0ms);

    auto const when = time_point_cast<duration>(clock::now() + interval);

    Manager::instance().activate (*this, interval, when);
}

} // ripple
