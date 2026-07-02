#include <xrpld/perflog/detail/PerfLogImp.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/core/CurrentThreadName.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/config/Constants.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobTypes.h>
#include <xrpl/core/PerfLog.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/json_writer.h>
#include <xrpl/protocol/jss.h>

#include <boost/filesystem/operations.hpp>
#include <boost/system/detail/error_code.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <ios>
#include <memory>
#include <mutex>
#include <ostream>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace xrpl::perf {

PerfLogImp::Counters::Counters(std::set<char const*> const& labels, JobTypes const& jobTypes)
{
    {
        // populateRpc
        rpc.reserve(labels.size());
        for (std::string const label : labels)
        {
            auto const inserted = rpc.emplace(label, Rpc()).second;
            if (!inserted)
            {
                // Ensure that no other function populates this entry.
                // LCOV_EXCL_START
                UNREACHABLE(
                    "xrpl::perf::PerfLogImp::Counters::Counters : failed to "
                    "insert label");
                // LCOV_EXCL_STOP
            }
        }
    }
    {
        // populateJq
        jq.reserve(jobTypes.size());
        for (auto const& [jobType, _] : jobTypes)
        {
            auto const inserted = jq.emplace(jobType, Jq()).second;
            if (!inserted)
            {
                // Ensure that no other function populates this entry.
                // LCOV_EXCL_START
                UNREACHABLE(
                    "xrpl::perf::PerfLogImp::Counters::Counters : failed to "
                    "insert job type");
                // LCOV_EXCL_STOP
            }
        }
    }
}

json::Value
PerfLogImp::Counters::countersJson() const
{
    json::Value rpcobj(json::ValueType::Object);
    // totalRpc represents all rpc methods. All that started, finished, etc.
    Rpc totalRpc;
    for (auto const& proc : rpc)
    {
        Rpc value;
        {
            std::scoped_lock const lock(proc.second.mutex);
            if ((proc.second.value.started == 0u) && (proc.second.value.finished == 0u) &&
                (proc.second.value.errored == 0u))
            {
                continue;
            }
            value = proc.second.value;
        }

        json::Value p(json::ValueType::Object);
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

    if (totalRpc.started != 0u)
    {
        json::Value totalRpcJson(json::ValueType::Object);
        totalRpcJson[jss::started] = std::to_string(totalRpc.started);
        totalRpcJson[jss::finished] = std::to_string(totalRpc.finished);
        totalRpcJson[jss::errored] = std::to_string(totalRpc.errored);
        totalRpcJson[jss::duration_us] = std::to_string(totalRpc.duration.count());
        rpcobj[jss::total] = totalRpcJson;
    }

    json::Value jobQueueObj(json::ValueType::Object);
    // totalJq represents all jobs. All enqueued, started, finished, etc.
    Jq totalJq;
    for (auto const& proc : jq)
    {
        Jq value;
        {
            std::scoped_lock const lock(proc.second.mutex);
            if ((proc.second.value.queued == 0u) && (proc.second.value.started == 0u) &&
                (proc.second.value.finished == 0u))
            {
                continue;
            }
            value = proc.second.value;
        }

        json::Value j(json::ValueType::Object);
        j[jss::queued] = std::to_string(value.queued);
        totalJq.queued += value.queued;
        j[jss::started] = std::to_string(value.started);
        totalJq.started += value.started;
        j[jss::finished] = std::to_string(value.finished);
        totalJq.finished += value.finished;
        j[jss::queued_duration_us] = std::to_string(value.queuedDuration.count());
        totalJq.queuedDuration += value.queuedDuration;
        j[jss::running_duration_us] = std::to_string(value.runningDuration.count());
        totalJq.runningDuration += value.runningDuration;
        jobQueueObj[JobTypes::name(proc.first)] = j;
    }

    if (totalJq.queued != 0u)
    {
        json::Value totalJqJson(json::ValueType::Object);
        totalJqJson[jss::queued] = std::to_string(totalJq.queued);
        totalJqJson[jss::started] = std::to_string(totalJq.started);
        totalJqJson[jss::finished] = std::to_string(totalJq.finished);
        totalJqJson[jss::queued_duration_us] = std::to_string(totalJq.queuedDuration.count());
        totalJqJson[jss::running_duration_us] = std::to_string(totalJq.runningDuration.count());
        jobQueueObj[jss::total] = totalJqJson;
    }

    json::Value counters(json::ValueType::Object);
    // Be kind to reporting tools and let them expect rpc and jq objects
    // even if empty.
    counters[jss::rpc] = rpcobj;
    counters[jss::job_queue] = jobQueueObj;
    return counters;
}

json::Value
PerfLogImp::Counters::currentJson() const
{
    auto const present = steady_clock::now();

    json::Value jobsArray(json::ValueType::Array);
    auto const jobs = [this] {
        std::scoped_lock const lock(jobsMutex);
        return this->jobs;
    }();

    for (auto const& j : jobs)
    {
        if (j.first == JtInvalid)
            continue;
        json::Value jobj(json::ValueType::Object);
        jobj[jss::job] = JobTypes::name(j.first);
        jobj[jss::duration_us] =
            std::to_string(std::chrono::duration_cast<microseconds>(present - j.second).count());
        jobsArray.append(jobj);
    }

    json::Value methodsArray(json::ValueType::Array);
    std::vector<MethodStart> methods;
    {
        std::scoped_lock const lock(methodsMutex);
        methods.reserve(this->methods.size());
        for (auto const& m : this->methods)
            methods.push_back(m.second);
    }
    for (auto m : methods)
    {
        json::Value methodobj(json::ValueType::Object);
        methodobj[jss::method] = m.first;
        methodobj[jss::duration_us] =
            std::to_string(std::chrono::duration_cast<microseconds>(present - m.second).count());
        methodsArray.append(methodobj);
    }

    json::Value current(json::ValueType::Object);
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
        JLOG(j_.fatal()) << "Unable to open performance log " << setup_.perfLog << ".";
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
            if (cond_.wait_until(lock, lastLog_ + setup_.logInterval, [&] { return stop_; }))
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
    {
        // If logFile_ is not writable do no further work.
        return;
    }

    auto const present = system_clock::now();
    if (present < lastLog_ + setup_.logInterval)
        return;
    lastLog_ = present;

    json::Value report(json::ValueType::Object);
    report[jss::time] = to_string(std::chrono::floor<microseconds>(present));
    {
        std::scoped_lock const lock{counters_.jobsMutex};
        report[jss::workers] = static_cast<unsigned int>(counters_.jobs.size());
    }
    report[jss::hostid] = hostname_;
    report[jss::counters] = counters_.countersJson();
    report[jss::nodestore] = json::ValueType::Object;
    app_.getNodeStore().getCountsJson(report[jss::nodestore]);
    report[jss::current_activities] = counters_.currentJson();
    app_.getOPs().stateAccounting(report);

    logFile_ << json::Compact{std::move(report)} << std::endl;
}

PerfLogImp::PerfLogImp(
    Setup setup,
    Application& app,
    beast::Journal journal,
    std::function<void()>&& signalStop)
    : setup_(std::move(setup)), app_(app), j_(journal), signalStop_(std::move(signalStop))
{
    openLog();
}

PerfLogImp::~PerfLogImp()
{
    stop();
}

void
PerfLogImp::rpcStart(std::string const& method, std::uint64_t const requestId)
{
    auto counter = counters_.rpc.find(method);
    if (counter == counters_.rpc.end())
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::perf::PerfLogImp::rpcStart : valid method input");
        return;
        // LCOV_EXCL_STOP
    }

    {
        std::scoped_lock const lock(counter->second.mutex);
        ++counter->second.value.started;
    }
    std::scoped_lock const lock(counters_.methodsMutex);
    counters_.methods[requestId] = {counter->first.c_str(), steady_clock::now()};
}

void
PerfLogImp::rpcEnd(std::string const& method, std::uint64_t const requestId, bool finish)
{
    auto counter = counters_.rpc.find(method);
    if (counter == counters_.rpc.end())
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::perf::PerfLogImp::rpcEnd : valid method input");
        return;
        // LCOV_EXCL_STOP
    }
    steady_time_point startTime;
    {
        std::scoped_lock const lock(counters_.methodsMutex);
        auto const e = counters_.methods.find(requestId);
        if (e != counters_.methods.end())
        {
            startTime = e->second.second;
            counters_.methods.erase(e);
        }
        else
        {
            // LCOV_EXCL_START
            UNREACHABLE("xrpl::perf::PerfLogImp::rpcEnd : valid requestId input");
            // LCOV_EXCL_STOP
        }
    }
    std::scoped_lock const lock(counter->second.mutex);
    if (finish)
    {
        ++counter->second.value.finished;
    }
    else
    {
        ++counter->second.value.errored;
    }
    counter->second.value.duration +=
        std::chrono::duration_cast<microseconds>(steady_clock::now() - startTime);
}

void
PerfLogImp::jobQueue(JobType const type)
{
    auto counter = counters_.jq.find(type);
    if (counter == counters_.jq.end())
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::perf::PerfLogImp::jobQueue : valid job type input");
        return;
        // LCOV_EXCL_STOP
    }
    std::scoped_lock const lock(counter->second.mutex);
    ++counter->second.value.queued;
}

void
PerfLogImp::jobStart(
    JobType const type,
    microseconds dur,
    steady_time_point startTime,
    int instance)
{
    auto counter = counters_.jq.find(type);
    if (counter == counters_.jq.end())
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::perf::PerfLogImp::jobStart : valid job type input");
        return;
        // LCOV_EXCL_STOP
    }

    {
        std::scoped_lock const lock(counter->second.mutex);
        ++counter->second.value.started;
        counter->second.value.queuedDuration += dur;
    }
    std::scoped_lock const lock(counters_.jobsMutex);
    if (instance >= 0 && instance < counters_.jobs.size())
        counters_.jobs[instance] = {type, startTime};
}

void
PerfLogImp::jobFinish(JobType const type, microseconds dur, int instance)
{
    auto counter = counters_.jq.find(type);
    if (counter == counters_.jq.end())
    {
        // LCOV_EXCL_START
        UNREACHABLE("xrpl::perf::PerfLogImp::jobFinish : valid job type input");
        return;
        // LCOV_EXCL_STOP
    }

    {
        std::scoped_lock const lock(counter->second.mutex);
        ++counter->second.value.finished;
        counter->second.value.runningDuration += dur;
    }
    std::scoped_lock const lock(counters_.jobsMutex);
    if (instance >= 0 && instance < counters_.jobs.size())
        counters_.jobs[instance] = {JtInvalid, steady_time_point()};
}

void
PerfLogImp::resizeJobs(int const resize)
{
    std::scoped_lock const lock(counters_.jobsMutex);
    if (resize > counters_.jobs.size())
        counters_.jobs.resize(resize, {JtInvalid, steady_time_point()});
}

void
PerfLogImp::rotate()
{
    if (setup_.perfLog.empty())
        return;

    std::scoped_lock const lock(mutex_);
    rotate_ = true;
    cond_.notify_one();
}

void
PerfLogImp::start()
{
    if (!setup_.perfLog.empty())
        thread_ = std::thread(&PerfLogImp::run, this);
}

void
PerfLogImp::stop()
{
    if (thread_.joinable())
    {
        {
            std::scoped_lock const lock(mutex_);
            stop_ = true;
            cond_.notify_one();
        }
        thread_.join();
    }
}

//-----------------------------------------------------------------------------

PerfLog::Setup
setupPerfLog(Section const& section, boost::filesystem::path const& configDir)
{
    PerfLog::Setup setup;
    std::string perfLog;
    set(perfLog, "perf_log", section);
    if (!perfLog.empty())
    {
        setup.perfLog = boost::filesystem::path(perfLog);
        if (setup.perfLog.is_relative())
        {
            setup.perfLog = boost::filesystem::absolute(setup.perfLog, configDir);
        }
    }

    std::uint64_t logInterval = 0;
    if (getIfExists(section, Keys::kLogInterval, logInterval))
        setup.logInterval = std::chrono::seconds(logInterval);
    return setup;
}

std::unique_ptr<PerfLog>
makePerfLog(
    PerfLog::Setup const& setup,
    Application& app,
    beast::Journal journal,
    std::function<void()>&& signalStop)
{
    return std::make_unique<PerfLogImp>(setup, app, journal, std::move(signalStop));
}

}  // namespace xrpl::perf
