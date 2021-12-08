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

#ifndef RIPPLE_CORE_JOBQUEUE_H_INCLUDED
#define RIPPLE_CORE_JOBQUEUE_H_INCLUDED

#include <ripple/basics/LocalValue.h>
#include <ripple/core/ClosureCounter.h>
#include <ripple/core/JobTypeData.h>
#include <ripple/core/JobTypes.h>
#include <ripple/core/impl/Workers.h>
#include <ripple/json/json_value.h>
#include <boost/coroutine/all.hpp>
#include <boost/range/begin.hpp>  // workaround for boost 1.72 bug
#include <boost/range/end.hpp>    // workaround for boost 1.72 bug

namespace ripple {

namespace perf {
class PerfLog;
}

class Logs;
struct Coro_create_t
{
    explicit Coro_create_t() = default;
};

/** A pool of threads to perform work.

    A job posted will always run to completion.

    Coroutines that are suspended must be resumed,
    and run to completion.

    When the JobQueue stops, it waits for all jobs
    and coroutines to finish.
*/
class JobQueue : private Workers::Callback
{
public:
    /** Coroutines must run to completion. */
    class Coro : public std::enable_shared_from_this<Coro>
    {
    private:
        detail::LocalValues lvs_;
        JobQueue& jq_;
        JobType type_;
        std::string name_;
        bool running_;
        std::mutex mutex_;
        std::mutex mutex_run_;
        std::condition_variable cv_;
        boost::coroutines::asymmetric_coroutine<void>::pull_type coro_;
        boost::coroutines::asymmetric_coroutine<void>::push_type* yield_;
#ifndef NDEBUG
        bool finished_ = false;
#endif

    public:
        // Private: Used in the implementation
        template <class F>
        Coro(Coro_create_t, JobQueue&, JobType, std::string const&, F&&);

        // Not copy-constructible or assignable
        Coro(Coro const&) = delete;
        Coro&
        operator=(Coro const&) = delete;

        ~Coro();

        /** Suspend coroutine execution.
            Effects:
              The coroutine's stack is saved.
              The associated Job thread is released.
            Note:
              The associated Job function returns.
              Undefined behavior if called consecutively without a corresponding
           post.
        */
        void
        yield() const;

        /** Schedule coroutine execution.
            Effects:
              Returns immediately.
              A new job is scheduled to resume the execution of the coroutine.
              When the job runs, the coroutine's stack is restored and execution
                continues at the beginning of coroutine function or the
           statement after the previous call to yield. Undefined behavior if
           called after the coroutine has completed with a return (as opposed to
           a yield()). Undefined behavior if post() or resume() called
           consecutively without a corresponding yield.

            @return true if the Coro's job is added to the JobQueue.
        */
        bool
        post();

        /** Resume coroutine execution.
            Effects:
               The coroutine continues execution from where it last left off
                 using this same thread.
            Undefined behavior if called after the coroutine has completed
              with a return (as opposed to a yield()).
            Undefined behavior if resume() or post() called consecutively
              without a corresponding yield.
        */
        void
        resume();

        /** Returns true if the Coro is still runnable (has not returned). */
        bool
        runnable() const;

        /** Once called, the Coro allows early exit without an assert. */
        void
        expectEarlyExit();

        /** Waits until coroutine returns from the user function. */
        void
        join();
    };

    using JobFunction = std::function<void(Job&)>;

    JobQueue(
        int threadCount,
        beast::insight::Collector::ptr const& collector,
        beast::Journal journal,
        Logs& logs,
        perf::PerfLog& perfLog);
    ~JobQueue();

    /** Adds a job to the JobQueue.

        @param type The type of job.
        @param name Name of the job.
        @param jobHandler Lambda with signature void (Job&).  Called when the
       job is executed.

        @return true if jobHandler added to queue.
    */
    template <
        typename JobHandler,
        typename = std::enable_if_t<std::is_same<
            decltype(std::declval<JobHandler&&>()(std::declval<Job&>())),
            void>::value>>
    bool
    addJob(JobType type, std::string const& name, JobHandler&& jobHandler)
    {
        if (auto optionalCountedJob =
                jobCounter_.wrap(std::forward<JobHandler>(jobHandler)))
        {
            return addRefCountedJob(type, name, std::move(*optionalCountedJob));
        }
        return false;
    }

    /** Creates a coroutine and adds a job to the queue which will run it.

        @param t The type of job.
        @param name Name of the job.
        @param f Has a signature of void(std::shared_ptr<Coro>). Called when the
       job executes.

        @return shared_ptr to posted Coro.  nullptr if post was not successful.
    */
    template <class F>
    std::shared_ptr<Coro>
    postCoro(JobType t, std::string const& name, F&& f);

    /** Jobs waiting at this priority.
     */
    int
    getJobCount(JobType t) const;

    /** Jobs waiting plus running at this priority.
     */
    int
    getJobCountTotal(JobType t) const;

    /** All waiting jobs at or greater than this priority.
     */
    int
    getJobCountGE(JobType t) const;

    /** Return a scoped LoadEvent.
     */
    std::unique_ptr<LoadEvent>
    makeLoadEvent(JobType t, std::string const& name);

    /** Add multiple load events.
     */
    void
    addLoadEvents(JobType t, int count, std::chrono::milliseconds elapsed);

    // Cannot be const because LoadMonitor has no const methods.
    bool
    isOverloaded();

    // Cannot be const because LoadMonitor has no const methods.
    Json::Value
    getJson(int c = 0);

    /** Block until no jobs running. */
    void
    rendezvous();

    void
    stop();

    bool
    isStopping() const
    {
        return stopping_;
    }

    // We may be able to move away from this, but we can keep it during the
    // transition.
    bool
    isStopped() const;

private:
    friend class Coro;

    using JobDataMap = std::map<JobType, JobTypeData>;

    beast::Journal m_journal;
    mutable std::mutex m_mutex;
    std::uint64_t m_lastJob;
    std::set<Job> m_jobSet;
    JobCounter jobCounter_;
    std::atomic_bool stopping_{false};
    std::atomic_bool stopped_{false};
    JobDataMap m_jobData;
    JobTypeData m_invalidJobData;

    // The number of jobs currently in processTask()
    int m_processCount;

    // The number of suspended coroutines
    int nSuspend_ = 0;

    Workers m_workers;
    Job::CancelCallback m_cancelCallback;

    // Statistics tracking
    perf::PerfLog& perfLog_;
    beast::insight::Collector::ptr m_collector;
    beast::insight::Gauge job_count;
    beast::insight::Hook hook;

    std::condition_variable cv_;

    void
    collect();
    JobTypeData&
    getJobTypeData(JobType type);

    // Adds a reference counted job to the JobQueue.
    //
    //    param type The type of job.
    //    param name Name of the job.
    //    param func std::function with signature void (Job&).  Called when the
    //    job is executed.
    //
    //    return true if func added to queue.
    bool
    addRefCountedJob(
        JobType type,
        std::string const& name,
        JobFunction const& func);

    // Signals an added Job for processing.
    //
    // Pre-conditions:
    //  The JobType must be valid.
    //  The Job must exist in mJobSet.
    //  The Job must not have previously been queued.
    //
    // Post-conditions:
    //  Count of waiting jobs of that type will be incremented.
    //  If JobQueue exists, and has at least one thread, Job will eventually
    //  run.
    //
    // Invariants:
    //  The calling thread owns the JobLock
    void
    queueJob(Job const& job, std::lock_guard<std::mutex> const& lock);

    // Returns the next Job we should run now.
    //
    // RunnableJob:
    //  A Job in the JobSet whose slots count for its type is greater than zero.
    //
    // Pre-conditions:
    //  mJobSet must not be empty.
    //  mJobSet holds at least one RunnableJob
    //
    // Post-conditions:
    //  job is a valid Job object.
    //  job is removed from mJobQueue.
    //  Waiting job count of its type is decremented
    //  Running job count of its type is incremented
    //
    // Invariants:
    //  The calling thread owns the JobLock
    void
    getNextJob(Job& job);

    // Indicates that a running Job has completed its task.
    //
    // Pre-conditions:
    //  Job must not exist in mJobSet.
    //  The JobType must not be invalid.
    //
    // Post-conditions:
    //  The running count of that JobType is decremented
    //  A new task is signaled if there are more waiting Jobs than the limit, if
    //  any.
    //
    // Invariants:
    //  <none>
    void
    finishJob(JobType type);

    // Runs the next appropriate waiting Job.
    //
    // Pre-conditions:
    //  A RunnableJob must exist in the JobSet
    //
    // Post-conditions:
    //  The chosen RunnableJob will have Job::doJob() called.
    //
    // Invariants:
    //  <none>
    void
    processTask(int instance) override;

    // Returns the limit of running jobs for the given job type.
    // For jobs with no limit, we return the largest int. Hopefully that
    // will be enough.
    int
    getJobLimit(JobType type);
};

/*
    An RPC command is received and is handled via ServerHandler(HTTP) or
    Handler(websocket), depending on the connection type. The handler then calls
    the JobQueue::postCoro() method to create a coroutine and run it at a later
    point. This frees up the handler thread and allows it to continue handling
    other requests while the RPC command completes its work asynchronously.

    postCoro() creates a Coro object. When the Coro ctor is called, and its
    coro_ member is initialized (a boost::coroutines::pull_type), execution
    automatically passes to the coroutine, which we don't want at this point,
    since we are still in the handler thread context. It's important to note
   here that construction of a boost pull_type automatically passes execution to
   the coroutine. A pull_type object automatically generates a push_type that is
    passed as a parameter (do_yield) in the signature of the function the
    pull_type was created with. This function is immediately called during coro_
    construction and within it, Coro::yield_ is assigned the push_type
    parameter (do_yield) address and called (yield()) so we can return execution
    back to the caller's stack.

    postCoro() then calls Coro::post(), which schedules a job on the job
    queue to continue execution of the coroutine in a JobQueue worker thread at
    some later time. When the job runs, we lock on the Coro::mutex_ and call
    coro_ which continues where we had left off. Since we the last thing we did
    in coro_ was call yield(), the next thing we continue with is calling the
    function param f, that was passed into Coro ctor. It is within this
    function body that the caller specifies what he would like to do while
    running in the coroutine and allow them to suspend and resume execution.
    A task that relies on other events to complete, such as path finding, calls
    Coro::yield() to suspend its execution while waiting on those events to
    complete and continue when signaled via the Coro::post() method.

    There is a potential race condition that exists here where post() can get
    called before yield() after f is called. Technically the problem only occurs
    if the job that post() scheduled is executed before yield() is called.
    If the post() job were to be executed before yield(), undefined behavior
    would occur. The lock ensures that coro_ is not called again until we exit
    the coroutine. At which point a scheduled resume() job waiting on the lock
    would gain entry, harmlessly call coro_ and immediately return as we have
    already completed the coroutine.

    The race condition occurs as follows:

        1- The coroutine is running.
        2- The coroutine is about to suspend, but before it can do so, it must
            arrange for some event to wake it up.
        3- The coroutine arranges for some event to wake it up.
        4- Before the coroutine can suspend, that event occurs and the
   resumption of the coroutine is scheduled on the job queue. 5- Again, before
   the coroutine can suspend, the resumption of the coroutine is dispatched. 6-
   Again, before the coroutine can suspend, the resumption code runs the
            coroutine.
        The coroutine is now running in two threads.

        The lock prevents this from happening as step 6 will block until the
            lock is released which only happens after the coroutine completes.
*/

}  // namespace ripple

#include <ripple/core/Coro.ipp>

namespace ripple {

template <class F>
std::shared_ptr<JobQueue::Coro>
JobQueue::postCoro(JobType t, std::string const& name, F&& f)
{
    /*  First param is a detail type to make construction private.
        Last param is the function the coroutine runs. Signature of
        void(std::shared_ptr<Coro>).
    */
    auto coro = std::make_shared<Coro>(
        Coro_create_t{}, *this, t, name, std::forward<F>(f));
    if (!coro->post())
    {
        // The Coro was not successfully posted.  Disable it so it's destructor
        // can run with no negative side effects.  Then destroy it.
        coro->expectEarlyExit();
        coro.reset();
    }
    return coro;
}

}  // namespace ripple

#endif
