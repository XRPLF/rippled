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

#ifndef RIPPLE_BASICS_PERFLOG_H
#define RIPPLE_BASICS_PERFLOG_H

#include <ripple/core/JobTypes.h>
#include <ripple/json/json_value.h>
#include <boost/filesystem.hpp>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace beast {
class Journal;
}

namespace ripple {
namespace perf {

/**
 * Singleton class that maintains performance counters and optionally
 * writes Json-formatted data to a distinct log. It should exist prior
 * to other objects launched by Application to make it accessible for
 * performance logging.
 */

class PerfLog
{
public:
    using steady_clock = std::chrono::steady_clock;
    using system_clock = std::chrono::system_clock;
    using steady_time_point = std::chrono::time_point<steady_clock>;
    using system_time_point = std::chrono::time_point<system_clock>;
    using seconds = std::chrono::seconds;
    using milliseconds = std::chrono::milliseconds;
    using microseconds = std::chrono::microseconds;

    /**
     * Configuration from [perf] section of rippled.cfg.
     */
    struct Setup
    {
        boost::filesystem::path perfLog;
        // log_interval is in milliseconds to support faster testing.
        milliseconds logInterval{seconds(1)};
    };

    virtual ~PerfLog() = default;

    virtual void
    start() = 0;

    /**
     * Log start of RPC call.
     *
     * @param method RPC command
     * @param requestId Unique identifier to track command
     */
    virtual void
    rpcStart(std::string const& method, std::uint64_t requestId) = 0;

    /**
     * Log successful finish of RPC call
     *
     * @param method RPC command
     * @param requestId Unique identifier to track command
     */
    virtual void
    rpcFinish(std::string const& method, std::uint64_t requestId) = 0;

    /**
     * Log errored RPC call
     *
     * @param method RPC command
     * @param requestId Unique identifier to track command
     */
    virtual void
    rpcError(std::string const& method, std::uint64_t requestId) = 0;

    /**
     * Log queued job
     *
     * @param type Job type
     */
    virtual void
    jobQueue(JobType const type) = 0;

    /**
     * Log job executing
     *
     * @param type Job type
     * @param dur Duration enqueued in microseconds
     * @param startTime Time that execution began
     * @param instance JobQueue worker thread instance
     */
    virtual void
    jobStart(
        JobType const type,
        microseconds dur,
        steady_time_point startTime,
        int instance) = 0;

    /**
     * Log job finishing
     *
     * @param type Job type
     * @param dur Duration running in microseconds
     * @param instance Jobqueue worker thread instance
     */
    virtual void
    jobFinish(JobType const type, microseconds dur, int instance) = 0;

    /**
     * Render performance counters in Json
     *
     * @return Counters Json object
     */
    virtual Json::Value
    countersJson() const = 0;

    /**
     * Render currently executing jobs and RPC calls and durations in Json
     *
     * @return Current executing jobs and RPC calls and durations
     */
    virtual Json::Value
    currentJson() const = 0;

    /**
     * Ensure enough room to store each currently executing job
     *
     * @param resize Number of JobQueue worker threads
     */
    virtual void
    resizeJobs(int const resize) = 0;

    /**
     * Rotate perf log file
     */
    virtual void
    rotate() = 0;
};

}  // namespace perf

class Section;
class Stoppable;

namespace perf {

PerfLog::Setup
setup_PerfLog(Section const& section, boost::filesystem::path const& configDir);

std::unique_ptr<PerfLog>
make_PerfLog(
    PerfLog::Setup const& setup,
    Stoppable& parent,
    beast::Journal journal,
    std::function<void()>&& signalStop);

}  // namespace perf
}  // namespace ripple

#endif  // RIPPLE_BASICS_PERFLOG_H
