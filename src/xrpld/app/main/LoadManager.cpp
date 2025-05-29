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

#include <xrpld/app/main/Application.h>
#include <xrpld/app/main/LoadManager.h>
#include <xrpld/app/misc/LoadFeeTrack.h>
#include <xrpld/app/misc/NetworkOPs.h>

#include <xrpl/beast/core/CurrentThreadName.h>
#include <xrpl/json/to_string.h>

#include <memory>
#include <mutex>
#include <thread>

namespace ripple {

LoadManager::LoadManager(Application& app, beast::Journal journal)
    : app_(app), journal_(journal), lastHeartbeat_(), armed_(false)
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
LoadManager::activateStallDetector()
{
    std::lock_guard sl(mutex_);
    armed_ = true;
    lastHeartbeat_ = std::chrono::steady_clock::now();
}

void
LoadManager::heartbeat()
{
    auto const heartbeat = std::chrono::steady_clock::now();
    std::lock_guard sl(mutex_);
    lastHeartbeat_ = heartbeat;
}

//------------------------------------------------------------------------------

void
LoadManager::start()
{
    JLOG(journal_.debug()) << "Starting";
    XRPL_ASSERT(
        !thread_.joinable(),
        "ripple::LoadManager::start : thread not joinable");

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
        t += 1s;

        std::unique_lock sl(mutex_);
        if (cv_.wait_until(sl, t, [this] { return stop_; }))
            break;

        // Copy out shared data under a lock.  Use copies outside lock.
        auto const lastHeartbeat = lastHeartbeat_;
        auto const armed = armed_;
        sl.unlock();

        // Measure the amount of time we have been stalled, in seconds.
        using namespace std::chrono;
        auto const timeSpentStalled =
            duration_cast<seconds>(steady_clock::now() - lastHeartbeat);

        constexpr auto reportingIntervalSeconds = 10s;
        constexpr auto stallFatalLogMessageTimeLimit = 90s;
        constexpr auto stallLogicErrorTimeLimit = 600s;

        if (armed && (timeSpentStalled >= reportingIntervalSeconds))
        {
            // Report the stalled condition every reportingIntervalSeconds
            if ((timeSpentStalled % reportingIntervalSeconds) == 0s)
            {
                if (timeSpentStalled < stallFatalLogMessageTimeLimit)
                {
                    JLOG(journal_.warn())
                        << "Server stalled for " << timeSpentStalled.count()
                        << " seconds.";

                    if (app_.getJobQueue().isOverloaded())
                    {
                        JLOG(journal_.warn())
                            << "JobQueue: " << app_.getJobQueue().getJson(0);
                    }
                }
                else
                {
                    JLOG(journal_.fatal())
                        << "Server stalled for " << timeSpentStalled.count()
                        << " seconds.";
                    JLOG(journal_.fatal())
                        << "JobQueue: " << app_.getJobQueue().getJson(0);
                }
            }

            // If we go over the stallLogicErrorTimeLimit spent stalled, it
            // means that the stall resolution code has failed, which qualifies
            // as a LogicError
            if (timeSpentStalled >= stallLogicErrorTimeLimit)
            {
                JLOG(journal_.fatal())
                    << "LogicError: Fatal server stall detected. Stalled time: "
                    << timeSpentStalled.count() << "s";
                JLOG(journal_.fatal())
                    << "JobQueue: " << app_.getJobQueue().getJson(0);
                LogicError("Fatal server stall detected");
            }
        }
    }

    bool change = false;
    if (app_.getJobQueue().isOverloaded())
    {
        JLOG(journal_.info()) << "Raising local fee (JQ overload): "
                              << app_.getJobQueue().getJson(0);
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

//------------------------------------------------------------------------------

std::unique_ptr<LoadManager>
make_LoadManager(Application& app, beast::Journal journal)
{
    return std::unique_ptr<LoadManager>{new LoadManager{app, journal}};
}

}  // namespace ripple
