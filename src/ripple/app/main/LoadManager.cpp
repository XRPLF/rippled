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

#include <ripple/app/main/Application.h>
#include <ripple/app/main/LoadManager.h>
#include <ripple/app/misc/LoadFeeTrack.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/basics/UptimeClock.h>
#include <ripple/beast/core/CurrentThreadName.h>
#include <ripple/json/to_string.h>
#include <memory>
#include <mutex>
#include <thread>

namespace ripple {

LoadManager::LoadManager(Application& app, beast::Journal journal)
    : app_(app), journal_(journal), deadLock_(), armed_(false)
{
}

LoadManager::~LoadManager()
{
    try
    {
        stop();
    }
    catch (std::exception const& ex)
    {
        // Swallow the exception in a destructor.
        JLOG(journal_.warn())
            << "std::exception in ~LoadManager.  " << ex.what();
    }
}

//------------------------------------------------------------------------------

void
LoadManager::activateDeadlockDetector()
{
    std::lock_guard sl(mutex_);
    armed_ = true;
    deadLock_ = std::chrono::steady_clock::now();
}

void
LoadManager::resetDeadlockDetector()
{
    auto const detector_start = std::chrono::steady_clock::now();
    std::lock_guard sl(mutex_);
    deadLock_ = detector_start;
}

//------------------------------------------------------------------------------

void
LoadManager::start()
{
    JLOG(journal_.debug()) << "Starting";
    assert(!thread_.joinable());

    thread_ = std::thread{&LoadManager::run, this};
}

void
LoadManager::stop()
{
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
        // There is at most one thread waiting on this condition.
        cv_.notify_all();
    }
    if (thread_.joinable())
    {
        JLOG(journal_.debug()) << "Stopping";
        thread_.join();
    }
}

//------------------------------------------------------------------------------

void
LoadManager::run()
{
    beast::setCurrentThreadName("LoadManager");

    using namespace std::chrono_literals;
    using clock_type = std::chrono::steady_clock;

    auto t = clock_type::now();

    while (true)
    {
        {
            t += 1s;
            std::unique_lock sl(mutex_);
            if (cv_.wait_until(sl, t, [this] { return stop_; }))
            {
                break;
            }
            // Copy out shared data under a lock.  Use copies outside lock.
            auto const deadLock = deadLock_;
            auto const armed = armed_;
            sl.unlock();

            // Measure the amount of time we have been deadlocked, in seconds.
            using namespace std::chrono;
            auto const timeSpentDeadlocked =
                duration_cast<seconds>(steady_clock::now() - deadLock);

            constexpr auto reportingIntervalSeconds = 10s;
            constexpr auto deadlockFatalLogMessageTimeLimit = 90s;
            constexpr auto deadlockLogicErrorTimeLimit = 600s;
            if (armed && (timeSpentDeadlocked >= reportingIntervalSeconds))
            {
                // Report the deadlocked condition every
                // reportingIntervalSeconds
                if ((timeSpentDeadlocked % reportingIntervalSeconds) == 0s)
                {
                    if (timeSpentDeadlocked < deadlockFatalLogMessageTimeLimit)
                    {
                        JLOG(journal_.warn())
                            << "Server stalled for "
                            << timeSpentDeadlocked.count() << " seconds.";
                    }
                    else
                    {
                        JLOG(journal_.fatal())
                            << "Deadlock detected. Deadlocked time: "
                            << timeSpentDeadlocked.count() << "s";
                        if (app_.getJobQueue().isOverloaded())
                        {
                            JLOG(journal_.fatal())
                                << app_.getJobQueue().getJson(0);
                        }
                    }
                }

                // If we go over the deadlockTimeLimit spent deadlocked, it
                // means that the deadlock resolution code has failed, which
                // qualifies as undefined behavior.
                //
                if (timeSpentDeadlocked >= deadlockLogicErrorTimeLimit)
                {
                    JLOG(journal_.fatal())
                        << "LogicError: Deadlock detected. Deadlocked time: "
                        << timeSpentDeadlocked.count() << "s";
                    if (app_.getJobQueue().isOverloaded())
                    {
                        JLOG(journal_.fatal()) << app_.getJobQueue().getJson(0);
                    }
                    LogicError("Deadlock detected");
                }
            }
        }

        bool change = false;
        if (app_.getJobQueue().isOverloaded())
        {
            JLOG(journal_.info()) << app_.getJobQueue().getJson(0);
            change = app_.getFeeTrack().raiseLocalFee();
        }
        else
        {
            change = app_.getFeeTrack().lowerLocalFee();
        }

        if (change)
        {
            // VFALCO TODO replace this with a Listener / observer and
            // subscribe in NetworkOPs or Application.
            app_.getOPs().reportFeeChange();
        }
    }
}

//------------------------------------------------------------------------------

std::unique_ptr<LoadManager>
make_LoadManager(Application& app, beast::Journal journal)
{
    return std::unique_ptr<LoadManager>{new LoadManager{app, journal}};
}

}  // namespace ripple
