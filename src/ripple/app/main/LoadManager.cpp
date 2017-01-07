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
#include <ripple/app/misc/LoadFeeTrack.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/basics/UptimeTimer.h>
#include <ripple/json/to_string.h>
#include <ripple/beast/core/CurrentThreadName.h>
#include <memory>
#include <mutex>
#include <thread>

namespace ripple {

LoadManager::LoadManager (
    Application& app, Stoppable& parent, beast::Journal journal)
    : Stoppable ("LoadManager", parent)
    , app_ (app)
    , journal_ (journal)
    , deadLock_ (0)
    , armed_ (false)
    , stop_ (false)
{
    UptimeTimer::getInstance ().beginManualUpdates ();
}

LoadManager::~LoadManager ()
{
    try
    {
        UptimeTimer::getInstance ().endManualUpdates ();
        onStop ();
    }
    catch (std::exception const& ex)
    {
        // Swallow the exception in a destructor.
        JLOG(journal_.warn()) << "std::exception in ~LoadManager.  "
            << ex.what();
    }
}

//------------------------------------------------------------------------------

void LoadManager::activateDeadlockDetector ()
{
    std::lock_guard<std::mutex> sl (mutex_);
    armed_ = true;
}

void LoadManager::resetDeadlockDetector ()
{
    auto const elapsedSeconds =
        UptimeTimer::getInstance ().getElapsedSeconds ();

    std::lock_guard<std::mutex> sl (mutex_);
    deadLock_ = elapsedSeconds;
}

//------------------------------------------------------------------------------

void LoadManager::onPrepare ()
{
}

void LoadManager::onStart ()
{
    JLOG(journal_.debug()) << "Starting";
    assert (! thread_.joinable());

    thread_ = std::thread {&LoadManager::run, this};
}

void LoadManager::onStop ()
{
    if (thread_.joinable())
    {
        JLOG(journal_.debug()) << "Stopping";
        {
            std::lock_guard<std::mutex> sl (mutex_);
            stop_ = true;
        }
        thread_.join();
    }
    stopped ();
}

//------------------------------------------------------------------------------

void LoadManager::run ()
{
    beast::setCurrentThreadName ("LoadManager");

    using clock_type = std::chrono::steady_clock;

    // Initialize the clock to the current time.
    auto t = clock_type::now();
    bool stop = false;

    while (! (stop || isStopping ()))
    {
        {
            // VFALCO NOTE I think this is to reduce calls to the operating
            //             system for retrieving the current time.
            //
            //        TODO Instead of incrementing can't we just retrieve the
            //             current time again?
            //
            // Manually update the timer.
            UptimeTimer::getInstance ().incrementElapsedTime ();

            // Copy out shared data under a lock.  Use copies outside lock.
            std::unique_lock<std::mutex> sl (mutex_);
            auto const deadLock = deadLock_;
            auto const armed = armed_;
            stop = stop_;
            sl.unlock();

            // Measure the amount of time we have been deadlocked, in seconds.
            //
            // VFALCO NOTE deadLock_ is a canary for detecting the condition.
            int const timeSpentDeadlocked =
                UptimeTimer::getInstance ().getElapsedSeconds () - deadLock;

            // VFALCO NOTE I think that "armed" refers to the deadlock detector.
            //
            int const reportingIntervalSeconds = 10;
            if (armed && (timeSpentDeadlocked >= reportingIntervalSeconds))
            {
                // Report the deadlocked condition every 10 seconds
                if ((timeSpentDeadlocked % reportingIntervalSeconds) == 0)
                {
                    JLOG(journal_.warn())
                        << "Server stalled for "
                        << timeSpentDeadlocked << " seconds.";
                }

                // If we go over 500 seconds spent deadlocked, it means that
                // the deadlock resolution code has failed, which qualifies
                // as undefined behavior.
                //
                assert (timeSpentDeadlocked < 500);
            }
        }

        bool change = false;
        if (app_.getJobQueue ().isOverloaded ())
        {
            JLOG(journal_.info()) << app_.getJobQueue ().getJson (0);
            change = app_.getFeeTrack ().raiseLocalFee ();
        }
        else
        {
            change = app_.getFeeTrack ().lowerLocalFee ();
        }

        if (change)
        {
            // VFALCO TODO replace this with a Listener / observer and
            // subscribe in NetworkOPs or Application.
            app_.getOPs ().reportFeeChange ();
        }

        t += std::chrono::seconds (1);
        auto const duration = t - clock_type::now();

        if ((duration < std::chrono::seconds (0)) ||
            (duration > std::chrono::seconds (1)))
        {
            JLOG(journal_.warn()) << "time jump";
            t = clock_type::now();
        }
        else
        {
            alertable_sleep_for(duration);
        }
    }

    stopped ();
}

//------------------------------------------------------------------------------

std::unique_ptr<LoadManager>
make_LoadManager (Application& app,
    Stoppable& parent, beast::Journal journal)
{
    return std::make_unique<LoadManager>(app, parent, journal);
}

} // ripple
