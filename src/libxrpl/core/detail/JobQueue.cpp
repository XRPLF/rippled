#include <xrpl/core/JobQueue.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobTypeInfo.h>
#include <xrpl/core/LoadEvent.h>
#include <xrpl/core/PerfLog.h>
#include <xrpl/json/json_value.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <tuple>
#include <utility>

namespace xrpl {

JobQueue::JobQueue(
    int threadCount,
    beast::insight::Collector::ptr const& collector,
    beast::Journal journal,
    Logs& logs,
    perf::PerfLog& perfLog)
    : journal_(journal)
    , invalidJobData_(JobTypes::instance().getInvalid(), collector, logs)
    , workers_(*this, &perfLog, "JobQueue", threadCount)
    , perfLog_(perfLog)
    , collector_(collector)
{
    JLOG(journal_.info()) << "Using " << threadCount << "  threads";

    hook_ = collector_->makeHook(std::bind(&JobQueue::collect, this));
    jobCount_ = collector_->makeGauge("job_count");

    {
        std::scoped_lock const lock(mutex_);

        for (auto const& x : JobTypes::instance())
        {
            JobTypeInfo const& jt = x.second;

            // And create dynamic information for all jobs
            auto const result(jobData_.emplace(
                std::piecewise_construct,
                std::forward_as_tuple(jt.type()),
                std::forward_as_tuple(jt, collector_, logs)));
            XRPL_ASSERT(result.second == true, "xrpl::JobQueue::JobQueue : jobs added");
            (void)result.second;
        }
    }
}

JobQueue::~JobQueue()
{
    // Must unhook before destroying
    hook_ = beast::insight::Hook();
}

void
JobQueue::collect()
{
    std::scoped_lock const lock(mutex_);
    jobCount_ = jobSet_.size();
}

bool
JobQueue::addRefCountedJob(JobType type, std::string const& name, JobFunction const& func)
{
    XRPL_ASSERT(type != JtInvalid, "xrpl::JobQueue::addRefCountedJob : valid input job type");

    auto iter(jobData_.find(type));
    XRPL_ASSERT(
        iter != jobData_.end(), "xrpl::JobQueue::addRefCountedJob : job type found in jobs");
    if (iter == jobData_.end())
        return false;

    JLOG(journal_.debug()) << __func__ << " : Adding job : " << name << " : " << type;
    JobTypeData& data(iter->second);

    // FIXME: Workaround incorrect client shutdown ordering
    // do not add jobs to a queue with no threads
    XRPL_ASSERT(
        (type >= JtClient && type <= JtClientWebsocket) || workers_.getNumberOfThreads() > 0,
        "xrpl::JobQueue::addRefCountedJob : threads available or job "
        "requires no threads");

    {
        std::scoped_lock const lock(mutex_);
        auto result = jobSet_.emplace(type, name, ++lastJob_, data.load(), func);
        auto const& job = *result.first;

        JobType const type(job.getType());
        XRPL_ASSERT(type != JtInvalid, "xrpl::JobQueue::addRefCountedJob : has valid job type");
        XRPL_ASSERT(jobSet_.contains(job), "xrpl::JobQueue::addRefCountedJob : job found");
        perfLog_.jobQueue(type);

        JobTypeData& data(getJobTypeData(type));

        if (data.waiting + data.running < getJobLimit(type))
        {
            workers_.addTask();
        }
        else
        {
            // defer the task until we go below the limit
            ++data.deferred;
        }
        ++data.waiting;
    }
    return true;
}

int
JobQueue::getJobCount(JobType t) const
{
    std::scoped_lock const lock(mutex_);

    JobDataMap::const_iterator const c = jobData_.find(t);

    return (c == jobData_.end()) ? 0 : c->second.waiting;
}

int
JobQueue::getJobCountTotal(JobType t) const
{
    std::scoped_lock const lock(mutex_);

    JobDataMap::const_iterator const c = jobData_.find(t);

    return (c == jobData_.end()) ? 0 : (c->second.waiting + c->second.running);
}

int
JobQueue::getJobCountGE(JobType t) const
{
    // return the number of jobs at this priority level or greater
    int ret = 0;

    std::scoped_lock const lock(mutex_);

    for (auto const& x : jobData_)
    {
        if (x.first >= t)
            ret += x.second.waiting;
    }

    return ret;
}

std::unique_ptr<LoadEvent>
JobQueue::makeLoadEvent(JobType t, std::string const& name)
{
    JobDataMap::iterator const iter(jobData_.find(t));
    XRPL_ASSERT(iter != jobData_.end(), "xrpl::JobQueue::makeLoadEvent : valid job type input");

    if (iter == jobData_.end())
        return {};

    return std::make_unique<LoadEvent>(iter->second.load(), name, true);
}

void
JobQueue::addLoadEvents(JobType t, int count, std::chrono::milliseconds elapsed)
{
    if (isStopped())
        logicError("JobQueue::addLoadEvents() called after JobQueue stopped");

    JobDataMap::iterator const iter(jobData_.find(t));
    XRPL_ASSERT(iter != jobData_.end(), "xrpl::JobQueue::addLoadEvents : valid job type input");
    iter->second.load().addSamples(count, elapsed);
}

bool
JobQueue::isOverloaded()
{
    return std::ranges::any_of(jobData_, [](auto& entry) { return entry.second.load().isOver(); });
}

json::Value
JobQueue::getJson(int c)
{
    using namespace std::chrono_literals;
    json::Value ret(json::ValueType::Object);

    ret["threads"] = workers_.getNumberOfThreads();

    json::Value priorities = json::ValueType::Array;

    std::scoped_lock const lock(mutex_);

    for (auto& x : jobData_)
    {
        XRPL_ASSERT(x.first != JtInvalid, "xrpl::JobQueue::getJson : valid job type");

        if (x.first == JtGeneric)
            continue;

        JobTypeData& data(x.second);

        LoadMonitor::Stats const stats(data.stats());

        int const waiting(data.waiting);
        int const running(data.running);

        if ((stats.count != 0) || (waiting != 0) || (stats.latencyPeak != 0ms) || (running != 0))
        {
            json::Value& pri = priorities.append(json::ValueType::Object);

            pri["job_type"] = data.name();

            if (stats.isOverloaded)
                pri["over_target"] = true;

            if (waiting != 0)
                pri["waiting"] = waiting;

            if (stats.count != 0)
                pri["per_second"] = static_cast<int>(stats.count);

            if (stats.latencyPeak != 0ms)
                pri["peak_time"] = static_cast<int>(stats.latencyPeak.count());

            if (stats.latencyAvg != 0ms)
                pri["avg_time"] = static_cast<int>(stats.latencyAvg.count());

            if (running != 0)
                pri["in_progress"] = running;
        }
    }

    ret["job_types"] = priorities;

    return ret;
}

void
JobQueue::rendezvous()
{
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return processCount_ == 0 && jobSet_.empty(); });
}

JobTypeData&
JobQueue::getJobTypeData(JobType type)
{
    JobDataMap::iterator const c(jobData_.find(type));
    XRPL_ASSERT(c != jobData_.end(), "xrpl::JobQueue::getJobTypeData : valid job type input");

    // NIKB: This is ugly and I hate it. We must remove JtInvalid completely
    //       and use something sane.
    if (c == jobData_.end())
        return invalidJobData_;

    return c->second;
}

void
JobQueue::stop()
{
    stopping_ = true;
    using namespace std::chrono_literals;
    jobCounter_.join("JobQueue", 1s, journal_);
    {
        // After the JobCounter is joined, all jobs have finished executing
        // (i.e. returned from `Job::doJob`) and no more are being accepted,
        // but there may still be some threads between the return of
        // `Job::doJob` and the return of `JobQueue::processTask`. That is why
        // we must wait on the condition variable to make these assertions.
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return processCount_ == 0 && jobSet_.empty(); });
        XRPL_ASSERT(processCount_ == 0, "xrpl::JobQueue::stop : all processes completed");
        XRPL_ASSERT(jobSet_.empty(), "xrpl::JobQueue::stop : all jobs completed");
        XRPL_ASSERT(nSuspend_ == 0, "xrpl::JobQueue::stop : no coros suspended");
        stopped_ = true;
    }
}

bool
JobQueue::isStopped() const
{
    return stopped_;
}

void
JobQueue::getNextJob(Job& job)
{
    XRPL_ASSERT(!jobSet_.empty(), "xrpl::JobQueue::getNextJob : non-empty jobs");

    std::set<Job>::const_iterator iter;
    for (iter = jobSet_.begin(); iter != jobSet_.end(); ++iter)
    {
        JobType const type = iter->getType();
        XRPL_ASSERT(type != JtInvalid, "xrpl::JobQueue::getNextJob : valid job type");

        JobTypeData& data(getJobTypeData(type));
        XRPL_ASSERT(
            data.running <= getJobLimit(type), "xrpl::JobQueue::getNextJob : maximum jobs running");

        // Run this job if we're running below the limit.
        if (data.running < getJobLimit(data.type()))
        {
            XRPL_ASSERT(data.waiting > 0, "xrpl::JobQueue::getNextJob : positive data waiting");
            --data.waiting;
            ++data.running;
            break;
        }
    }

    XRPL_ASSERT(iter != jobSet_.end(), "xrpl::JobQueue::getNextJob : found next job");
    job = *iter;
    jobSet_.erase(iter);
}

void
JobQueue::finishJob(JobType type)
{
    XRPL_ASSERT(type != JtInvalid, "xrpl::JobQueue::finishJob : valid input job type");

    JobTypeData& data = getJobTypeData(type);

    // Queue a deferred task if possible
    if (data.deferred > 0)
    {
        XRPL_ASSERT(
            data.running + data.waiting >= getJobLimit(type),
            "xrpl::JobQueue::finishJob : job limit");

        --data.deferred;
        workers_.addTask();
    }

    --data.running;
}

void
JobQueue::processTask(int instance)
{
    JobType type = JtInvalid;

    {
        using namespace std::chrono;
        Job::clock_type::time_point const startTime(Job::clock_type::now());
        {
            Job job;
            {
                std::scoped_lock const lock(mutex_);
                getNextJob(job);
                ++processCount_;
            }
            type = job.getType();
            JobTypeData const& data(getJobTypeData(type));
            JLOG(journal_.trace()) << "Doing " << data.name() << "job";

            // The amount of time that the job was in the queue
            auto const qTime = ceil<microseconds>(startTime - job.queueTime());
            perfLog_.jobStart(type, qTime, startTime, instance);

            job.doJob();

            // The amount of time it took to execute the job
            auto const xTime = ceil<microseconds>(Job::clock_type::now() - startTime);

            if (xTime >= 10ms || qTime >= 10ms)
            {
                getJobTypeData(type).dequeue.notify(qTime);
                getJobTypeData(type).execute.notify(xTime);
            }
            perfLog_.jobFinish(type, xTime, instance);
        }
    }

    {
        std::scoped_lock const lock(mutex_);
        // Job should be destroyed before stopping
        // otherwise destructors with side effects can access
        // parent objects that are already destroyed.
        finishJob(type);
        if (--processCount_ == 0 && jobSet_.empty())
            cv_.notify_all();
    }

    // Note that when Job::~Job is called, the last reference
    // to the associated LoadEvent object (in the Job) may be destroyed.
}

int
JobQueue::getJobLimit(JobType type)
{
    JobTypeInfo const& j(JobTypes::instance().get(type));
    XRPL_ASSERT(j.type() != JtInvalid, "xrpl::JobQueue::getJobLimit : valid job type");

    return j.limit();
}

}  // namespace xrpl
