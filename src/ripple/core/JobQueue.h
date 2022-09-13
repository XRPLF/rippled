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

/** A pool of threads to perform work.

    A job posted will always run to completion.

    Coroutines that are suspended must be resumed,
    and run to completion.

    When the JobQueue stops, it waits for all jobs
    and coroutines to finish.
*/
class JobQueue : private Workers::Callback
{
    // A small class used to prevent construction of coroutines without
    // going through the JobQueue APIs.
    struct CoroCreator
    {
        explicit constexpr CoroCreator() = default;
    };

public:
    /** Coroutines must run to completion. */
    class Coro : public std::enable_shared_from_this<Coro>,
                 public CountedObject<Coro>
    {
    private:
        detail::LocalValues lvs_;
        JobQueue& jq_;
        JobType const type_;
        std::string const name_;
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
        template <class F>
        Coro(
            JobQueue::CoroCreator,
            JobQueue& jq,
            JobType type,
            std::string const& name,
            F&& f);

        Coro(Coro const&) = delete;
        Coro&
        operator=(Coro const&) = delete;

        Coro(Coro&&) = delete;
        Coro&
        operator=(Coro&&) = delete;

        virtual ~Coro();

        /** Suspend the execution of a running coroutine.

            The coroutine's stack is saved and the job thread on which the
            coroutine is executed is released back to its thread pool.

            @note Calling this when the coroutine is already suspended will
                  result in undefined behavior. A call to post() or resume()
                  must come first.
        */
        void
        yield() const;

        /** Schedule a job to execute the coroutine.

            Once the job starts running, the coroutine execution context is
            set up and execution begins either at the start of the coroutine
            or, if yield() was previous called, at the statement after that
            yield().

            @note Calling this can result in undefined behavior if
                  - the coroutine has finished executing by using 'return'
                    instead of a call to 'yield()'; or
                  - the coroutine is either executing or scheduled for
                    execution.

            @return true if the coro was scheduled on the job queue; false
                    otherwise.
        */
        bool
        post();

        /** Resume execution of a suspended coroutine on the current thread.

            The coroutine will continues execution from where it last left,
            i.e. in the statement following the yield() that the corooutine
            executed last.

            @note Calling this when the coroutine has either terminated its
                  own execution (by calling `return` instead of doing a call
                  to yield()) or is either scheduled for execution or is
                  already executing will result in undefined behavior.
        */
        void
        resume();

        /** true if the coroutine is still runnable (i.e. has not returned). */
        bool
        runnable() const;

        /** Once called, the Coro allows early exit without an assert. */
        void
        expectEarlyExit();

        /** Waits until coroutine returns from the user function. */
        void
        join();
    };

    using JobFunction = std::function<void()>;

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
            decltype(std::declval<JobHandler&&>()()),
            void>::value>>
    bool
    addJob(JobType type, std::string const& name, JobHandler&& jobHandler)
    {
        if (auto optionalCountedJob =
                jobCounter_.wrap(std::forward<JobHandler>(jobHandler)))
            return addRefCountedJob(type, name, std::move(*optionalCountedJob));
        return false;
    }

    /** Creates a coroutine and adds a job to the queue which will run it.

        @param t The type of job.
        @param name Name of the job.
        @param f The actual coroutine body; has a signature of:
                 void(std::shared_ptr<Coro>)

        @return shared_ptr to posted Coro. nullptr if post was not successful.
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

    /** Returns a new load event for the particular job */
    LoadEvent
    createLoadEvent(JobType t, std::string name);

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

    /** Returns the number of threads that this job queue is configured with */
    int
    getThreadCount() const
    {
        return workers_.count();
    }

private:
    friend class Coro;

    beast::Journal journal_;
    mutable std::mutex mutex_;
    std::uint64_t nextId_ = 0;
    std::set<Job> jobSet_;
    JobCounter jobCounter_;
    std::atomic_bool stopping_{false};
    std::atomic_bool stopped_{false};
    std::array<JobTypeData, jobTypes.size()> jobData_;

    // The number of jobs currently in processTask()
    int activeThreads_ = 0;

    // The number of coroutines (active or suspended)
    std::atomic<int> totalCoroutines_ = 0;

    // The number of suspended coroutines
    std::atomic<int> suspendedCoroutines_ = 0;

    Workers workers_;

    // Statistics tracking
    perf::PerfLog& perfLog_;
    beast::insight::Collector::ptr collector_;
    beast::insight::Gauge jobCountGauge_;
    beast::insight::Gauge activeThreadsGauge_;
    beast::insight::Hook hook_;

    std::condition_variable cv_;

    void
    collect();

    // Adds a reference counted job to the JobQueue.
    //
    //    param type The type of job.
    //    param name Name of the job.
    //    param func A std::function<void()> that is called to do the work.
    //
    //    return true if func added to queue.
    bool
    addRefCountedJob(JobType type, std::string const& name, JobFunction func);

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
    processTask(unsigned int instance) override;

    // Called when an uncaught exception occurs while processing a task
    //
    // Invariants:
    //  <none>
    void
    uncaughtException(unsigned int instance, std::exception_ptr eptr) override;
};

/** RPC command handling details:

    An RPC command is received and is handled via ServerHandler(HTTP) or
    Handler(websocket), depending on the connection type. The handler then calls
    the JobQueue::postCoro() method to create a coroutine and run it at a later
    point. This frees up the handler thread and allows it to continue handling
    other requests while the RPC command completes its work asynchronously.

    postCoro() creates a Coro object. When the Coro ctor is called, and its
    coro_ member is initialized (a boost::coroutines::pull_type), execution
    automatically passes to the coroutine, which we don't want at this point,
    since we are still in the handler thread context. It's important to note
    here that construction of a boost pull_type automatically passes execution
    to the coroutine. A pull_type object automatically generates a push_type
    that is passed as a parameter (do_yield) in the signature of the function
    the pull_type was created with. This function is immediately called during
    coro_ construction and within it, Coro::yield_ is assigned the push_type
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

    1. The coroutine is running.
    2. The coroutine is about to suspend, but before it can do so, it must
       arrange for some event to wake it up.
    3. The coroutine arranges for some event to wake it up.
    4. Before the coroutine can suspend, that event occurs and the
       resumption of the coroutine is scheduled on the job queue.
    5. Again, before the coroutine can suspend, the resumption of the coroutine
       is dispatched.
    6. Again, before the coroutine can suspend, the resumption code runs the
       coroutine.

    The coroutine is now running in two threads. The lock prevents this from
    happening as step 6 will block until the lock is released which only
    happens after the coroutine completes.
*/

}  // namespace ripple

#include <ripple/core/Coro.ipp>

namespace ripple {

template <class F>
std::shared_ptr<JobQueue::Coro>
JobQueue::postCoro(JobType t, std::string const& name, F&& f)
{
    if (stopping_ || stopped_)
        return nullptr;

    auto coro = std::make_shared<Coro>(
        CoroCreator{}, *this, t, name, std::forward<F>(f));

    if (!coro->post())
    {
        // The coroutine was not successfully posted, so we disable it. That
        // way its destructor can run with no negative side effects; then we
        // can destroy it.
        coro->expectEarlyExit();
        coro.reset();
    }

    return coro;
}

}  // namespace ripple

#endif
