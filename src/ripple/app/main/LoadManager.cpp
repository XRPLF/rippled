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
#include <ripple/app/main/LoadManager.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/basics/UptimeTimer.h>
#include <ripple/core/JobQueue.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/json/to_string.h>
#include <beast/threads/Thread.h>
#include <beast/cxx14/memory.h> // <memory>
#include <mutex>
#include <thread>

namespace ripple {

class LoadManagerImp
    : public LoadManager
    , public beast::Thread
{
public:
    using LockType = std::mutex;
    using ScopedLockType = std::lock_guard <LockType>;

    Application& app_;
    beast::Journal m_journal;
    LockType mLock;

    bool mArmed;

    int mDeadLock;              // Detect server deadlocks
    //--------------------------------------------------------------------------

    LoadManagerImp (Application& app,
            Stoppable& parent, beast::Journal journal)
        : LoadManager (parent)
        , Thread ("loadmgr")
        , app_ (app)
        , m_journal (journal)
        , mArmed (false)
        , mDeadLock (0)
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
        using clock_type = std::chrono::steady_clock;

        // Initialize the clock to the current time.
        auto t = clock_type::now();

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
            if (app_.getJobQueue ().isOverloaded ())
            {
                if (m_journal.info)
                    m_journal.info << app_.getJobQueue ().getJson (0);
                change = app_.getFeeTrack ().raiseLocalFee ();
            }
            else
            {
                change = app_.getFeeTrack ().lowerLocalFee ();
            }

            if (change)
            {
                // VFALCO TODO replace this with a Listener / observer and subscribe in NetworkOPs or Application
                app_.getOPs ().reportFeeChange ();
            }

            t += std::chrono::seconds (1);
            auto const duration = t - clock_type::now();

            if ((duration < std::chrono::seconds (0)) || (duration > std::chrono::seconds (1)))
            {
                m_journal.warning << "time jump";
                t = clock_type::now();
            }
            else
            {
                std::this_thread::sleep_for (duration);
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

std::unique_ptr<LoadManager>
make_LoadManager (Application& app,
    beast::Stoppable& parent, beast::Journal journal)
{
    return std::make_unique<LoadManagerImp>(app, parent, journal);
}

} // ripple
