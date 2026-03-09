#pragma once

#include <xrpl/core/JobQueue.h>

#include <coroutine>
#include <memory>

namespace xrpl {

/**
 * Awaiter that suspends and immediately reschedules on the JobQueue.
 * Equivalent to calling yield() followed by post() in the old Coro API.
 *
 * Usage:
 *     co_await JobQueueAwaiter{runner};
 *
 * What it waits for: The coroutine is re-queued as a job and resumes
 * when a worker thread picks it up.
 *
 * Which thread resumes: A JobQueue worker thread.
 *
 * What await_resume() returns: void.
 *
 * Dependency Diagram
 * ==================
 *
 *   JobQueueAwaiter
 *   +----------------------------------------------+
 *   | + runner : shared_ptr<CoroTaskRunner>         |
 *   +----------------------------------------------+
 *   | + await_ready()   -> false (always suspend)   |
 *   | + await_suspend() -> bool (suspend or cancel) |
 *   | + await_resume()  -> void                     |
 *   +----------------------------------------------+
 *          |                         |
 *          | uses                    | uses
 *          v                         v
 *   CoroTaskRunner              JobQueue
 *   .onSuspend()                (via runner->post() -> addJob)
 *   .onUndoSuspend()
 *   .post()
 *
 * Control Flow (await_suspend)
 * ============================
 *
 *   co_await JobQueueAwaiter{runner}
 *     |
 *     +-- await_ready() -> false
 *     +-- await_suspend(handle)
 *           |
 *           +-- runner->onSuspend()     // ++nSuspend_
 *           +-- runner->post()          // addJob to JobQueue
 *           |     |
 *           |     +-- success? return noop_coroutine()
 *           |     |     // coroutine stays suspended;
 *           |     |     // worker thread will call resume()
 *           |     +-- failure? (JQ stopping)
 *           |           +-- runner->onUndoSuspend()  // --nSuspend_
 *           |           +-- return handle  // symmetric transfer back
 *           |                              // coroutine continues immediately
 *           |                              // so it can clean up and co_return
 *
 * DEPRECATED — prefer `co_await runner->yieldAndPost()`
 * =====================================================
 *
 * GCC-12 has a coroutine codegen bug where using this external awaiter
 * struct at multiple co_await points in the same coroutine corrupts the
 * state machine's resume index. After the second co_await, the third
 * resumption enters handle().resume() but never reaches await_resume()
 * or any subsequent user code — the coroutine hangs indefinitely.
 *
 * The fix is `co_await runner->yieldAndPost()`, which defines the
 * awaiter as an inline struct inside a CoroTaskRunner member function.
 * GCC-12 handles inline awaiters correctly at multiple co_await points.
 *
 * This struct is retained for single-use scenarios and documentation
 * purposes. For any code that may use co_await in a loop or at
 * multiple points, always use `runner->yieldAndPost()`.
 *
 * Usage Examples
 * ==============
 *
 * 1. Yield and auto-repost (preferred — works on all compilers):
 *
 *     CoroTask<void> handler(auto runner) {
 *         doPartA();
 *         co_await runner->yieldAndPost();   // yield + repost
 *         doPartB();                         // runs on a worker thread
 *         co_return;
 *     }
 *
 * 2. Multiple yield points in a loop:
 *
 *     CoroTask<void> batchProcessor(auto runner) {
 *         for (auto& item : items) {
 *             process(item);
 *             co_await runner->yieldAndPost();  // let other jobs run
 *         }
 *         co_return;
 *     }
 *
 * 3. Graceful shutdown — checking after resume:
 *
 *     CoroTask<void> longTask(auto runner, JobQueue& jq) {
 *         while (hasWork()) {
 *             co_await runner->yieldAndPost();
 *             // If JQ is stopping, await_suspend resumes the coroutine
 *             // immediately without re-queuing. Always check
 *             // isStopping() to decide whether to proceed:
 *             if (jq.isStopping())
 *                 co_return;
 *             doNextChunk();
 *         }
 *         co_return;
 *     }
 *
 * Caveats / Pitfalls
 * ==================
 *
 * BUG-RISK: Using a stale or null runner.
 *   The runner shared_ptr must be valid and point to the CoroTaskRunner
 *   that owns the coroutine currently executing. Passing a runner from
 *   a different coroutine, or a default-constructed shared_ptr, is UB.
 *
 * BUG-RISK: Assuming resume happens on the same thread.
 *   After co_await, the coroutine resumes on whatever worker thread
 *   picks up the job. Do not rely on thread-local state unless it is
 *   managed through LocalValue (which CoroTaskRunner automatically
 *   swaps in/out).
 *
 * BUG-RISK: Ignoring the shutdown path.
 *   When the JobQueue is stopping, post() fails and await_suspend()
 *   resumes the coroutine immediately (symmetric transfer back to h).
 *   The coroutine body continues on the same thread. If your code
 *   after co_await assumes it was re-queued and is running on a worker
 *   thread, that assumption breaks during shutdown. Always handle the
 *   "JQ is stopping" case, either by checking jq.isStopping() or by
 *   letting the coroutine fall through to co_return naturally.
 *
 * DIFFERENCE from runner->suspend() + runner->post():
 *   Both JobQueueAwaiter and yieldAndPost() combine suspend + post
 *   in one atomic operation. With the manual suspend()/post() pattern,
 *   there is a window between the two calls where an external event
 *   could race. The atomic awaiters remove that window — onSuspend()
 *   and post() happen within the same await_suspend() call while the
 *   coroutine is guaranteed to be suspended. Use yieldAndPost() unless
 *   you need an external party to decide *when* to call post().
 */
struct JobQueueAwaiter
{
    // The CoroTaskRunner that owns the currently executing coroutine.
    std::shared_ptr<JobQueue::CoroTaskRunner> runner;

    /**
     * Always returns false so the coroutine suspends.
     */
    bool
    await_ready() const noexcept
    {
        return false;
    }

    /**
     * Increment nSuspend (equivalent to yield()) and schedule resume
     * on the JobQueue (equivalent to post()). If the JobQueue is
     * stopping, undoes the suspend count and transfers back to the
     * coroutine so it can clean up and co_return.
     *
     * Returns a coroutine_handle<> (symmetric transfer) instead of
     * bool to work around a GCC-12 codegen bug where bool-returning
     * await_suspend leaves the coroutine in an invalid state —
     * neither properly suspended nor resumed — causing a hang.
     *
     * WARNING: GCC-12 has an additional codegen bug where using this
     * external awaiter struct at multiple co_await points in the same
     * coroutine corrupts the state machine's resume index, causing the
     * coroutine to hang on the third resumption. Prefer
     * `co_await runner->yieldAndPost()` which uses an inline awaiter
     * that GCC-12 handles correctly.
     *
     * @return noop_coroutine() to stay suspended (job posted);
     *         the caller's handle to resume immediately (JQ stopping)
     */
    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> h)
    {
        XRPL_ASSERT(runner, "xrpl::JobQueueAwaiter::await_suspend : runner is valid");
        runner->onSuspend();
        if (!runner->post())
        {
            // JobQueue is stopping. Undo the suspend count and
            // transfer back to the coroutine so it can clean up
            // and co_return.
            runner->onUndoSuspend();
            return h;
        }
        return std::noop_coroutine();
    }

    void
    await_resume() const noexcept
    {
    }
};

}  // namespace xrpl
