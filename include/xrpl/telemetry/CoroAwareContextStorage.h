#pragma once

/**
 * A coroutine-aware OpenTelemetry runtime-context storage.
 *
 * OpenTelemetry's default ThreadLocalContextStorage keeps the active-context
 * stack in a plain static thread_local, so the ambient span does NOT follow a
 * JobQueue::Coro across yield/resume — a scope pushed on one worker is stranded
 * when the coroutine resumes on another. This storage instead keeps the stack
 * in an xrpl::LocalValue, which JobQueue::Coro::resume() swaps in and out with
 * the coroutine (see Coro.ipp). The active context therefore rides the
 * coroutine: scopes held across yield are safe, and per-line log-trace
 * correlation (Log.cpp reads RuntimeContext::GetCurrent()) is retained.
 *
 * Off a coroutine, LocalValue transparently provides a per-thread store, so
 * behaviour is identical to the default thread-local storage.
 *
 *     +-------------------------------------------------+
 *     |            CoroAwareContextStorage              |
 *     |     (opentelemetry RuntimeContextStorage)       |
 *     +-------------------------------------------------+
 *     | - stack_ : LocalValue<vector<Context>>          |
 *     +-------------------------------------------------+
 *     | + GetCurrent() : Context                        |
 *     | + Attach(ctx)  : unique_ptr<Token>              |
 *     | + Detach(token): bool                           |
 *     +-------------------------------------------------+
 *                        | backed by (coro-aware)
 *              +------------------------+
 *              | xrpl::LocalValue store |
 *              | (swapped by Coro)      |
 *              +------------------------+
 *
 * Install once at telemetry start via
 * opentelemetry::context::RuntimeContext::SetRuntimeContextStorage(), BEFORE
 * any span is created (SDK requirement).
 *
 * @note Thread-safety: each thread/coroutine sees its own LocalValue store, so
 * the stack is never shared across threads — no locking needed. The storage
 * object itself is stateless apart from the LocalValue handle.
 * @note Known limitation: install once, before the first span; resetting the
 * storage while spans exist is undefined behaviour (SDK). A span created on a
 * raw worker thread and later moved onto a coroutine does not retro-attach to
 * the coroutine's store — not a pattern here (spans are created inside their
 * own coro/job body).
 *
 * Example 1 — install at telemetry start (primary use):
 * @code
 *   using opentelemetry::context::RuntimeContext;
 *   RuntimeContext::SetRuntimeContextStorage(
 *       opentelemetry::nostd::shared_ptr<
 *           opentelemetry::context::RuntimeContextStorage>(
 *           new xrpl::telemetry::CoroAwareContextStorage()));
 * @endcode
 *
 * Example 2 — a scope held across a coroutine yield stays correct:
 * @code
 *   // Inside a JobQueue::Coro body:
 *   auto span = ScopedSpanGuard(TraceCategory::Rpc, "rpc", "process");
 *   context.coro->yield();   // may resume on another worker
 *   // span is still the ambient context here — the storage rode the coro.
 * @endcode
 *
 * Example 3 — edge case: off a coroutine, behaves like thread-local storage:
 * @code
 *   // On a plain worker thread (no coro): each thread has its own stack,
 *   // identical to opentelemetry's default ThreadLocalContextStorage.
 *   auto span = ScopedSpanGuard(TraceCategory::Ledger, "ledger", "build");
 * @endcode
 */

#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpl/basics/LocalValue.h>

#include <opentelemetry/context/context.h>
#include <opentelemetry/context/runtime_context.h>
#include <opentelemetry/nostd/unique_ptr.h>

#include <vector>

namespace xrpl::telemetry {

class CoroAwareContextStorage : public opentelemetry::context::RuntimeContextStorage
{
public:
    CoroAwareContextStorage() = default;

    // Non-copyable and non-movable: the LocalValue store is keyed by the
    // address of stack_, so copying or moving would mis-key the store and
    // strand its entries. Only ever new-ed once and installed on the SDK, so
    // these are for intent/safety rather than a live bug.
    CoroAwareContextStorage(CoroAwareContextStorage const&) = delete;
    CoroAwareContextStorage&
    operator=(CoroAwareContextStorage const&) = delete;
    CoroAwareContextStorage(CoroAwareContextStorage&&) = delete;
    CoroAwareContextStorage&
    operator=(CoroAwareContextStorage&&) = delete;

    /**
     * @return the current (top-of-stack) context for this coro/thread.
     */
    opentelemetry::context::Context
    GetCurrent() noexcept override;

    /**
     * Push a context frame onto this coro/thread's stack.
     * @param context the context to make current.
     * @return a token that Detach() uses to pop back to the prior frame.
     */
    opentelemetry::nostd::unique_ptr<opentelemetry::context::Token>
    Attach(opentelemetry::context::Context const& context) noexcept override;

    /**
     * Pop the stack back through the frame the token refers to.
     * @param token a token returned by Attach().
     * @return true if the frame was found and detached.
     */
    bool
    Detach(opentelemetry::context::Token& token) noexcept override;

private:
    /**
     * The active-context stack, stored coro-locally. LocalValue hands back the
     * stack for whichever store (coro or thread) is currently installed.
     */
    LocalValue<std::vector<opentelemetry::context::Context>> stack_;
};

}  // namespace xrpl::telemetry

#endif  // XRPL_ENABLE_TELEMETRY
