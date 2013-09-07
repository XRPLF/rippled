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
    , protected Thread
{
private:
    typedef CriticalSection LockType;
    typedef List <DeadlineTimer> Items;

public:
    Manager ()
        : SharedSingleton <Manager> (SingletonLifetime::persistAfterCreation)
        , Thread ("DeadlineTimer::Manager")
    {
        startThread ();
    }

    ~Manager ()
    {
        signalThreadShouldExit ();
        notify ();
        waitForThreadToExit ();
        bassert (m_items.empty ());
    }

    // Okay to call on an active timer.
    // However, an extra notification may still happen due to concurrency.
    //
    void activate (DeadlineTimer& timer, double secondsRecurring, Time const& when)
    {
        bassert (secondsRecurring >= 0);

        LockType::ScopedLockType lock (m_mutex);

        if (timer.m_isActive)
        {
            m_items.erase (m_items.iterator_to (timer));

            timer.m_isActive = false;
        }

        timer.m_secondsRecurring = secondsRecurring;
        timer.m_notificationTime = when;

        insertSorted (timer);
        timer.m_isActive = true;

        notify ();
    }

    // Okay to call this on an inactive timer.
    // This can happen naturally based on concurrency.
    //
    void deactivate (DeadlineTimer& timer)
    {
        LockType::ScopedLockType lock (m_mutex);

        if (timer.m_isActive)
        {
            m_items.erase (m_items.iterator_to (timer));

            timer.m_isActive = false;

            notify ();
        }
    }

    void run ()
    {
        while (! threadShouldExit ())
        {
            Time const currentTime = Time::getCurrentTime ();
            
            double seconds = 0;

            {
                LockType::ScopedLockType lock (m_mutex);

                // Notify everyone whose timer has expired
                //
                while (! m_items.empty ())
                {
                    Items::iterator const iter = m_items.begin ();
                    DeadlineTimer& timer (*iter);

                    // Has this timer expired?
                    if (timer.m_notificationTime <= currentTime)
                    {
                        // Expired, remove it from the list.
                        m_items.erase (iter);

                        // Call the listener
                        //
                        // NOTE
                        //      The lock is held.
                        //      The listener MUST NOT block for long.
                        //
                        timer.m_listener->onDeadlineTimer (timer);

                        // Is the timer recurring?
                        if (timer.m_secondsRecurring > 0)
                        {
                            // Yes so set the timer again.
                            timer.m_notificationTime =
                                currentTime + RelativeTime (iter->m_secondsRecurring);

                            // Keep it active.
                            insertSorted (timer);
                        }
                        else
                        {
                            // Not a recurring timer, deactivate it.
                            timer.m_isActive = false;
                        }
                    }
                    else
                    {
                        break;
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
                wait (static_cast <int> (seconds * 1000 + 0.5));
            }
            else if (seconds == 0)
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
            Items::iterator before = m_items.begin ();

            for (;;)
            {
                if (before->m_notificationTime >= timer.m_notificationTime)
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

    static Manager* createInstance ()
    {
        return new Manager;
    }

private:
    CriticalSection m_mutex;
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
    m_manager->deactivate (*this);
}

void DeadlineTimer::setExpiration (double secondsUntilDeadline)
{
    bassert (secondsUntilDeadline > 0);

    Time const when = Time::getCurrentTime () + RelativeTime (secondsUntilDeadline);

    m_manager->activate (*this, 0, when);
}

void DeadlineTimer::setRecurringExpiration (double secondsUntilDeadline)
{
    bassert (secondsUntilDeadline > 0);

    Time const when = Time::getCurrentTime () + RelativeTime (secondsUntilDeadline);

    m_manager->activate (*this, secondsUntilDeadline, when);
}

void DeadlineTimer::setExpirationTime (Time const& when)
{
    m_manager->activate (*this, 0, when);
}

void DeadlineTimer::reset ()
{
    m_manager->deactivate (*this);
}
