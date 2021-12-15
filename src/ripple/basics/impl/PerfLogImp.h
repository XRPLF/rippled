//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2018 Ripple Labs Inc.

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

#ifndef RIPPLE_BASICS_PERFLOGIMP_H
#define RIPPLE_BASICS_PERFLOGIMP_H

#include <ripple/basics/PerfLog.h>
#include <ripple/basics/chrono.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/impl/Handler.h>
#include <boost/asio/ip/host_name.hpp>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ripple {
namespace perf {

/** A box coupling data with a mutex for locking access to it. */
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
        using MethodStart = std::pair<char const*, steady_time_point>;
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

        // rpc_ and jq_ do not need mutex protection because all
        // keys and values are created before more threads are started.
        std::unordered_map<std::string, Locked<Rpc>> rpc_;
        std::unordered_map<JobType, Locked<Jq>> jq_;
        std::vector<std::pair<JobType, steady_time_point>> jobs_;
        mutable std::mutex jobsMutex_;
        std::unordered_map<std::uint64_t, MethodStart> methods_;
        mutable std::mutex methodsMutex_;

        Counters(
            std::vector<char const*> const& labels,
            JobTypes const& jobTypes);
        Json::Value
        countersJson() const;
        Json::Value
        currentJson() const;
    };

    Setup const setup_;
    Application& app_;
    beast::Journal const j_;
    std::function<void()> const signalStop_;
    Counters counters_{ripple::RPC::getHandlerNames(), JobTypes::instance()};
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
    rpcEnd(
        std::string const& method,
        std::uint64_t const requestId,
        bool finish);

public:
    PerfLogImp(
        Setup const& setup,
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
    jobStart(
        JobType const type,
        microseconds dur,
        steady_time_point startTime,
        int instance) override;
    void
    jobFinish(JobType const type, microseconds dur, int instance) override;

    Json::Value
    countersJson() const override
    {
        return counters_.countersJson();
    }

    Json::Value
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

}  // namespace perf
}  // namespace ripple

#endif  // RIPPLE_BASICS_PERFLOGIMP_H
