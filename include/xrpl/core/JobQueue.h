#pragma once

#include <xrpl/basics/LocalValue.h>
#include <xrpl/core/ClosureCounter.h>
#include <xrpl/core/CoroTask.h>
#include <xrpl/core/JobTypeData.h>
#include <xrpl/core/JobTypes.h>
#include <xrpl/core/detail/Workers.h>
#include <xrpl/json/json_value.h>

#include <boost/context/protected_fixedsize_stack.hpp>
#include <boost/coroutine2/all.hpp>

#include <coroutine>
#include <set>

namespace xrpl {

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
        boost::coroutines2::coroutine<void>::pull_type coro_;
        boost::coroutines2::coroutine<void>::push_type* yield_;
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

    /** C++20 coroutine lifecycle manager. Replaces Coro for new code.
     *
     *  Class / Inheritance / Dependency Diagram
     *  =========================================
     *
     *  std::enable_shared_from_this<CoroTaskRunner>
     *            ^
     *            | (public inheritance)
     *            |
     *  CoroTaskRunner
     *  +---------------------------------------------------+
     *  | - lvs_         : detail::LocalValues               |
     *  | - jq_          : JobQueue&                         |
     *  | - type_        : JobType                           |
     *  | - name_        : std::string                       |
     *  | - runCount_    : int         (in-flight resumes)    |
     *  | - mutex_       : std::mutex      (coroutine guard) |
     *  | - mutex_run_   : std::mutex      (join guard)      |
     *  | - cv_          : condition_variable                 |
     *  | - task_        : CoroTask<void>                    |
     *  | - storedFunc_  : unique_ptr<FuncBase> (type-erased)|
     *  +---------------------------------------------------+
     *  | + init(F&&)         : set up coroutine callable    |
     *  | + onSuspend()       : ++jq_.nSuspend_              |
     *  | + onUndoSuspend()   : --jq_.nSuspend_              |
     *  | + suspend()         : returns SuspendAwaiter       |
     *  | + post()            : schedule resume on JobQueue  |
     *  | + resume()          : resume coroutine on caller   |
     *  | + runnable()        : !task_.done()                |
     *  | + expectEarlyExit() : teardown for failed post     |
     *  | + join()            : block until not running       |
     *  +---------------------------------------------------+
     *          |                          |
     *          | owns                     | references
     *          v                          v
     *    CoroTask<void>             JobQueue
     *    (coroutine frame)          (thread pool + nSuspend_)
     *
     *    FuncBase / FuncStore<F>    (type-erased heap storage
     *                                for the coroutine lambda)
     *
     *  Coroutine Lifecycle (Control Flow)
     *  ===================================
     *
     *  Caller thread              JobQueue worker thread
     *  -------------              ----------------------
     *  postCoroTask(f)
     *    |
     *    +-- check stopping_ (reject if JQ shutting down)
     *    +-- ++nSuspend_  (lazy start counts as suspended)
     *    +-- make_shared<CoroTaskRunner>
     *    +-- init(f)
     *    |     +-- store lambda on heap (FuncStore)
     *    |     +-- task_ = f(shared_from_this())
     *    |           [coroutine created, suspended at initial_suspend]
     *    +-- post()
     *    |     +-- ++runCount_
     *    |     +-- addJob(type_, [resume]{})
     *    |                                    resume()
     *    |                                      |
     *    |                                      +-- --nSuspend_
     *    |                                      +-- swap in LocalValues
     *    |                                      +-- task_.handle().resume()
     *    |                                      |     [coroutine body runs]
     *    |                                      |     ...
     *    |                                      |     co_await suspend()
     *    |                                      |       +-- ++nSuspend_
     *    |                                      |       [coroutine suspends]
     *    |                                      +-- swap out LocalValues
     *    |                                      +-- --runCount_
     *    |                                      +-- cv_.notify_all()
     *    |
     *  post()  <-- called externally or by yieldAndPost()
     *    +-- ++runCount_
     *    +-- addJob(type_, [resume]{})
     *                                         resume()
     *                                           |
     *                                           +-- [coroutine body continues]
     *                                           +-- co_return
     *                                           +-- --runCount_
     *                                           +-- cv_.notify_all()
     *  join()
     *    +-- cv_.wait([]{runCount_ == 0})
     *    +-- [done]
     *
     *  Usage Examples
     *  ==============
     *
     *  1. Fire-and-forget coroutine (most common pattern):
     *
     *      jq.postCoroTask(jtCLIENT, "MyWork",
     *          [](auto runner) -> CoroTask<void> {
     *              doSomeWork();
     *              co_await runner->suspend();  // yield to other jobs
     *              doMoreWork();
     *              co_return;
     *          });
     *
     *  2. Manually controlling suspend / resume (external trigger):
     *
     *      auto runner = jq.postCoroTask(jtCLIENT, "ExtTrigger",
     *          [&result](auto runner) -> CoroTask<void> {
     *              startAsyncOperation(callback);
     *              co_await runner->suspend();
     *              // callback called runner->post() to get here
     *              result = collectResult();
     *              co_return;
     *          });
     *      // ... later, from the callback:
     *      runner->post();   // reschedule the coroutine on the JobQueue
     *
     *  3. Using yieldAndPost() for automatic suspend + repost:
     *
     *      jq.postCoroTask(jtCLIENT, "AutoRepost",
     *          [](auto runner) -> CoroTask<void> {
     *              step1();
     *              co_await runner->yieldAndPost();  // yield + auto-repost
     *              step2();
     *              co_await runner->yieldAndPost();
     *              step3();
     *              co_return;
     *          });
     *
     *  4. Checking shutdown after co_await (cooperative cancellation):
     *
     *      jq.postCoroTask(jtCLIENT, "Cancellable",
     *          [&jq](auto runner) -> CoroTask<void> {
     *              while (moreWork()) {
     *                  co_await runner->yieldAndPost();
     *                  if (jq.isStopping())
     *                      co_return;  // bail out cleanly
     *                  processNextItem();
     *              }
     *              co_return;
     *          });
     *
     *  Caveats / Pitfalls
     *  ==================
     *
     *  BUG-RISK: Calling suspend() without a matching post()/resume().
     *    After co_await runner->suspend(), the coroutine is parked and
     *    nSuspend_ is incremented.  If nothing ever calls post() or
     *    resume(), the coroutine is leaked and JobQueue::stop() will
     *    hang forever waiting for nSuspend_ to reach zero.
     *
     *  BUG-RISK: Calling post() on an already-running coroutine.
     *    post() schedules a resume() job.  If the coroutine has not
     *    actually suspended yet (no co_await executed), the resume job
     *    will try to call handle().resume() while the coroutine is still
     *    running on another thread.  This is UB.  The mutex_ prevents
     *    data corruption but the logic is wrong — always co_await
     *    suspend() before calling post().  (The test incorrect_order()
     *    shows this works only because mutex_ serializes the calls.)
     *
     *  BUG-RISK: Dropping the shared_ptr<CoroTaskRunner> before join().
     *    The CoroTaskRunner destructor asserts that finished_ is true
     *    (the coroutine completed). If you let the last shared_ptr die
     *    while the coroutine is still running or suspended, you get an
     *    assertion failure in debug and UB in release.  Always call
     *    join() or expectEarlyExit() first.
     *
     *  BUG-RISK: Lambda captures outliving the coroutine frame.
     *    The lambda passed to postCoroTask is heap-allocated (FuncStore)
     *    to prevent dangling.  But objects captured by pointer still need
     *    their own lifetime management.  If you capture a raw pointer to
     *    a stack variable, and the stack frame exits before the coroutine
     *    finishes, the pointer dangles.  Use shared_ptr or ensure the
     *    pointed-to object outlives the coroutine.
     *
     *  BUG-RISK: Forgetting co_return in a void coroutine.
     *    If the coroutine body falls off the end without co_return,
     *    the compiler may silently treat it as co_return (per standard),
     *    but some compilers warn.  Always write explicit co_return.
     *
     *  LIMITATION: CoroTaskRunner only supports CoroTask<void>.
     *    The task_ member is CoroTask<void>.  To return values from
     *    the top-level coroutine, write through a captured pointer
     *    (as the tests demonstrate), or co_await inner CoroTask<T>
     *    coroutines that return values.
     *
     *  LIMITATION: One coroutine per CoroTaskRunner.
     *    init() must be called exactly once.  You cannot reuse a
     *    CoroTaskRunner to run a second coroutine.  Create a new one
     *    via postCoroTask() instead.
     *
     *  LIMITATION: No timeout on join().
     *    join() blocks indefinitely.  If the coroutine is suspended
     *    and never posted, join() will deadlock.  Use timed waits
     *    on the gate pattern (condition_variable + wait_for) in tests.
     */
    class CoroTaskRunner : public std::enable_shared_from_this<CoroTaskRunner>
    {
    private:
        // Per-coroutine thread-local storage. Swapped in before resume()
        // and swapped out after, so each coroutine sees its own LocalValue
        // state regardless of which worker thread executes it.
        detail::LocalValues lvs_;

        // Back-reference to the owning JobQueue. Used to post jobs,
        // increment/decrement nSuspend_, and acquire jq_.m_mutex.
        JobQueue& jq_;

        // Job type passed to addJob() when posting this coroutine.
        JobType type_;

        // Human-readable name for this coroutine job (for logging).
        std::string name_;

        // Number of in-flight resume operations (pending + active).
        // Incremented by post(), decremented when resume() finishes.
        // Guarded by mutex_run_. join() blocks until this reaches 0.
        //
        // A counter (not a bool) is needed because post() can be called
        // from within the coroutine body (e.g. via yieldAndPost()),
        // enqueuing a second resume while the first is still running.
        // A bool would be clobbered: R2.post() sets true, then R1's
        // cleanup sets false — losing the fact that R2 is still pending.
        int runCount_;

        // Serializes all coroutine resume() calls, preventing concurrent
        // execution of the coroutine body on multiple threads. Handles the
        // race where post() enqueues a resume before the coroutine has
        // actually suspended (post-before-suspend pattern).
        std::mutex mutex_;

        // Guards runCount_. Used with cv_ for join() to wait
        // until all pending/active resume operations complete.
        std::mutex mutex_run_;

        // Notified when runCount_ reaches zero, allowing
        // join() waiters to wake up.
        std::condition_variable cv_;

        // The coroutine handle wrapper. Owns the coroutine frame.
        // Set by init(). Reset to empty in resume() upon coroutine
        // completion (to break the shared_ptr cycle) or in
        // expectEarlyExit() on early termination.
        CoroTask<void> task_;

        /**
         * Type-erased base for heap-stored callables.
         * Prevents the coroutine lambda from being destroyed before
         * the coroutine frame is done with it.
         *
         * @see FuncStore
         */
        struct FuncBase
        {
            virtual ~FuncBase() = default;
        };

        /**
         * Concrete type-erased storage for a callable of type F.
         * The coroutine frame stores a reference to the lambda's implicit
         * object parameter. If the lambda is a temporary, that reference
         * dangles after the call returns. FuncStore keeps it alive on
         * the heap for the lifetime of the CoroTaskRunner.
         */
        template <class F>
        struct FuncStore : FuncBase
        {
            F func;  // The stored callable (coroutine lambda).
            explicit FuncStore(F&& f) : func(std::move(f))
            {
            }
        };

        // Heap-allocated callable storage. Set by init(), ensures the
        // lambda outlives the coroutine frame that references it.
        std::unique_ptr<FuncBase> storedFunc_;

        // True once the coroutine has completed or expectEarlyExit() was
        // called. Asserted in the destructor (debug) to catch leaked
        // runners. Available in all builds to guard expectEarlyExit()
        // against double-decrementing nSuspend_.
        bool finished_ = false;

    public:
        /**
         * Tag type for private construction. Prevents external code
         * from constructing CoroTaskRunner directly. Use postCoroTask().
         */
        struct create_t
        {
            explicit create_t() = default;
        };

        /**
         * Construct a CoroTaskRunner. Private by convention (create_t tag).
         *
         * @param jq   The JobQueue this coroutine will run on
         * @param type Job type for scheduling priority
         * @param name Human-readable name for logging
         */
        CoroTaskRunner(create_t, JobQueue&, JobType, std::string const&);

        CoroTaskRunner(CoroTaskRunner const&) = delete;
        CoroTaskRunner&
        operator=(CoroTaskRunner const&) = delete;

        /**
         * Destructor. Asserts (debug) that the coroutine has finished
         * or expectEarlyExit() was called.
         */
        ~CoroTaskRunner();

        /**
         * Initialize with a coroutine-returning callable.
         * Must be called exactly once, after the object is managed by
         * shared_ptr (because init uses shared_from_this internally).
         * This is handled automatically by postCoroTask().
         *
         * @param f Callable: CoroTask<void>(shared_ptr<CoroTaskRunner>)
         */
        template <class F>
        void
        init(F&& f);

        /**
         * Increment the JobQueue's suspended-coroutine count (nSuspend_).
         * Called when the coroutine is about to suspend. Every call
         * must be balanced by a corresponding decrement (via resume()
         * or onUndoSuspend()), or JobQueue::stop() will hang.
         */
        void
        onSuspend();

        /**
         * Decrement nSuspend_ without resuming.
         * Used to undo onSuspend() when a scheduled post() fails
         * (e.g. JobQueue is stopping).
         */
        void
        onUndoSuspend();

        /**
         * Suspend the coroutine.
         * The awaiter's await_suspend() increments nSuspend_ before the
         * coroutine actually suspends. The caller must later call post()
         * or resume() to continue execution.
         *
         * @return An awaiter for use with `co_await runner->suspend()`
         */
        auto
        suspend();

        /**
         * Suspend the coroutine and immediately repost it on the
         * JobQueue. Combines suspend() + post() atomically inside
         * await_suspend, so there is no window where an external
         * event could race between the two.
         *
         * Equivalent to JobQueueAwaiter but defined as an inline
         * awaiter returned from a member function. This avoids a
         * GCC-12 coroutine codegen bug where an external awaiter
         * struct (JobQueueAwaiter) used at multiple co_await points
         * corrupts the coroutine state machine's resume index,
         * causing the coroutine to hang on the third resumption.
         *
         * @return An awaiter for use with `co_await runner->yieldAndPost()`
         */
        auto
        yieldAndPost();

        /**
         * Schedule coroutine resumption as a job on the JobQueue.
         * Captures shared_from_this() to prevent this runner from being
         * destroyed while the job is queued.
         *
         * @return true if the job was accepted; false if the JobQueue
         *         is stopping (caller must handle cleanup)
         */
        bool
        post();

        /**
         * Resume the coroutine on the current thread.
         * Decrements nSuspend_, swaps in LocalValues, resumes the
         * coroutine handle, swaps out LocalValues, and notifies join()
         * waiters. Lock ordering (sequential, non-overlapping):
         * jq_.m_mutex -> mutex_ -> mutex_run_.
         *
         * @pre post() must have been called before resume(). Direct
         *      calls without a prior post() will corrupt runCount_
         *      and break join().
         */
        void
        resume();

        /**
         * @return true if the coroutine has not yet run to completion
         */
        bool
        runnable() const;

        /**
         * Handle early termination when the coroutine never ran.
         * Decrements nSuspend_ and destroys the coroutine frame to
         * break the shared_ptr cycle (frame -> lambda -> runner -> frame).
         * Called by postCoroTask() when post() fails.
         */
        void
        expectEarlyExit();

        /**
         * Block until all pending/active resume operations complete.
         * Uses cv_ + mutex_run_ to wait until runCount_ reaches 0.
         * Warning: deadlocks if the coroutine is suspended and never posted.
         */
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
        typename =
            std::enable_if_t<std::is_same<decltype(std::declval<JobHandler&&>()()), void>::value>>
    bool
    addJob(JobType type, std::string const& name, JobHandler&& jobHandler)
    {
        if (auto optionalCountedJob = jobCounter_.wrap(std::forward<JobHandler>(jobHandler)))
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

    /** Creates a C++20 coroutine and adds a job to the queue to run it.

        @param t The type of job.
        @param name Name of the job.
        @param f Callable with signature
            CoroTask<void>(std::shared_ptr<CoroTaskRunner>).

        @return shared_ptr to posted CoroTaskRunner. nullptr if not successful.
    */
    template <class F>
    std::shared_ptr<CoroTaskRunner>
    postCoroTask(JobType t, std::string const& name, F&& f);

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
    addRefCountedJob(JobType type, std::string const& name, JobFunction const& func);

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
    static int
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

}  // namespace xrpl

#include <xrpl/core/Coro.ipp>
#include <xrpl/core/CoroTaskRunner.ipp>

namespace xrpl {

template <class F>
std::shared_ptr<JobQueue::Coro>
JobQueue::postCoro(JobType t, std::string const& name, F&& f)
{
    /*  First param is a detail type to make construction private.
        Last param is the function the coroutine runs. Signature of
        void(std::shared_ptr<Coro>).
    */
    auto coro = std::make_shared<Coro>(Coro_create_t{}, *this, t, name, std::forward<F>(f));
    if (!coro->post())
    {
        // The Coro was not successfully posted.  Disable it so it's destructor
        // can run with no negative side effects.  Then destroy it.
        coro->expectEarlyExit();
        coro.reset();
    }
    return coro;
}

// postCoroTask — entry point for launching a C++20 coroutine on the JobQueue.
//
// Control Flow
// ============
//
//   postCoroTask(t, name, f)
//     |
//     +-- 1. Check stopping_ — reject if JQ shutting down
//     |
//     +-- 2. ++nSuspend_   (mirrors Boost Coro ctor's implicit yield)
//     |        The coroutine is "suspended" from the JobQueue's perspective
//     |        even though it hasn't run yet — this keeps the JQ shutdown
//     |        logic correct (it waits for nSuspend_ to reach 0).
//     |
//     +-- 3. Create CoroTaskRunner (shared_ptr, ref-counted)
//     |
//     +-- 4. runner->init(f)
//     |        +-- Heap-allocate the lambda (FuncStore) to prevent
//     |        |   dangling captures in the coroutine frame
//     |        +-- task_ = f(shared_from_this())
//     |              [coroutine created but NOT started — lazy initial_suspend]
//     |
//     +-- 5. runner->post()
//     |        +-- addJob(type_, [resume]{})   → resume on worker thread
//     |        +-- failure (JQ stopping):
//     |              +-- runner->expectEarlyExit()
//     |              |     --nSuspend_, destroy coroutine frame
//     |              +-- return nullptr
//
// Why async post() instead of synchronous resume()?
// ==================================================
// The initial dispatch MUST use async post() so the coroutine body runs on
// a JobQueue worker thread, not the caller's thread.  resume() swaps the
// caller's thread-local LocalValues with the coroutine's private copy.
// If the coroutine mutates LocalValues (e.g. thread_specific_storage test),
// those mutations bleed back into the caller's thread-local state after the
// swap-out, corrupting subsequent tests that share the same thread pool.
// Async post() avoids this by running the coroutine on a worker thread whose
// LocalValues are managed by the thread pool, not by the caller.
//
template <class F>
std::shared_ptr<JobQueue::CoroTaskRunner>
JobQueue::postCoroTask(JobType t, std::string const& name, F&& f)
{
    // Reject if the JQ is shutting down — matches addJob()'s stopping_ check.
    // Must check before incrementing nSuspend_ to avoid leaving an orphan
    // count that would cause stop() to hang.
    if (stopping_)
        return nullptr;

    {
        std::lock_guard lock(m_mutex);
        ++nSuspend_;
    }

    auto runner = std::make_shared<CoroTaskRunner>(CoroTaskRunner::create_t{}, *this, t, name);
    runner->init(std::forward<F>(f));
    if (!runner->post())
    {
        runner->expectEarlyExit();
        runner.reset();
    }
    return runner;
}

}  // namespace xrpl
