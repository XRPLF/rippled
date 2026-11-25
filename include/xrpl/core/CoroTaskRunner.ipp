#pragma once

/**
 * @file CoroTaskRunner.ipp
 *
 * CoroTaskRunner inline implementation.
 *
 * This file contains the business logic for managing C++20 coroutines
 * on the JobQueue. It is included at the bottom of JobQueue.h.
 *
 * Data Flow: suspend / post / resume cycle
 * =========================================
 *
 *         coroutine body                 CoroTaskRunner              JobQueue
 *         --------------                 --------------              --------
 *              |
 *    co_await runner->suspend()
 *              |
 *              +--- await_suspend ------> onSuspend()
 *              |                            ++nSuspend_ ------------> nSuspend_
 *              |                          [coroutine is now suspended]
 *              |
 *              .    (externally or by yieldAndPost())
 *              .
 *              +--- (caller calls) -----> post()
 *              |                            ++runCount_
 *              |                            addJob(resume) ----------> job enqueued
 *              |                                                        |
 *              |                                                      [worker picks up]
 *              |                                                        |
 *              +--- <----- resume() <-----------------------------------+
 *              |             --nSuspend_ ------> nSuspend_
 *              |             swap in LocalValues (lvs_)
 *              |             task_.handle().resume()
 *              |                |
 *              |    [coroutine body continues here]
 *              |                |
 *              |             swap out LocalValues
 *              |             --runCount_
 *              |             cv_.notify_all()
 *              v
 *
 * Thread Safety
 * =============
 * - mutex_     : guards task_.handle().resume() so that post()-before-suspend
 *                races cannot resume the coroutine while it is still running.
 *                (See the race condition discussion in JobQueue.h)
 * - mutexRun_ : guards runCount_ counter; used by join() to wait until
 *                all in-flight resume operations complete.
 * - jq_.mutex_: guards nSuspend_ increments/decrements.
 *
 * Common Mistakes When Modifying This File
 * =========================================
 *
 * 1. Changing lock ordering.
 *    resume() acquires locks sequentially (never held simultaneously):
 *    jq_.mutex_ (released immediately), then mutex_ (held across resume),
 *    then mutexRun_ (released after decrement). post() acquires only
 *    mutexRun_. Any new code path must follow the same order.
 *
 * 2. Removing the shared_from_this() capture in post().
 *    The lambda passed to addJob captures [this, sp = shared_from_this()].
 *    If you remove sp, 'this' can be destroyed before the job runs,
 *    causing use-after-free. The sp capture is load-bearing.
 *
 * 3. Forgetting to decrement nSuspend_ on a new code path.
 *    Every ++nSuspend_ must have a matching --nSuspend_. If you add a new
 *    suspension path (e.g. a new awaiter) and forget to decrement on resume
 *    or on failure, JobQueue::stop() will hang.
 *
 * 4. Calling task_.handle().resume() without holding mutex_.
 *    This allows a race where the coroutine runs on two threads
 *    simultaneously. Always hold mutex_ around resume().
 *
 * 5. Swapping LocalValues outside of the mutex_ critical section.
 *    The swap-in and swap-out of LocalValues must bracket the resume()
 *    call. If you move the swap-out before the lock_guard(mutex_) is
 *    released, you break LocalValue isolation for any code that runs
 *    after the coroutine suspends but before the lock is dropped.
 */

namespace xrpl {

/**
 * Construct a CoroTaskRunner. Sets runCount_ to 0; does not
 * create the coroutine. Call init() afterwards.
 *
 * @param jq   The JobQueue this coroutine will run on
 * @param type Job type for scheduling priority
 * @param name Human-readable name for logging
 */
inline JobQueue::CoroTaskRunner::CoroTaskRunner(
    CreateT,
    JobQueue& jq,
    JobType type,
    std::string name)
    : jq_(jq), type_(type), name_(std::move(name))
{
}

/**
 * Initialize with a coroutine-returning callable.
 * Stores the callable on the heap (FuncStore) so it outlives the
 * coroutine frame. Coroutine frames store a reference to the
 * callable's implicit object parameter (the lambda). If the callable
 * is a temporary, that reference dangles after the caller returns.
 * Keeping the callable alive here ensures the coroutine's captures
 * remain valid.
 *
 * @param f Callable: CoroTask<void>(shared_ptr<CoroTaskRunner>)
 */
template <class F>
void
JobQueue::CoroTaskRunner::init(F&& f)
{
    using Fn = std::decay_t<F>;
    auto store = std::make_unique<FuncStore<Fn>>(std::forward<F>(f));
    task_ = store->func(shared_from_this());
    storedFunc_ = std::move(store);
}

/**
 * Destructor. Waits for any in-flight resume() to complete, then
 * asserts (debug) that the coroutine has finished or
 * expectEarlyExit() was called.
 *
 * The join() call is necessary because with async dispatch the
 * coroutine runs on a worker thread. The gate signal (which wakes
 * the test thread) can arrive before resume() has set finished_.
 * join() synchronizes via mutexRun_, establishing a happens-before
 * edge: finished_ = true -> unlock(mutexRun_) in resume() ->
 * lock(mutexRun_) in join() -> read finished_.
 */
inline JobQueue::CoroTaskRunner::~CoroTaskRunner()
{
#ifndef NDEBUG
    join();
    XRPL_ASSERT(finished_, "xrpl::JobQueue::CoroTaskRunner::~CoroTaskRunner : is finished");
#endif
}

/**
 * Increment the JobQueue's suspended-coroutine count (nSuspend_).
 */
inline void
JobQueue::CoroTaskRunner::onSuspend()
{
    std::scoped_lock const lock(jq_.mutex_);
    ++jq_.nSuspend_;
}

/**
 * Decrement nSuspend_ without resuming.
 */
inline void
JobQueue::CoroTaskRunner::onUndoSuspend()
{
    std::scoped_lock const lock(jq_.mutex_);
    --jq_.nSuspend_;
}

/**
 * Return a SuspendAwaiter whose await_suspend() increments nSuspend_
 * before the coroutine actually suspends. The caller must later call
 * post() or resume() to continue execution.
 *
 * @return Awaiter for use with `co_await runner->suspend()`
 */
inline auto
JobQueue::CoroTaskRunner::suspend()
{
    /**
     * Custom awaiter for suspend(). Always suspends (await_ready
     * returns false) and increments nSuspend_ in await_suspend().
     */
    // The C++ coroutine protocol mandates these awaiter names and
    // instance-callable methods, which conflict with the project
    // naming/static conventions.
    // NOLINTBEGIN(readability-identifier-naming, readability-convert-member-functions-to-static)
    struct SuspendAwaiter
    {
        CoroTaskRunner& runner_;  // The runner that owns this coroutine.

        /**
         * Always returns false so the coroutine suspends.
         */
        [[nodiscard]] bool
        await_ready() const noexcept
        {
            return false;
        }

        /**
         * Called when the coroutine suspends. Increments nSuspend_
         * so the JobQueue knows a coroutine is waiting.
         */
        void
        await_suspend(std::coroutine_handle<>) const
        {
            runner_.onSuspend();
        }

        void
        await_resume() const noexcept
        {
        }
    };
    // NOLINTEND(readability-identifier-naming, readability-convert-member-functions-to-static)
    return SuspendAwaiter{*this};
}

/**
 * Suspend and immediately repost on the JobQueue. Equivalent to
 * `co_await JobQueueAwaiter{runner}` but uses an inline struct
 * to work around a GCC-12 codegen bug (see declaration in JobQueue.h).
 *
 * If the JobQueue is stopping (post fails), the suspend count is
 * undone and the coroutine continues immediately via symmetric
 * transfer back to its own handle.
 *
 * @return An inline YieldPostAwaiter
 */
inline auto
JobQueue::CoroTaskRunner::yieldAndPost()
{
    // The C++ coroutine protocol mandates these awaiter names and
    // instance-callable methods, which conflict with the project
    // naming/static conventions.
    // NOLINTBEGIN(readability-identifier-naming, readability-convert-member-functions-to-static)
    struct YieldPostAwaiter
    {
        CoroTaskRunner& runner_;

        [[nodiscard]] bool
        await_ready() const noexcept
        {
            return false;
        }

        /**
         * Returns a coroutine_handle<> (symmetric transfer) rather than
         * void + h.resume(). Two reasons:
         *
         *  1. h.resume() runs the coroutine nested inside this frame. A
         *     coroutine that yields in a loop against a stopping JobQueue
         *     fails post() every iteration, so the stack grows without
         *     bound. Symmetric transfer is a tail call and does not nest.
         *
         *  2. After h.resume() returns, the coroutine may have completed
         *     and destroyed its frame -- the frame this awaiter lives in.
         *     Returning from await_suspend would then touch freed memory.
         *
         * A bool return would also avoid nesting, but GCC-12 miscompiles
         * bool-returning await_suspend (see JobQueueAwaiter.h).
         *
         * @return noop_coroutine() to stay suspended (job posted);
         *         the caller's handle to continue now (JQ stopping)
         */
        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> h)
        {
            runner_.onSuspend();
            if (!runner_.post())
            {
                runner_.onUndoSuspend();
                return h;
            }
            return std::noop_coroutine();
        }

        void
        await_resume() const noexcept
        {
        }
    };
    // NOLINTEND(readability-identifier-naming, readability-convert-member-functions-to-static)
    return YieldPostAwaiter{*this};
}

/**
 * Schedule coroutine resumption as a job on the JobQueue.
 * A shared_ptr capture (sp) prevents this CoroTaskRunner from being
 * destroyed while the job is queued but not yet executed.
 *
 * @return false if the JobQueue rejected the job (shutting down)
 */
inline bool
JobQueue::CoroTaskRunner::post()
{
    {
        std::scoped_lock const lk(mutexRun_);
        ++runCount_;
    }

    // sp prevents 'this' from being destroyed while the job is pending
    if (jq_.addJob(type_, name_, [this, sp = shared_from_this()]() { resume(); }))
    {
        return true;
    }

    // The coroutine will not run. Undo the runCount_ increment.
    std::scoped_lock const lk(mutexRun_);
    --runCount_;
    cv_.notify_all();
    return false;
}

/**
 * Resume the coroutine on the current thread.
 *
 * Steps:
 *   1. Decrement nSuspend_ (under jq_.mutex_)
 *   2. Swap in this coroutine's LocalValues for thread-local isolation
 *   3. Resume the coroutine handle (under mutex_)
 *   4. Swap out LocalValues, restoring the thread's previous state
 *   5. Decrement runCount_ and notify join() waiters
 *
 * @pre post() must have been called before resume(). Direct calls
 *      without a prior post() will corrupt runCount_ and break join().
 * Note: runCount_ is NOT incremented here — post() already did that.
 * This ensures join() stays blocked for the entire post->resume lifetime.
 */
inline void
JobQueue::CoroTaskRunner::resume()
{
    {
        std::scoped_lock const lock(jq_.mutex_);
        --jq_.nSuspend_;
    }
    auto saved = detail::getLocalValues().release();
    detail::getLocalValues().reset(&lvs_);
    std::scoped_lock const lock(mutex_);
    XRPL_ASSERT(
        task_.handle() && !task_.done(),
        "xrpl::JobQueue::CoroTaskRunner::resume : task handle is valid and not done");
    if (task_.handle() && !task_.done())
    {
        task_.handle().resume();
    }
    else
    {
        // A resume() with no coroutine to run (e.g. a duplicate external
        // post() after completion). Resuming a null or finished handle is
        // undefined behavior, so skip it -- this matches the old
        // Coro::resume() `if (coro_)` guard. The bookkeeping below still
        // runs to balance the ++runCount_ done by the post() that
        // scheduled this call.
        JLOG(jq_.journal_.warn())
            << "CoroTaskRunner::resume called for coroutine '" << name_
            << "' with no runnable coroutine (duplicate post or already completed)";
    }
    detail::getLocalValues().release();
    detail::getLocalValues().reset(saved);
    if (task_.done())
    {
        finished_ = true;
        // An exception that escapes a top-level coroutine body is captured
        // by promise_type::unhandled_exception() but has no awaiter to
        // rethrow it, so it would vanish with the frame. Surface it in the
        // log. (The old Boost path propagated it out of resume() instead.)
        if (auto const& ep = task_.handle().promise().exception_)
        {
            try
            {
                std::rethrow_exception(ep);
            }
            catch (std::exception const& e)
            {
                JLOG(jq_.journal_.error())
                    << "Unhandled exception in coroutine '" << name_ << "': " << e.what();
            }
            catch (...)
            {
                JLOG(jq_.journal_.error())
                    << "Unhandled non-standard exception in coroutine '" << name_ << "'";
            }
        }
        // Break the shared_ptr cycle: frame -> shared_ptr<runner> -> this.
        // Use std::move (not task_ = {}) so task_.handle_ is null BEFORE the
        // frame is destroyed. operator= would destroy the frame while handle_
        // still holds the old value -- a re-entrancy hazard on GCC-12 if
        // frame destruction triggers runner cleanup.
        [[maybe_unused]] auto completed = std::move(task_);
    }
    std::scoped_lock const lk(mutexRun_);
    --runCount_;
    cv_.notify_all();
}

/**
 * @return true if the coroutine has not yet run to completion
 */
inline bool
JobQueue::CoroTaskRunner::runnable() const
{
    // After normal completion, task_ is reset to break the shared_ptr cycle
    // (handle_ becomes null). A null handle means the coroutine is done.
    return task_.handle() && !task_.done();
}

/**
 * Handle early termination when the coroutine never ran (e.g. JobQueue
 * is stopping). Decrements nSuspend_ and destroys the coroutine frame
 * to break the shared_ptr cycle: frame -> lambda -> runner -> frame.
 */
inline void
JobQueue::CoroTaskRunner::expectEarlyExit()
{
    if (!finished_)
    {
        std::scoped_lock const lock(jq_.mutex_);
        --jq_.nSuspend_;
        finished_ = true;
    }
    // Break the shared_ptr cycle: frame -> shared_ptr<runner> -> this.
    // The coroutine is at initial_suspend and never ran user code, so
    // destroying it is safe. Use std::move (not task_ = {}) so
    // task_.handle_ is null before the frame is destroyed.
    {
        [[maybe_unused]] auto completed = std::move(task_);
    }
    storedFunc_.reset();
}

/**
 * Block until all pending/active resume operations complete.
 * Uses cv_ + mutexRun_ to wait until runCount_ reaches 0 or
 * finished_ becomes true. The finished_ check handles the case
 * where resume() is called directly (without post()), which
 * decrements runCount_ below zero. In that scenario runCount_
 * never returns to 0, but finished_ becoming true guarantees
 * the coroutine is done and no more resumes will occur.
 *
 * Note: when join() returns via the finished_ disjunct, the final
 * resume() call may still be executing its post-completion
 * bookkeeping (the --runCount_ / notify after finished_ is set).
 * That is safe -- the coroutine body has fully completed and the
 * runner is kept alive by the resume job's shared_ptr -- but
 * callers must not assume resume() itself has returned.
 */
inline void
JobQueue::CoroTaskRunner::join()
{
    std::unique_lock<std::mutex> lk(mutexRun_);
    cv_.wait(lk, [this]() { return runCount_ == 0 || finished_; });
}

}  // namespace xrpl
