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

#include <ripple/basics/BasicConfig.h>
#include <ripple/basics/impl/PerfLogImp.h>
#include <ripple/beast/core/CurrentThreadName.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/core/JobTypes.h>
#include <ripple/json/json_writer.h>
#include <ripple/json/to_string.h>
#include <boost/optional.hpp>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace ripple {
namespace perf {

PerfLogImp::Counters::Counters(
    std::vector<char const*> const& labels,
    JobTypes const& jobTypes)
{
    {
        // populateRpc
        rpc_.reserve(labels.size());
        for (std::string const label : labels)
        {
            auto const inserted = rpc_.emplace(label, Rpc()).second;
            if (!inserted)
            {
                // Ensure that no other function populates this entry.
                assert(false);
            }
        }
    }
    {
        // populateJq
        jq_.reserve(jobTypes.size());
        for (auto const& [jobType, _] : jobTypes)
        {
            auto const inserted = jq_.emplace(jobType, Jq()).second;
            if (!inserted)
            {
                // Ensure that no other function populates this entry.
                assert(false);
            }
        }
    }
}

Json::Value
PerfLogImp::Counters::countersJson() const
{
    Json::Value rpcobj(Json::objectValue);
    // totalRpc represents all rpc methods. All that started, finished, etc.
    Rpc totalRpc;
    for (auto const& proc : rpc_)
    {
        Rpc value;
        {
            std::lock_guard lock(proc.second.mutex);
            if (!proc.second.value.started && !proc.second.value.finished &&
                !proc.second.value.errored)
            {
                continue;
            }
            value = proc.second.value;
        }

        Json::Value p(Json::objectValue);
        p[jss::started] = std::to_string(value.started);
        totalRpc.started += value.started;
        p[jss::finished] = std::to_string(value.finished);
        totalRpc.finished += value.finished;
        p[jss::errored] = std::to_string(value.errored);
        totalRpc.errored += value.errored;
        p[jss::duration_us] = std::to_string(value.duration.count());
        totalRpc.duration += value.duration;
        rpcobj[proc.first] = p;
    }

    if (totalRpc.started)
    {
        Json::Value totalRpcJson(Json::objectValue);
        totalRpcJson[jss::started] = std::to_string(totalRpc.started);
        totalRpcJson[jss::finished] = std::to_string(totalRpc.finished);
        totalRpcJson[jss::errored] = std::to_string(totalRpc.errored);
        totalRpcJson[jss::duration_us] =
            std::to_string(totalRpc.duration.count());
        rpcobj[jss::total] = totalRpcJson;
    }

    Json::Value jqobj(Json::objectValue);
    // totalJq represents all jobs. All enqueued, started, finished, etc.
    Jq totalJq;
    for (auto const& proc : jq_)
    {
        Jq value;
        {
            std::lock_guard lock(proc.second.mutex);
            if (!proc.second.value.queued && !proc.second.value.started &&
                !proc.second.value.finished)
            {
                continue;
            }
            value = proc.second.value;
        }

        Json::Value j(Json::objectValue);
        j[jss::queued] = std::to_string(value.queued);
        totalJq.queued += value.queued;
        j[jss::started] = std::to_string(value.started);
        totalJq.started += value.started;
        j[jss::finished] = std::to_string(value.finished);
        totalJq.finished += value.finished;
        j[jss::queued_duration_us] =
            std::to_string(value.queuedDuration.count());
        totalJq.queuedDuration += value.queuedDuration;
        j[jss::running_duration_us] =
            std::to_string(value.runningDuration.count());
        totalJq.runningDuration += value.runningDuration;
        jqobj[JobTypes::name(proc.first)] = j;
    }

    if (totalJq.queued)
    {
        Json::Value totalJqJson(Json::objectValue);
        totalJqJson[jss::queued] = std::to_string(totalJq.queued);
        totalJqJson[jss::started] = std::to_string(totalJq.started);
        totalJqJson[jss::finished] = std::to_string(totalJq.finished);
        totalJqJson[jss::queued_duration_us] =
            std::to_string(totalJq.queuedDuration.count());
        totalJqJson[jss::running_duration_us] =
            std::to_string(totalJq.runningDuration.count());
        jqobj[jss::total] = totalJqJson;
    }

    Json::Value counters(Json::objectValue);
    // Be kind to reporting tools and let them expect rpc and jq objects
    // even if empty.
    counters[jss::rpc] = rpcobj;
    counters[jss::job_queue] = jqobj;
    return counters;
}

Json::Value
PerfLogImp::Counters::currentJson() const
{
    auto const present = steady_clock::now();

    Json::Value jobsArray(Json::arrayValue);
    auto const jobs = [this] {
        std::lock_guard lock(jobsMutex_);
        return jobs_;
    }();

    for (auto const& j : jobs)
    {
        if (j.first == jtINVALID)
            continue;
        Json::Value jobj(Json::objectValue);
        jobj[jss::job] = JobTypes::name(j.first);
        jobj[jss::duration_us] = std::to_string(
            std::chrono::duration_cast<microseconds>(present - j.second)
                .count());
        jobsArray.append(jobj);
    }

    Json::Value methodsArray(Json::arrayValue);
    std::vector<MethodStart> methods;
    {
        std::lock_guard lock(methodsMutex_);
        methods.reserve(methods_.size());
        for (auto const& m : methods_)
            methods.push_back(m.second);
    }
    for (auto m : methods)
    {
        Json::Value methodobj(Json::objectValue);
        methodobj[jss::method] = m.first;
        methodobj[jss::duration_us] = std::to_string(
            std::chrono::duration_cast<microseconds>(present - m.second)
                .count());
        methodsArray.append(methodobj);
    }

    Json::Value current(Json::objectValue);
    current[jss::jobs] = jobsArray;
    current[jss::methods] = methodsArray;
    return current;
}

//-----------------------------------------------------------------------------

void
PerfLogImp::openLog()
{
    if (setup_.perfLog.empty())
        return;

    if (logFile_.is_open())
        logFile_.close();

    auto logDir = setup_.perfLog.parent_path();
    if (!boost::filesystem::is_directory(logDir))
    {
        boost::system::error_code ec;
        boost::filesystem::create_directories(logDir, ec);
        if (ec)
        {
            JLOG(j_.fatal()) << "Unable to create performance log "
                                "directory "
                             << logDir << ": " << ec.message();
            signalStop_();
            return;
        }
    }

    logFile_.open(setup_.perfLog.c_str(), std::ios::out | std::ios::app);

    if (!logFile_)
    {
        JLOG(j_.fatal()) << "Unable to open performance log " << setup_.perfLog
                         << ".";
        signalStop_();
    }
}

void
PerfLogImp::run()
{
    beast::setCurrentThreadName("perflog");
    lastLog_ = system_clock::now();

    while (true)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (cond_.wait_until(
                    lock, lastLog_ + setup_.logInterval, [&] { return stop_; }))
            {
                return;
            }
            if (rotate_)
            {
                openLog();
                rotate_ = false;
            }
        }
        report();
    }
}

void
PerfLogImp::report()
{
    if (!logFile_)
        // If logFile_ is not writable do no further work.
        return;

    auto const present = system_clock::now();
    if (present < lastLog_ + setup_.logInterval)
        return;
    lastLog_ = present;

    Json::Value report(Json::objectValue);
    report[jss::time] = to_string(floor<microseconds>(present));
    {
        std::lock_guard lock{counters_.jobsMutex_};
        report[jss::workers] =
            static_cast<unsigned int>(counters_.jobs_.size());
    }
    report[jss::hostid] = hostname_;
    report[jss::counters] = counters_.countersJson();
    report[jss::current_activities] = counters_.currentJson();

    logFile_ << Json::Compact{std::move(report)} << std::endl;
}

PerfLogImp::PerfLogImp(
    Setup const& setup,
    Stoppable& parent,
    beast::Journal journal,
    std::function<void()>&& signalStop)
    : Stoppable("PerfLogImp", parent)
    , setup_(setup)
    , j_(journal)
    , signalStop_(std::move(signalStop))
{
    openLog();
}

PerfLogImp::~PerfLogImp()
{
    onStop();
}

void
PerfLogImp::rpcStart(std::string const& method, std::uint64_t const requestId)
{
    auto counter = counters_.rpc_.find(method);
    if (counter == counters_.rpc_.end())
    {
        assert(false);
        return;
    }

    {
        std::lock_guard lock(counter->second.mutex);
        ++counter->second.value.started;
    }
    std::lock_guard lock(counters_.methodsMutex_);
    counters_.methods_[requestId] = {
        counter->first.c_str(), steady_clock::now()};
}

void
PerfLogImp::rpcEnd(
    std::string const& method,
    std::uint64_t const requestId,
    bool finish)
{
    auto counter = counters_.rpc_.find(method);
    if (counter == counters_.rpc_.end())
    {
        assert(false);
        return;
    }
    steady_time_point startTime;
    {
        std::lock_guard lock(counters_.methodsMutex_);
        auto const e = counters_.methods_.find(requestId);
        if (e != counters_.methods_.end())
        {
            startTime = e->second.second;
            counters_.methods_.erase(e);
        }
        else
        {
            assert(false);
        }
    }
    std::lock_guard lock(counter->second.mutex);
    if (finish)
        ++counter->second.value.finished;
    else
        ++counter->second.value.errored;
    counter->second.value.duration += std::chrono::duration_cast<microseconds>(
        steady_clock::now() - startTime);
}

void
PerfLogImp::jobQueue(JobType const type)
{
    auto counter = counters_.jq_.find(type);
    if (counter == counters_.jq_.end())
    {
        assert(false);
        return;
    }
    std::lock_guard lock(counter->second.mutex);
    ++counter->second.value.queued;
}

void
PerfLogImp::jobStart(
    JobType const type,
    microseconds dur,
    steady_time_point startTime,
    int instance)
{
    auto counter = counters_.jq_.find(type);
    if (counter == counters_.jq_.end())
    {
        assert(false);
        return;
    }
    {
        std::lock_guard lock(counter->second.mutex);
        ++counter->second.value.started;
        counter->second.value.queuedDuration += dur;
    }
    std::lock_guard lock(counters_.jobsMutex_);
    if (instance >= 0 && instance < counters_.jobs_.size())
        counters_.jobs_[instance] = {type, startTime};
}

void
PerfLogImp::jobFinish(JobType const type, microseconds dur, int instance)
{
    auto counter = counters_.jq_.find(type);
    if (counter == counters_.jq_.end())
    {
        assert(false);
        return;
    }
    {
        std::lock_guard lock(counter->second.mutex);
        ++counter->second.value.finished;
        counter->second.value.runningDuration += dur;
    }
    std::lock_guard lock(counters_.jobsMutex_);
    if (instance >= 0 && instance < counters_.jobs_.size())
        counters_.jobs_[instance] = {jtINVALID, steady_time_point()};
}

void
PerfLogImp::resizeJobs(int const resize)
{
    std::lock_guard lock(counters_.jobsMutex_);
    if (resize > counters_.jobs_.size())
        counters_.jobs_.resize(resize, {jtINVALID, steady_time_point()});
}

void
PerfLogImp::rotate()
{
    if (setup_.perfLog.empty())
        return;

    std::lock_guard lock(mutex_);
    rotate_ = true;
    cond_.notify_one();
}

void
PerfLogImp::onStart()
{
    if (setup_.perfLog.size())
        thread_ = std::thread(&PerfLogImp::run, this);
}

void
PerfLogImp::onStop()
{
    if (thread_.joinable())
    {
        {
            std::lock_guard lock(mutex_);
            stop_ = true;
            cond_.notify_one();
        }
        thread_.join();
    }
}

void
PerfLogImp::onChildrenStopped()
{
    stopped();
}

//-----------------------------------------------------------------------------

PerfLog::Setup
setup_PerfLog(Section const& section, boost::filesystem::path const& configDir)
{
    PerfLog::Setup setup;
    std::string perfLog;
    set(perfLog, "perf_log", section);
    if (perfLog.size())
    {
        setup.perfLog = boost::filesystem::path(perfLog);
        if (setup.perfLog.is_relative())
        {
            setup.perfLog =
                boost::filesystem::absolute(setup.perfLog, configDir);
        }
    }

    std::uint64_t logInterval;
    if (get_if_exists(section, "log_interval", logInterval))
        setup.logInterval = std::chrono::seconds(logInterval);
    return setup;
}

std::unique_ptr<PerfLog>
make_PerfLog(
    PerfLog::Setup const& setup,
    Stoppable& parent,
    beast::Journal journal,
    std::function<void()>&& signalStop)
{
    return std::make_unique<PerfLogImp>(
        setup, parent, journal, std::move(signalStop));
}

}  // namespace perf
}  // namespace ripple
