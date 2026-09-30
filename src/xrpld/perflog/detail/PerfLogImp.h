#pragma once

#include <xrpl/basics/StringUtilities.h>
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
#include <span>
#include <string>
#include <string_view>
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
        using MethodStart = std::pair<std::string_view, steady_time_point>;

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
            microseconds duration{0};
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
            microseconds queuedDuration{0};
            microseconds runningDuration{0};
        };

        // rpc and jq do not need mutex protection because all
        // keys and values are created before more threads are started.
        //
        // Every key views the characters of a name from the methodNames constructor
        // parameter below, which the caller guarantees outlive this object, so the
        // map copies no name to store one and needs no string to look one up.
        std::unordered_map<std::string_view, Locked<Rpc>> rpc;

        // The same names, in the order the caller gave them, and still carrying the
        // proof that each reaches its terminating null. countersJson() walks these
        // rather than rpc, so that it can report a key as a C string. Held by value,
        // so that a caller may build the range it passes on the fly: only the names
        // have to outlive this object, not the container that carried them.
        std::vector<NullTerminatedView> labels;
        std::unordered_map<JobType, Locked<Jq>> jq;
        std::vector<std::pair<JobType, steady_time_point>> jobs;
        mutable std::mutex jobsMutex;
        // Each view is a key of rpc above, not the argument rpcStart() received, so
        // currentJson() may read it as a C string.
        std::unordered_map<std::uint64_t, MethodStart> methods;
        mutable std::mutex methodsMutex;

        Counters(std::span<NullTerminatedView const> methodNames, JobTypes const& jobTypes);
        json::Value
        countersJson() const;
        json::Value
        currentJson() const;
    };

    Setup const setup_;
    Application& app_;
    beast::Journal const j_;
    std::function<void()> const signalStop_;
    Counters counters_;
    std::ofstream logFile_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cond_;
    system_time_point lastLog_;
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
    rpcEnd(std::string_view method, std::uint64_t const requestId, bool finish);

public:
    /**
     * @param methodNames The RPC methods to count, one counter per name. The
     *        names must outlive this object, which holds views of them. Passed
     *        in rather than looked up here so that this layer needs to know
     *        nothing about the RPC dispatch table.
     */
    PerfLogImp(
        Setup setup,
        Application& app,
        std::span<NullTerminatedView const> methodNames,
        beast::Journal journal,
        std::function<void()>&& signalStop);

    ~PerfLogImp() override;

    void
    rpcStart(std::string_view method, std::uint64_t const requestId) override;

    void
    rpcFinish(std::string_view method, std::uint64_t const requestId) override
    {
        rpcEnd(method, requestId, true);
    }

    void
    rpcError(std::string_view method, std::uint64_t const requestId) override
    {
        rpcEnd(method, requestId, false);
    }

    void
    jobQueue(JobType const type) override;
    void
    jobStart(JobType const type, microseconds dur, steady_time_point startTime, int instance)
        override;
    void
    jobFinish(JobType const type, microseconds dur, int instance) override;

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
