#pragma once

#include <xrpld/rpc/detail/Handler.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobTypes.h>
#include <xrpl/core/PerfLog.h>
#include <xrpl/json/json_value.h>

#include <boost/asio/ip/host_name.hpp>

#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace xrpl::perf {

/**
 * A box coupling data with a mutex for locking access to it.
 */
template <typename T>
struct Locked
{
    T value;
    mutable std::mutex mutex;

    Locked() = default;
    Locked(T const& value) : value(value)
    {
    }
    Locked(T&& value) : value(std::move(value))
    {
    }
    Locked(Locked const& rhs) : value(rhs.value)
    {
    }
    Locked(Locked&& rhs) : value(std::move(rhs.value))
    {
    }
};

/**
 * Implementation class for PerfLog.
 */
class PerfLogImp : public PerfLog
{
    /**
     * Track performance counters and currently executing tasks.
     */
    struct Counters
    {
    public:
        using MethodStart = std::pair<char const*, SteadyTimePoint>;
        /**
         * RPC performance counters.
         */
        struct Rpc
        {
            // Counters for each time a method starts and then either
            // finishes successfully or with an exception.
            std::uint64_t started{0};
            std::uint64_t finished{0};
            std::uint64_t errored{0};
            // Cumulative duration of all finished and errored method calls.
            Microseconds duration{0};
        };

        /**
         * Job Queue task performance counters.
         */
        struct Jq
        {
            // Counters for each time a job is enqueued, begins to run,
            // finishes.
            std::uint64_t queued{0};
            std::uint64_t started{0};
            std::uint64_t finished{0};
            // Cumulative duration of all jobs' queued and running times.
            Microseconds queuedDuration{0};
            Microseconds runningDuration{0};
        };

        // rpc and jq do not need mutex protection because all
        // keys and values are created before more threads are started.
        std::unordered_map<std::string, Locked<Rpc>> rpc;
        std::unordered_map<JobType, Locked<Jq>> jq;
        std::vector<std::pair<JobType, SteadyTimePoint>> jobs;
        mutable std::mutex jobsMutex;
        std::unordered_map<std::uint64_t, MethodStart> methods;
        mutable std::mutex methodsMutex;

        Counters(std::set<char const*> const& labels, JobTypes const& jobTypes);
        json::Value
        countersJson() const;
        json::Value
        currentJson() const;
    };

    Setup const setup_;
    Application& app_;
    beast::Journal const j_;
    std::function<void()> const signalStop_;
    Counters counters_{xrpl::rpc::getHandlerNames(), JobTypes::instance()};
    std::ofstream logFile_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cond_;
    SystemTimePoint lastLog_;
    std::string const hostname_{boost::asio::ip::host_name()};
    bool stop_{false};
    bool rotate_{false};

    void
    openLog();
    void
    run();
    void
    report();
    void
    rpcEnd(std::string const& method, std::uint64_t const requestId, bool finish);

public:
    PerfLogImp(
        Setup setup,
        Application& app,
        beast::Journal journal,
        std::function<void()>&& signalStop);

    ~PerfLogImp() override;

    void
    rpcStart(std::string const& method, std::uint64_t const requestId) override;

    void
    rpcFinish(std::string const& method, std::uint64_t const requestId) override
    {
        rpcEnd(method, requestId, true);
    }

    void
    rpcError(std::string const& method, std::uint64_t const requestId) override
    {
        rpcEnd(method, requestId, false);
    }

    void
    jobQueue(JobType const type) override;
    void
    jobStart(JobType const type, Microseconds dur, SteadyTimePoint startTime, int instance)
        override;
    void
    jobFinish(JobType const type, Microseconds dur, int instance) override;

    json::Value
    countersJson() const override
    {
        return counters_.countersJson();
    }

    json::Value
    currentJson() const override
    {
        return counters_.currentJson();
    }

    void
    resizeJobs(int const resize) override;
    void
    rotate() override;

    void
    start() override;

    void
    stop() override;
};

}  // namespace xrpl::perf
