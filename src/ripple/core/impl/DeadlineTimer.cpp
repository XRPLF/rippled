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
#include <ripple/basics/contract.h>
#include <cassert>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace ripple {

class DeadlineTimer::Manager
{
private:
    using Items = beast::List <DeadlineTimer>;

    // Use RAII to manage our recursion counter.
    //
    // NOTE: Creation of any lock(mutex_) should be immediately followed
    // by constructing a named CountRecursion.  Otherwise the mutex recursion
    // tracking will be faulty.
    class CountRecursion
    {
        int& counter_;

    public:
        CountRecursion (CountRecursion const&) = delete;
        CountRecursion& operator=(CountRecursion const&) = delete;

        explicit CountRecursion (int& counter)
        : counter_ {counter}
        {
            ++counter_;
        }

        ~CountRecursion()
        {
            --counter_;
        }
    };

    Manager ()
    {
        thread_ = std::thread {&Manager::run, this};
    }

    ~Manager ()
    {
        {
            std::lock_guard<std::recursive_mutex> lock {mutex_};
            CountRecursion c {recursionCount_};
            shouldExit_ = true;
            wakeup_.notify_one();
        }
        thread_.join();
        assert (m_items.empty ());
    }

public:
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

        std::lock_guard <std::recursive_mutex> lock {mutex_};
        CountRecursion c {recursionCount_};

        if (timer.m_isActive)
        {
            m_items.erase (m_items.iterator_to (timer));

            timer.m_isActive = false;
        }

        timer.recurring_ = recurring;
        timer.notificationTime_ = when;

        insertSorted (timer);
        timer.m_isActive = true;

        wakeup_.notify_one();
    }

    // Okay to call this on an inactive timer.
    // This can happen naturally based on concurrency.
    //
    void deactivate (DeadlineTimer& timer)
    {
        std::lock_guard <std::recursive_mutex> lock {mutex_};
        CountRecursion c {recursionCount_};

        if (timer.m_isActive)
        {
            m_items.erase (m_items.iterator_to (timer));

            timer.m_isActive = false;

            wakeup_.notify_one();
        }
    }

    void run ()
    {
        threadEntry (
            this, &Manager::runImpl, "DeadlineTimer::Manager::run()");
    }

    void runImpl ()
    {
        using namespace std::chrono;
        bool shouldExit = true;

        do
        {
            {
                auto const currentTime =
                    time_point_cast<duration>(clock::now());
                auto nextDeadline = currentTime;

                std::unique_lock <std::recursive_mutex> lock {mutex_};
                CountRecursion c {recursionCount_};

                // See if a timer expired
                if (!shouldExit_ && !m_items.empty ())
                {
                    DeadlineTimer* const timer = &m_items.front ();

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

                        // Given the current code structure this call must
                        // happen inside the lock.  Once the lock is released
                        // the timer might be canceled and it would be invalid
                        // to call timer->m_listener.
                        timer->m_listener->onDeadlineTimer (*timer);

                        // re-loop
                        nextDeadline = currentTime - 1s;
                    }
                    else
                    {
                        // Timer has not yet expired.
                        nextDeadline = timer->notificationTime_;

                        // Can't be zero and come into the else clause.
                        assert (nextDeadline > currentTime);
                    }
                }

                if (!shouldExit_)
                {
                    // It's bad news to invoke std::condition_variable_any
                    // wait() or wait_until() on a recursive_mutex if the
                    // recursion depth is greater than one.  That's because
                    // wait() and wait_until() will only release one level
                    // of lock.
                    //
                    // We believe that the lock recursion depth can only be
                    // one at this point in the code, given the current code
                    // structure (December 2016).  Here's why:
                    //
                    //  1. The DeadlineTimer::Manager runs exclusively on its
                    //     own dedicated thread. This is the only thread where
                    //     wakeup_.wait() or wakeup_.wait_until() are called.
                    //
                    //  2. So in order for the recursive_mutex to be called
                    //     recursively, it must result from the call through
                    //     timer->m_listener->onDeadlineTimer (*timer).
                    //
                    //  3. Any callback into DeadlineTimer from a Listener
                    //     may do one of two things: a call to activate() or
                    //     a call to deactivate(). Either of these will invoke
                    //     the lock recursively. Then they both invoke
                    //     condition_variable_any wakeup_.notify_one() under
                    //     the recursive lock. Then they release the recursive
                    //     lock. Once this local lock release occurs the
                    //     recursion depth should be back to one.
                    //
                    //  4. So, once the Listener callback completes then the
                    //     recursive_lock is no longer recursively held. That
                    //     means when we enter the wakeup_.wait() or the
                    //     wakeup_.wait_until() the lock is never held
                    //     recursively.
                    //
                    // In case that analysis is, or becomes, incorrect the
                    // following LogicError should fire.
                    if (recursionCount_ != 1)
                        LogicError ("DeadlineTimer mutex recursion violation.");

                    if (nextDeadline > currentTime)
                        // Wake up at the next deadline or next notify.
                        // Cast to clock::duration to work around VS-2015 bug.
                        // Harmless on other platforms.
                        wakeup_.wait_until (lock,
                            time_point_cast<clock::duration>(nextDeadline));

                    else if (nextDeadline == currentTime)
                        // There is no deadline.  Wake up at the next notify.
                        wakeup_.wait (lock);

                    else;
                        // Do not wait. This can happen if the recurring
                        // timer duration is extremely short or if a listener
                        // burns lots of time in their callback.
                }
                // shouldExit is used outside the lock.
                shouldExit = shouldExit_;
            } // Note that we release the lock here.

        } while (!shouldExit);
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
    std::recursive_mutex mutex_;
    std::condition_variable_any wakeup_;  // Works with std::recursive_mutex.
    std::thread thread_;
    bool shouldExit_ {false};
    int recursionCount_ {0};

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
