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

class DeadlineTimer::Manager
    : public SharedSingleton <DeadlineTimer::Manager>
    , public InterruptibleThread::EntryPoint
{
private:
    typedef CriticalSection LockType;
    typedef List <DeadlineTimer> Items;

public:
    Manager ()
        : SharedSingleton <Manager> (SingletonLifetime::persistAfterCreation)
        , m_shouldStop (false)
        , m_thread ("DeadlineTimer::Manager")
    {
        m_thread.start (this);
    }

    ~Manager ()
    {
        m_shouldStop = true;

        m_thread.interrupt ();

        bassert (m_items.empty ());
    }

    void activate (DeadlineTimer* timer)
    {
        LockType::ScopedLockType lock (m_mutex);

        bassert (! timer->m_isActive);

        insertSorted (*timer);
        timer->m_isActive = true;

        m_thread.interrupt ();
    }

    // Okay to call this on an inactive timer.
    // This can happen naturally based on concurrency.
    //
    void deactivate (DeadlineTimer* timer)
    {
        LockType::ScopedLockType lock (m_mutex);

        if (timer->m_isActive)
        {
            m_items.erase (m_items.iterator_to (*timer));

            timer->m_isActive = false;
        }

        m_thread.interrupt ();
    }

    void threadRun ()
    {
        while (! m_shouldStop)
        {
            Time const currentTime = Time::getCurrentTime ();
            double seconds = 0;

            {
                LockType::ScopedLockType lock (m_mutex);

                // Notify everyone whose timer has expired
                //
                if (! m_items.empty ())
                {
                    for (;;)
                    {
                        Items::iterator const iter = m_items.begin ();

                        // Has this timer expired?
                        if (iter->m_notificationTime <= currentTime)
                        {
                            // Yes, so call the listener.
                            //
                            // Note that this happens while the lock is held.
                            //
                            iter->m_listener->onDeadlineTimer (*iter);

                            // Remove it from the list.
                            m_items.erase (iter);

                            // Is the timer recurring?
                            if (iter->m_secondsRecurring > 0)
                            {
                                // Yes so set the timer again.
                                iter->m_notificationTime =
                                    currentTime + RelativeTime (iter->m_secondsRecurring);

                                // Keep it active.
                                insertSorted (*iter);
                            }
                            else
                            {
                                // Not a recurring timer, deactivate it.
                                iter->m_isActive = false;
                            }
                        }
                        else
                        {
                            break;
                        }
                    }
                }

                // Figure out how long we need to wait.
                // This has to be done while holding the lock.
                //
                if (! m_items.empty ())
                {
                    seconds = (m_items.front ().m_notificationTime - currentTime).inSeconds ();
                }
                else
                {
                    seconds = 0;
                }
            }

            // Note that we have released the lock here.
            //
            if (seconds > 0)
            {
                // Wait until interrupt or next timer.
                //
                m_thread.wait (static_cast <int> (seconds * 1000 + 0.5));
            }
            else if (seconds == 0)
            {
                // Wait until interrupt
                //
                m_thread.wait ();
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
    void insertSorted (DeadlineTimer& item)
    {
        if (! m_items.empty ())
        {
            Items::iterator before = m_items.begin ();

            for (;;)
            {
                if (before->m_notificationTime >= item.m_notificationTime)
                {
                    m_items.insert (before, item);
                    break;
                }

                ++before;

                if (before == m_items.end ())
                {
                    m_items.push_back (item);
                    break;
                }
            }
        }
        else
        {
            m_items.push_back (item);
        }
    }

    static Manager* createInstance ()
    {
        return new Manager;
    }

private:
    CriticalSection m_mutex;
    bool volatile m_shouldStop;
    InterruptibleThread m_thread;
    Items m_items;
};

//------------------------------------------------------------------------------

DeadlineTimer::DeadlineTimer (Listener* listener)
    : m_listener (listener)
    , m_manager (Manager::getInstance ())
    , m_isActive (false)
{
}

DeadlineTimer::~DeadlineTimer ()
{
    m_manager->deactivate (this);
}

void DeadlineTimer::setExpiration (double secondsUntilDeadline)
{
    m_secondsRecurring = 0;
    m_notificationTime = Time::getCurrentTime () + RelativeTime (secondsUntilDeadline);

    m_manager->activate (this);
}

void DeadlineTimer::setRecurringExpiration (double secondsUntilDeadline)
{
    m_secondsRecurring = secondsUntilDeadline;
    m_notificationTime = Time::getCurrentTime () + RelativeTime (secondsUntilDeadline);

    m_manager->activate (this);
}

void DeadlineTimer::setExpirationTime (Time absoluteDeadline)
{
    m_secondsRecurring = 0;
    m_notificationTime = absoluteDeadline;

    m_manager->activate (this);
}

void DeadlineTimer::reset ()
{
    m_manager->deactivate (this);
}
