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

#include <ripple/core/JobTypes.h>
#include <ripple/core/JobTypeData.h>
#include <ripple/core/JobCoro.h>
#include <ripple/json/json_value.h>
#include <beast/insight/Collector.h>
#include <beast/threads/Stoppable.h>
#include <beast/module/core/thread/Workers.h>
#include <boost/function.hpp>
#include <thread>
#include <set>

namespace ripple {

class Logs;

class JobQueue
    : public beast::Stoppable
    , private beast::Workers::Callback
{
public:
    using JobFunction = std::function <void(Job&)>;

    JobQueue (beast::insight::Collector::ptr const& collector,
        Stoppable& parent, beast::Journal journal, Logs& logs);
    ~JobQueue ();

    void addJob (JobType type, std::string const& name, JobFunction const& func);

    /** Creates a coroutine and adds a job to the queue which will run it.

        @param t The type of job.
        @param name Name of the job.
        @param f Has a signature of void(std::shared_ptr<JobCoro>). Called when the job executes.
    */
    template <class F>
    void postCoro (JobType t, std::string const& name, F&& f);

    /** Jobs waiting at this priority.
    */
    int getJobCount (JobType t) const;

    /** Jobs waiting plus running at this priority.
    */
    int getJobCountTotal (JobType t) const;

    /** All waiting jobs at or greater than this priority.
    */
    int getJobCountGE (JobType t) const;

    /** Shut down the job queue without completing pending jobs.
    */
    void shutdown ();

    /** Set the number of thread serving the job queue to precisely this number.
    */
    void setThreadCount (int c, bool const standaloneMode);

    // VFALCO TODO Rename these to newLoadEventMeasurement or something similar
    //             since they create the object.
    LoadEvent::pointer getLoadEvent (JobType t, std::string const& name);

    // VFALCO TODO Why do we need two versions, one which returns a shared
    //             pointer and the other which returns an autoptr?
    LoadEvent::autoptr getLoadEventAP (JobType t, std::string const& name);

    /** Add multiple load events.
    */
    void addLoadEvents (JobType t, int count, std::chrono::milliseconds elapsed);

    // Cannot be const because LoadMonitor has no const methods.
    bool isOverloaded ();

    /** Get the Job corresponding to a thread.  If no thread, use the current
        thread. */
    Job* getJobForThread(std::thread::id const& id = {}) const;

    // Cannot be const because LoadMonitor has no const methods.
    Json::Value getJson (int c = 0);

private:
    using JobDataMap = std::map <JobType, JobTypeData>;

    beast::Journal m_journal;
    mutable std::mutex m_mutex;
    std::uint64_t m_lastJob;
    std::set <Job> m_jobSet;
    JobDataMap m_jobData;
    JobTypeData m_invalidJobData;

    std::map <std::thread::id, Job*> m_threadIds;

    // The number of jobs currently in processTask()
    int m_processCount;

    beast::Workers m_workers;
    Job::CancelCallback m_cancelCallback;

    // Statistics tracking
    beast::insight::Collector::ptr m_collector;
    beast::insight::Gauge job_count;
    beast::insight::Hook hook;

    static JobTypes const& getJobTypes()
    {
        static JobTypes types;
        return types;
    }

    void collect();
    JobTypeData& getJobTypeData (JobType type);

    // Signals the service stopped if the stopped condition is met.
    void checkStopped (std::lock_guard <std::mutex> const& lock);

    // Signals an added Job for processing.
    //
    // Pre-conditions:
    //  The JobType must be valid.
    //  The Job must exist in mJobSet.
    //  The Job must not have previously been queued.
    //
    // Post-conditions:
    //  Count of waiting jobs of that type will be incremented.
    //  If JobQueue exists, and has at least one thread, Job will eventually run.
    //
    // Invariants:
    //  The calling thread owns the JobLock
    void queueJob (Job const& job, std::lock_guard <std::mutex> const& lock);

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
    void getNextJob (Job& job);

    // Indicates that a running Job has completed its task.
    //
    // Pre-conditions:
    //  Job must not exist in mJobSet.
    //  The JobType must not be invalid.
    //
    // Post-conditions:
    //  The running count of that JobType is decremented
    //  A new task is signaled if there are more waiting Jobs than the limit, if any.
    //
    // Invariants:
    //  <none>
    void finishJob (Job const& job);

    template <class Rep, class Period>
    void on_dequeue (JobType type,
        std::chrono::duration <Rep, Period> const& value);

    template <class Rep, class Period>
    void on_execute (JobType type,
        std::chrono::duration <Rep, Period> const& value);

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
    void processTask () override;

    // Returns `true` if all jobs of this type should be skipped when
    // the JobQueue receives a stop notification. If the job type isn't
    // skipped, the Job will be called and the job must call Job::shouldCancel
    // to determine if a long running or non-mandatory operation should be canceled.
    bool skipOnStop (JobType type);

    // Returns the limit of running jobs for the given job type.
    // For jobs with no limit, we return the largest int. Hopefully that
    // will be enough.
    int getJobLimit (JobType type);

    void onStop () override;
    void onChildrenStopped () override;
};

/*
    An RPC command is received and is handled via ServerHandler(HTTP) or
    Handler(websocket), depending on the connection type. The handler then calls
    the JobQueue::postCoro() method to create a coroutine and run it at a later
    point. This frees up the handler thread and allows it to continue handling
    other requests while the RPC command completes its work asynchronously.

    postCoro() creates a JobCoro object. When the JobCoro ctor is called, and its
    coro_ member is initialized(a boost::coroutines::pull_type), execution
    automatically passes to the coroutine, which we don't want at this point,
    since we are still in the handler thread context. It's important to note here
    that construction of a boost pull_type automatically passes execution to the
    coroutine. A pull_type object automatically generates a push_type that is
    used as the as a parameter(do_yield) in the signature of the function the
    pull_type was created with. This function is immediately called during coro_
    construction and within it, JobCoro::yield_ is assigned the push_type
    parameter(do_yield) address and called(yield()) so we can return execution
    back to the caller's stack.

    postCoro() then calls JobCoro::post(), which schedules a job on the job
    queue to continue execution of the coroutine in a JobQueue worker thread at
    some later time. When the job runs, we lock on the JobCoro::mutex_ and call
    coro_ which continues where we had left off. Since we the last thing we did
    in coro_ was call yield(), the next thing we continue with is calling the
    function param f, that was passed into JobCoro ctor. It is within this
    function body that the caller specifies what he would like to do while
    running in the coroutine and allow them to suspend and resume execution.
    A task that relies on other events to complete, such as path finding, calls
    JobCoro::yield() to suspend its execution while waiting on those events to
    complete and continue when signaled via the JobCoro::post() method.

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
        4- Before the coroutine can suspend, that event occurs and the resumption
            of the coroutine is scheduled on the job queue.
        5- Again, before the coroutine can suspend, the resumption of the coroutine
            is dispatched.
        6- Again, before the coroutine can suspend, the resumption code runs the
            coroutine.
        The coroutine is now running in two threads.

        The lock prevents this from happening as step 6 will block until the
            lock is released which only happens after the coroutine completes.
*/

} // ripple

#include <ripple/core/JobCoro.ipp>

namespace ripple {

template <class F>
void JobQueue::postCoro (JobType t, std::string const& name, F&& f)
{
    /*  First param is a detail type to make construction private.
        Last param is the function the coroutine runs. Signature of
        void(std::shared_ptr<JobCoro>).
    */
    auto const coro = std::make_shared<JobCoro>(
        detail::JobCoro_create_t{}, *this, t, name, std::forward<F>(f));
    coro->post();
}

}

#endif
