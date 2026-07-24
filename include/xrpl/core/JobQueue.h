#pragma once

#include <xrpl/basics/LocalValue.h>
#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/insight/Gauge.h>
#include <xrpl/beast/insight/Hook.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ClosureCounter.h>
#include <xrpl/core/CoroTask.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobTypeData.h>
#include <xrpl/core/LoadEvent.h>
#include <xrpl/core/detail/Workers.h>
#include <xrpl/json/json_value.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <type_traits>

namespace xrpl {

namespace perf {
class PerfLog;
}  // namespace perf

class Logs;

/**
 * A pool of threads to perform work.
 *
 * A job posted will always run to completion.
 *
 * Coroutines that are suspended must be resumed,
 * and run to completion.
 *
 * When the JobQueue stops, it waits for all jobs
 * and coroutines to finish.
 */
class JobQueue : private Workers::Callback
{
public:
    /**
     * C++20 coroutine lifecycle manager.
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
     *  | - mutexRun_    : std::mutex      (join guard)      |
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
     *    +-- reserve a jobCounter_ slot (reject if JQ shutting down)
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
     *      jq.postCoroTask(JtClient, "MyWork",
     *          [](auto runner) -> CoroTask<void> {
     *              doSomeWork();
     *              co_await runner->suspend();  // yield to other jobs
     *              doMoreWork();
     *              co_return;
     *          });
     *
     *  2. Manually controlling suspend / resume (external trigger):
     *
     *      auto runner = jq.postCoroTask(JtClient, "ExtTrigger",
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
     *      jq.postCoroTask(JtClient, "AutoRepost",
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
     *      jq.postCoroTask(JtClient, "Cancellable",
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
        // increment/decrement nSuspend_, and acquire jq_.mutex_.
        JobQueue& jq_;

        // Job type passed to addJob() when posting this coroutine.
        JobType type_;

        // Human-readable name for this coroutine job (for logging).
        std::string name_;

        // Number of in-flight resume operations (pending + active).
        // Incremented by post(), decremented when resume() finishes.
        // Guarded by mutexRun_. join() blocks until this reaches 0.
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
        std::mutex mutexRun_;

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
        // Atomic to allow lock-free reads from runnable(), join(), and
        // the destructor without requiring the same mutex that guards
        // the write in resume().
        std::atomic<bool> finished_{false};

    public:
        /**
         * Tag type for private construction. Prevents external code
         * from constructing CoroTaskRunner directly. Use postCoroTask().
         */
        struct CreateT
        {
            explicit CreateT() = default;
        };

        /**
         * Construct a CoroTaskRunner. Private by convention (CreateT tag).
         *
         * @param jq   The JobQueue this coroutine will run on
         * @param type Job type for scheduling priority
         * @param name Human-readable name for logging
         */
        CoroTaskRunner(CreateT, JobQueue&, JobType, std::string const&);

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
         * jq_.mutex_ -> mutex_ -> mutexRun_.
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
         * Uses cv_ + mutexRun_ to wait until runCount_ reaches 0.
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
    ~JobQueue() override;

    /**
     * Adds a job to the JobQueue.
     *
     * @param type The type of job.
     * @param name Name of the job.
     * @param jobHandler Callable with signature void(). Called when the job is executed.
     *
     * @return true if jobHandler added to queue.
     */
    template <typename JobHandler>
    bool
    addJob(JobType type, std::string const& name, JobHandler&& jobHandler)
        requires(std::is_void_v<std::invoke_result_t<JobHandler>>)
    {
        if (auto optionalCountedJob = jobCounter_.wrap(std::forward<JobHandler>(jobHandler)))
        {
            return addRefCountedJob(type, name, std::move(*optionalCountedJob));
        }
        return false;
    }

    /**
     * Creates a C++20 coroutine and adds a job to the queue to run it.
     *
     * @param t The type of job.
     * @param name Name of the job.
     * @param f Callable with signature
     * CoroTask<void>(std::shared_ptr<CoroTaskRunner>).
     *
     * @return shared_ptr to posted CoroTaskRunner. nullptr if not successful.
     */
    template <class F>
    std::shared_ptr<CoroTaskRunner>
    postCoroTask(JobType t, std::string const& name, F&& f);

    /**
     * Jobs waiting at this priority.
     */
    int
    getJobCount(JobType t) const;

    /**
     * Jobs waiting plus running at this priority.
     */
    int
    getJobCountTotal(JobType t) const;

    /**
     * All waiting jobs at or greater than this priority.
     */
    int
    getJobCountGE(JobType t) const;

    /**
     * Return a scoped LoadEvent.
     */
    std::unique_ptr<LoadEvent>
    makeLoadEvent(JobType t, std::string const& name);

    /**
     * Add multiple load events.
     */
    void
    addLoadEvents(JobType t, int count, std::chrono::milliseconds elapsed);

    // Cannot be const because LoadMonitor has no const methods.
    bool
    isOverloaded();

    // Cannot be const because LoadMonitor has no const methods.
    json::Value
    getJson(int c = 0);

    /**
     * Block until no jobs running.
     */
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
    using JobDataMap = std::map<JobType, JobTypeData>;

    beast::Journal journal_;
    mutable std::mutex mutex_;
    std::uint64_t lastJob_{0};
    std::set<Job> jobSet_;
    JobCounter jobCounter_;
    std::atomic_bool stopping_{false};
    std::atomic_bool stopped_{false};
    JobDataMap jobData_;
    JobTypeData invalidJobData_;

    // The number of jobs currently in processTask()
    int processCount_{0};

    // The number of suspended coroutines
    int nSuspend_ = 0;

    Workers workers_;

    // Statistics tracking
    perf::PerfLog& perfLog_;
    beast::insight::Collector::ptr collector_;
    beast::insight::Gauge jobCount_;
    beast::insight::Hook hook_;

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
    //  jobSet_ must not be empty.
    //  jobSet_ holds at least one RunnableJob
    //
    // Post-conditions:
    //  job is a valid Job object.
    //  job is removed from jobQueue_.
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
    //  Job must not exist in jobSet_.
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

}  // namespace xrpl

#include <xrpl/core/CoroTaskRunner.ipp>  // IWYU pragma: keep

namespace xrpl {

// postCoroTask — entry point for launching a C++20 coroutine on the JobQueue.
//
// Control Flow
// ============
//
//   postCoroTask(t, name, f)
//     |
//     +-- 1. Reserve a jobCounter_ slot — reject if JQ shutting down
//     |
//     +-- 2. ++nSuspend_   (the coroutine uses lazy-start, so it is
//     |        "suspended" from the JQ's perspective before its first resume.
//     |        This keeps the JQ shutdown logic correct — it waits for
//     |        nSuspend_ to reach 0).
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
//     |
//     +-- 6. Release the jobCounter_ slot (on return)
//
// Why reserve a jobCounter_ slot?
// ===============================
// JobQueue::stop() joins jobCounter_ before it acquires mutex_ and asserts
// nSuspend_ == 0.  Without a reservation, stop() could run to completion in
// the window between the ++nSuspend_ in step 2 and the balancing post() or
// expectEarlyExit() in step 5, tripping that assert.  Holding a slot blocks
// stop()'s join() for the whole function, closing the window.  wrap() also
// returns nullopt once the counter is joined, so it doubles as the shutdown
// check — a plain stopping_ read cannot do this, because the read and the
// ++nSuspend_ are not a single atomic step.
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
    // Held until this function returns. Null once jobCounter_ is joined,
    // which is how a shutting-down JobQueue rejects new coroutines.
    auto const shutdownGuard = jobCounter_.wrap([]() {});
    if (!shutdownGuard)
        return nullptr;

    // Account for the initial suspension (CoroTask uses lazy start). Reject
    // if the JQ is shutting down and increment nSuspend_ under the same
    // lock. Without the lock, a TOCTOU race exists: stopping_ could become
    // true between the check and the increment, leaving an orphan nSuspend_
    // that causes stop() to hang.
    {
        std::lock_guard lock(mutex_);
        if (stopping_)
            return nullptr;
        ++nSuspend_;
    }

    auto runner = std::make_shared<CoroTaskRunner>(CoroTaskRunner::CreateT{}, *this, t, name);
    runner->init(std::forward<F>(f));
    if (!runner->post())
    {
        runner->expectEarlyExit();
        runner.reset();
    }
    return runner;
}

}  // namespace xrpl
