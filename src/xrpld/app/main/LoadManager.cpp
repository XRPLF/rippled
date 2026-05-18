#include <xrpld/app/main/LoadManager.h>

#include <xrpld/app/main/Application.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/core/CurrentThreadName.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/to_string.h>  // IWYU pragma: keep
#include <xrpl/server/LoadFeeTrack.h>
#include <xrpl/server/NetworkOPs.h>

#include <chrono>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>

namespace xrpl {

LoadManager::LoadManager(Application& app, beast::Journal journal)
    : app_(app), journal_(journal), armed_(false)
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
        JLOG(journal_.warn()) << "std::exception in ~LoadManager.  " << ex.what();
    }
}

//------------------------------------------------------------------------------

void
LoadManager::activateStallDetector()
{
    std::scoped_lock const sl(mutex_);
    armed_ = true;
    lastHeartbeat_ = std::chrono::steady_clock::now();
}

void
LoadManager::heartbeat()
{
    auto const heartbeat = std::chrono::steady_clock::now();
    std::scoped_lock const sl(mutex_);
    lastHeartbeat_ = heartbeat;
}

//------------------------------------------------------------------------------

void
LoadManager::start()
{
    JLOG(journal_.debug()) << "Starting";
    XRPL_ASSERT(!thread_.joinable(), "xrpl::LoadManager::start : thread not joinable");

    thread_ = std::thread{&LoadManager::run, this};
}

void
LoadManager::stop()
{
    {
        std::scoped_lock const lock(mutex_);
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
        auto const timeSpentStalled = duration_cast<seconds>(steady_clock::now() - lastHeartbeat);

        static constexpr auto kReportingIntervalSeconds = 10s;
        static constexpr auto kStallFatalLogMessageTimeLimit = 90s;
        static constexpr auto kStallLogicErrorTimeLimit = 600s;

        if (armed && (timeSpentStalled >= kReportingIntervalSeconds))
        {
            // Report the stalled condition every reportingIntervalSeconds
            if ((timeSpentStalled % kReportingIntervalSeconds) == 0s)
            {
                if (timeSpentStalled < kStallFatalLogMessageTimeLimit)
                {
                    JLOG(journal_.warn())
                        << "Server stalled for " << timeSpentStalled.count() << " seconds.";

                    if (app_.getJobQueue().isOverloaded())
                    {
                        JLOG(journal_.warn()) << "JobQueue: " << app_.getJobQueue().getJson(0);
                    }
                }
                else
                {
                    JLOG(journal_.fatal())
                        << "Server stalled for " << timeSpentStalled.count() << " seconds.";
                    JLOG(journal_.fatal()) << "JobQueue: " << app_.getJobQueue().getJson(0);
                }
            }

            // If we go over the stallLogicErrorTimeLimit spent stalled, it
            // means that the stall resolution code has failed, which qualifies
            // as a LogicError
            if (timeSpentStalled >= kStallLogicErrorTimeLimit)
            {
                JLOG(journal_.fatal()) << "LogicError: Fatal server stall detected. Stalled time: "
                                       << timeSpentStalled.count() << "s";
                JLOG(journal_.fatal()) << "JobQueue: " << app_.getJobQueue().getJson(0);
                logicError("Fatal server stall detected");
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
makeLoadManager(Application& app, beast::Journal journal)
{
    return std::unique_ptr<LoadManager>{new LoadManager{app, journal}};
}

}  // namespace xrpl
