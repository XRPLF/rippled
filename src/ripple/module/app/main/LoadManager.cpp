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

#include <boost/thread.hpp>

namespace ripple {

class LoadManagerImp
    : public LoadManager
    , public beast::Thread
{
public:
    /*  Entry mapping utilization to cost.

        The cost is expressed as a unitless relative quantity. These
        mappings are statically loaded at startup with heuristic values.
    */
    class Cost
    {
    public:
        // VFALCO TODO Eliminate this default ctor somehow
        Cost ()
            : m_loadType ()
            , m_cost (0)
            , m_resourceFlags (0)
        {
        }
        
        Cost (LoadType loadType, int cost, int resourceFlags)
            : m_loadType (loadType)
            , m_cost (cost)
            , m_resourceFlags (resourceFlags)
        {
        }

        LoadType getLoadType () const
        {
            return m_loadType;
        }

        int getCost () const
        {
            return m_cost;
        }

        int getResourceFlags () const
        {
            return m_resourceFlags;
        }

    public:
        // VFALCO TODO Make these private and const
        LoadType    m_loadType;
        int         m_cost;
        int         m_resourceFlags;
    };

    //--------------------------------------------------------------------------

    beast::Journal m_journal;
    typedef RippleMutex LockType;
    typedef std::lock_guard <LockType> ScopedLockType;
    LockType mLock;

    bool mArmed;

    int mDeadLock;              // Detect server deadlocks

    std::vector <Cost> mCosts;

    //--------------------------------------------------------------------------

    LoadManagerImp (Stoppable& parent, beast::Journal journal)
        : LoadManager (parent)
        , Thread ("loadmgr")
        , m_journal (journal)
        , mArmed (false)
        , mDeadLock (0)
        , mCosts (LT_MAX)
    {
        UptimeTimer::getInstance ().beginManualUpdates ();
    }

    ~LoadManagerImp ()
    {
        UptimeTimer::getInstance ().endManualUpdates ();

        stopThread ();
    }

    //--------------------------------------------------------------------------
    //
    // Stoppable
    //
    //--------------------------------------------------------------------------

    void onPrepare ()
    {
    }

    void onStart ()
    {
        m_journal.debug << "Starting";
        startThread ();
    }

    void onStop ()
    {
        if (isThreadRunning ())
        {
            m_journal.debug << "Stopping";
            stopThreadAsync ();
        }
        else
        {
            stopped ();
        }
    }

    //--------------------------------------------------------------------------

    void resetDeadlockDetector ()
    {
        ScopedLockType sl (mLock);
        mDeadLock = UptimeTimer::getInstance ().getElapsedSeconds ();
    }

    void activateDeadlockDetector ()
    {
        mArmed = true;
    }

    void logDeadlock (int dlTime)
    {
        m_journal.warning << "Server stalled for " << dlTime << " seconds.";
    }

    // VFALCO NOTE Where's the thread object? It's not a data member...
    //
    void run ()
    {
        // VFALCO TODO replace this with a beast Time object?
        //
        // Initialize the clock to the current time.
        boost::posix_time::ptime t = boost::posix_time::microsec_clock::universal_time ();

        while (! threadShouldExit ())
        {
            {
                // VFALCO NOTE What is this lock protecting?
                ScopedLockType sl (mLock);

                // VFALCO NOTE I think this is to reduce calls to the operating system
                //             for retrieving the current time.
                //
                //        TODO Instead of incrementing can't we just retrieve the current
                //             time again?
                //
                // Manually update the timer.
                UptimeTimer::getInstance ().incrementElapsedTime ();

                // Measure the amount of time we have been deadlocked, in seconds.
                //
                // VFALCO NOTE mDeadLock is a canary for detecting the condition.
                int const timeSpentDeadlocked = UptimeTimer::getInstance ().getElapsedSeconds () - mDeadLock;

                // VFALCO NOTE I think that "armed" refers to the deadlock detector
                //
                int const reportingIntervalSeconds = 10;
                if (mArmed && (timeSpentDeadlocked >= reportingIntervalSeconds))
                {
                    // Report the deadlocked condition every 10 seconds
                    if ((timeSpentDeadlocked % reportingIntervalSeconds) == 0)
                    {
                        logDeadlock (timeSpentDeadlocked);
                    }

                    // If we go over 500 seconds spent deadlocked, it means that the
                    // deadlock resolution code has failed, which qualifies as undefined
                    // behavior.
                    //
                    assert (timeSpentDeadlocked < 500);
                }
            }

            bool change;

            // VFALCO TODO Eliminate the dependence on the Application object.
            //             Choices include constructing with the job queue / feetracker.
            //             Another option is using an observer pattern to invert the dependency.
            if (getApp().getJobQueue ().isOverloaded ())
            {
                m_journal.info << getApp().getJobQueue ().getJson (0);
                change = getApp().getFeeTrack ().raiseLocalFee ();
            }
            else
            {
                change = getApp().getFeeTrack ().lowerLocalFee ();
            }

            if (change)
            {
                // VFALCO TODO replace this with a Listener / observer and subscribe in NetworkOPs or Application
                getApp().getOPs ().reportFeeChange ();
            }

            t += boost::posix_time::seconds (1);
            boost::posix_time::time_duration when = t - boost::posix_time::microsec_clock::universal_time ();

            if ((when.is_negative ()) || (when.total_seconds () > 1))
            {
                m_journal.warning << "time jump";
                t = boost::posix_time::microsec_clock::universal_time ();
            }
            else
            {
                boost::this_thread::sleep (when);
            }
        }

        stopped ();
    }
};

//------------------------------------------------------------------------------

LoadManager::LoadManager (Stoppable& parent)
    : Stoppable ("LoadManager", parent)
{
}

//------------------------------------------------------------------------------

LoadManager* LoadManager::New (Stoppable& parent, beast::Journal journal)
{
    return new LoadManagerImp (parent, journal);
}

} // ripple
