#pragma once

#include <xrpl/beast/utility/instrumentation.h>

#include <coroutine>
#include <exception>
#include <type_traits>
#include <utility>
#include <variant>

namespace xrpl {

template <typename T = void>
class CoroTask;

/**
 * CoroTask<void> -- coroutine return type for void-returning coroutines.
 *
 * Class / Dependency Diagram
 * ==========================
 *
 *   CoroTask<void>
 *   +-----------------------------------------------+
 *   | - handle_ : Handle (coroutine_handle<promise>) |
 *   +-----------------------------------------------+
 *   | + handle(), done()                             |
 *   | + await_ready/suspend/resume  (Awaiter iface)  |
 *   +-----------------------------------------------+
 *            |  owns
 *            v
 *   promise_type
 *   +-----------------------------------------------+
 *   | - exception_    : std::exception_ptr           |
 *   | - continuation_ : std::coroutine_handle<>      |
 *   +-----------------------------------------------+
 *   | + get_return_object() -> CoroTask              |
 *   | + initial_suspend()   -> suspend_always (lazy) |
 *   | + final_suspend()     -> FinalAwaiter          |
 *   | + return_void()                                |
 *   | + unhandled_exception()                        |
 *   +-----------------------------------------------+
 *            |  returns at final_suspend
 *            v
 *   FinalAwaiter
 *   +-----------------------------------------------+
 *   | await_suspend(h):                              |
 *   |   if continuation_ set -> symmetric transfer   |
 *   |   else                 -> noop_coroutine        |
 *   +-----------------------------------------------+
 *
 * Design Notes
 * ------------
 * - Lazy start: initial_suspend returns suspend_always, so the coroutine
 *   body does not execute until the handle is explicitly resumed.
 * - Symmetric transfer: await_suspend returns a coroutine_handle instead
 *   of void/bool, allowing the scheduler to jump directly to the next
 *   coroutine without growing the call stack.
 * - Continuation chaining: when one CoroTask is co_await-ed inside
 *   another, the caller's handle is stored as continuation_ so
 *   FinalAwaiter can resume it when this task finishes.
 * - Move-only: the handle is exclusively owned; copy is deleted.
 *
 * Usage Examples
 * ==============
 *
 * 1. Basic void coroutine (the most common case in rippled):
 *
 *      CoroTask<void> doWork(std::shared_ptr<CoroTaskRunner> runner) {
 *          // do something
 *          co_await runner->suspend();   // yield control
 *          // resumed later via runner->post() or runner->resume()
 *          co_return;
 *      }
 *
 * 2. co_await-ing one CoroTask<void> from another (chaining):
 *
 *      CoroTask<void> inner() {
 *          // ...
 *          co_return;
 *      }
 *      CoroTask<void> outer() {
 *          co_await inner();   // continuation_ links outer -> inner
 *          co_return;          // FinalAwaiter resumes outer
 *      }
 *
 * 3. Exceptions propagate through co_await:
 *
 *      CoroTask<void> failing() {
 *          throw std::runtime_error("oops");
 *          co_return;
 *      }
 *      CoroTask<void> caller() {
 *          try { co_await failing(); }
 *          catch (std::runtime_error const&) { // caught here }
 *      }
 *
 * Caveats / Pitfalls
 * ==================
 *
 * BUG-RISK: Dangling references in coroutine parameters.
 *   Coroutine parameters are copied into the frame, but references
 *   are NOT -- they are stored as-is. If the referent goes out of scope
 *   before the coroutine finishes, you get use-after-free.
 *
 *      // BROKEN -- local dies before coroutine runs:
 *      CoroTask<void> bad(int& ref) { co_return; }
 *      void launch() {
 *          int local = 42;
 *          auto task = bad(local);  // frame stores &local
 *      }  // local destroyed; frame holds dangling ref
 *
 *      // FIX -- pass by value, or ensure lifetime via shared_ptr.
 *
 * BUG-RISK: GCC 14 corrupts reference captures in coroutine lambdas.
 *   When a lambda that returns CoroTask captures by reference ([&]),
 *   GCC 14 may generate a corrupted coroutine frame. Always capture
 *   by explicit pointer-to-value instead:
 *
 *      // BROKEN on GCC 14:
 *      jq.postCoroTask(t, n, [&](auto) -> CoroTask<void> { ... });
 *
 *      // FIX -- capture pointers explicitly:
 *      jq.postCoroTask(t, n, [ptr = &val](auto) -> CoroTask<void> { ... });
 *
 * BUG-RISK: Resuming a destroyed or completed CoroTask.
 *   Calling handle().resume() after the coroutine has already run to
 *   completion (done() == true) is undefined behavior. The CoroTaskRunner
 *   guards against this with an XRPL_ASSERT, but standalone usage of
 *   CoroTask must check done() before resuming.
 *
 * BUG-RISK: Moving a CoroTask that is being awaited.
 *   If task A is co_await-ed by task B (so A.continuation_ == B), moving
 *   or destroying A will invalidate the continuation link. Never move
 *   or reassign a CoroTask while it is mid-execution or being awaited.
 *
 * LIMITATION: CoroTask is fire-and-forget for the top-level owner.
 *   There is no built-in notification when the coroutine finishes.
 *   The caller must use external synchronization (e.g. CoroTaskRunner::join
 *   or a gate/condition_variable) to know when it is done.
 *
 * LIMITATION: No cancellation token.
 *   There is no way to cancel a suspended CoroTask from outside. The
 *   coroutine body must cooperatively check a flag (e.g. jq_.isStopping())
 *   after each co_await and co_return early if needed.
 *
 * LIMITATION: Stackless -- cannot suspend from nested non-coroutine calls.
 *   If a coroutine calls a regular function that wants to "yield", it
 *   cannot. Only the immediate coroutine body can use co_await.
 *   This is acceptable for rippled because all yield() sites are shallow.
 */
template <>
class CoroTask<void>
{
public:
    struct promise_type;
    using Handle = std::coroutine_handle<promise_type>;

    /**
     * Coroutine promise. Compiler uses this to manage coroutine state.
     * Stores the exception (if any) and the continuation handle for
     * symmetric transfer back to the awaiting coroutine.
     */
    struct promise_type
    {
        // Captured exception from the coroutine body, rethrown in
        // await_resume() when this task is co_await-ed by a caller.
        std::exception_ptr exception_;

        // Handle to the coroutine that is co_await-ing this task.
        // Set by await_suspend(). FinalAwaiter uses it for symmetric
        // transfer back to the caller. Null if this is a top-level task.
        std::coroutine_handle<> continuation_;

        /**
         * Create the CoroTask return object.
         * Called by the compiler at coroutine creation.
         */
        CoroTask
        get_return_object()
        {
            return CoroTask{Handle::from_promise(*this)};
        }

        /**
         * Lazy start. The coroutine body does not execute until the
         * handle is explicitly resumed (e.g. by CoroTaskRunner::resume).
         */
        std::suspend_always
        initial_suspend() noexcept
        {
            return {};
        }

        /**
         * Awaiter returned by final_suspend(). Uses symmetric transfer:
         * if a continuation exists, transfers control directly to it
         * (tail-call, no stack growth). Otherwise returns noop_coroutine
         * so the coroutine frame stays alive for the owner to destroy.
         */
        struct FinalAwaiter
        {
            /**
             * Always false. We need await_suspend to run for
             * symmetric transfer.
             */
            bool
            await_ready() noexcept
            {
                return false;
            }

            /**
             * Symmetric transfer: returns the continuation handle so
             * the compiler emits a tail-call instead of a nested resume.
             * If no continuation is set, returns noop_coroutine to
             * suspend at final_suspend without destroying the frame.
             *
             * @param h Handle to this completing coroutine
             *
             * @return Continuation handle, or noop_coroutine
             */
            std::coroutine_handle<>
            await_suspend(Handle h) noexcept
            {
                if (auto cont = h.promise().continuation_)
                    return cont;
                return std::noop_coroutine();
            }

            void
            await_resume() noexcept
            {
            }
        };

        /**
         * Returns FinalAwaiter for symmetric transfer at coroutine end.
         */
        FinalAwaiter
        final_suspend() noexcept
        {
            return {};
        }

        /**
         * Called by the compiler for `co_return;` (void coroutine).
         */
        void
        return_void()
        {
        }

        /**
         * Called by the compiler when an exception escapes the coroutine
         * body. Captures it for later rethrowing in await_resume().
         */
        void
        unhandled_exception()
        {
            exception_ = std::current_exception();
        }
    };

    /**
     * Default constructor. Creates an empty (null handle) task.
     */
    CoroTask() = default;

    /**
     * Takes ownership of a compiler-generated coroutine handle.
     *
     * @param h Coroutine handle to own
     */
    explicit CoroTask(Handle h) : handle_(h)
    {
    }

    /**
     * Destroys the coroutine frame if this task owns one.
     */
    ~CoroTask()
    {
        if (handle_)
            handle_.destroy();
    }

    /**
     * Move constructor. Transfers handle ownership, leaves other empty.
     */
    CoroTask(CoroTask&& other) noexcept : handle_(std::exchange(other.handle_, {}))
    {
    }

    /**
     * Move assignment. Destroys current frame (if any), takes other's.
     */
    CoroTask&
    operator=(CoroTask&& other) noexcept
    {
        if (this != &other)
        {
            if (handle_)
                handle_.destroy();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    CoroTask(CoroTask const&) = delete;
    CoroTask&
    operator=(CoroTask const&) = delete;

    /**
     * @return The underlying coroutine_handle
     */
    Handle
    handle() const
    {
        return handle_;
    }

    /**
     * @return true if the coroutine has run to completion (or thrown)
     */
    bool
    done() const
    {
        return handle_ && handle_.done();
    }

    // -- Awaiter interface: allows `co_await someCoroTask;` --

    /**
     * Always false. This task is lazy, so co_await always suspends
     * the caller to set up the continuation link.
     */
    bool
    await_ready() const noexcept
    {
        return false;
    }

    /**
     * Stores the caller's handle as our continuation, then returns
     * our handle for symmetric transfer (caller suspends, we resume).
     *
     * @param caller Handle of the coroutine doing co_await on us
     *
     * @return Our handle for symmetric transfer
     */
    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> caller) noexcept
    {
        XRPL_ASSERT(handle_, "xrpl::CoroTask<void>::await_suspend : handle is valid");
        handle_.promise().continuation_ = caller;
        return handle_;  // Symmetric transfer
    }

    /**
     * Called in the awaiting coroutine's context after this task
     * completes. Rethrows any exception captured by
     * unhandled_exception().
     */
    void
    await_resume()
    {
        XRPL_ASSERT(handle_, "xrpl::CoroTask<void>::await_resume : handle is valid");
        if (auto& ep = handle_.promise().exception_)
            std::rethrow_exception(ep);
    }

private:
    // Exclusively-owned coroutine handle. Null after move or default
    // construction. Destroyed in the destructor.
    Handle handle_;
};

/**
 * CoroTask<T> -- coroutine return type for value-returning coroutines.
 *
 * Class / Dependency Diagram
 * ==========================
 *
 *   CoroTask<T>
 *   +-----------------------------------------------+
 *   | - handle_ : Handle (coroutine_handle<promise>) |
 *   +-----------------------------------------------+
 *   | + handle(), done()                             |
 *   | + await_ready/suspend/resume  (Awaiter iface)  |
 *   +-----------------------------------------------+
 *            |  owns
 *            v
 *   promise_type
 *   +-----------------------------------------------+
 *   | - result_       : variant<monostate, T,        |
 *   |                           exception_ptr>       |
 *   | - continuation_ : std::coroutine_handle<>      |
 *   +-----------------------------------------------+
 *   | + get_return_object() -> CoroTask              |
 *   | + initial_suspend()   -> suspend_always (lazy) |
 *   | + final_suspend()     -> FinalAwaiter          |
 *   | + return_value(T)     -> stores in result_[1]  |
 *   | + unhandled_exception -> stores in result_[2]  |
 *   +-----------------------------------------------+
 *            |  returns at final_suspend
 *            v
 *   FinalAwaiter  (same symmetric-transfer pattern as CoroTask<void>)
 *
 * Value Extraction
 * ----------------
 * await_resume() inspects the variant:
 *   - index 2 (exception_ptr) -> rethrow
 *   - index 1 (T)             -> return value via move
 *
 * Usage Examples
 * ==============
 *
 * 1. Simple value return:
 *
 *      CoroTask<int> computeAnswer() { co_return 42; }
 *
 *      CoroTask<void> caller() {
 *          int v = co_await computeAnswer();  // v == 42
 *      }
 *
 * 2. Chaining value-returning coroutines:
 *
 *      CoroTask<int> add(int a, int b) { co_return a + b; }
 *      CoroTask<int> doubleSum(int a, int b) {
 *          int s = co_await add(a, b);
 *          co_return s * 2;
 *      }
 *
 * 3. Exception propagation from inner to outer:
 *
 *      CoroTask<int> failing() {
 *          throw std::runtime_error("bad");
 *          co_return 0;  // never reached
 *      }
 *      CoroTask<void> caller() {
 *          try {
 *              int v = co_await failing();  // throws here
 *          } catch (std::runtime_error const& e) {
 *              // e.what() == "bad"
 *          }
 *      }
 *
 * Caveats / Pitfalls (in addition to CoroTask<void> caveats above)
 * ================================================================
 *
 * BUG-RISK: await_resume() moves the value out of the variant.
 *   Calling co_await on the same CoroTask<T> instance twice is undefined
 *   behavior -- the second call will see a moved-from T. CoroTask is
 *   single-shot: one co_return, one co_await.
 *
 * BUG-RISK: T must be move-constructible.
 *   return_value(T) takes by value and moves into the variant.
 *   Types that are not movable cannot be used as T.
 *
 * LIMITATION: No co_yield support.
 *   CoroTask<T> only supports a single co_return. It does not implement
 *   yield_value(), so using co_yield inside a CoroTask<T> coroutine is a
 *   compile error. For streaming values, a different return type
 *   (e.g. Generator<T>) would be needed.
 *
 * LIMITATION: Result is only accessible via co_await.
 *   There is no .get() or .result() method. The value can only be
 *   extracted by co_await-ing the CoroTask<T> from inside another
 *   coroutine. For extracting results in non-coroutine code, pass a
 *   pointer to the caller and write through it (as the tests do).
 */
template <typename T>
class CoroTask
{
    static_assert(
        std::is_move_constructible_v<T>,
        "CoroTask<T> requires T to be move-constructible");

public:
    struct promise_type;
    using Handle = std::coroutine_handle<promise_type>;

    /**
     * Coroutine promise for value-returning coroutines.
     * Stores the result as a variant: monostate (not yet set),
     * T (co_return value), or exception_ptr (unhandled exception).
     */
    struct promise_type
    {
        // Tri-state result:
        //   index 0 (monostate) -- coroutine has not yet completed
        //   index 1 (T)         -- co_return value stored here
        //   index 2 (exception) -- unhandled exception captured here
        std::variant<std::monostate, T, std::exception_ptr> result_;

        // Handle to the coroutine co_await-ing this task. Used by
        // FinalAwaiter for symmetric transfer. Null for top-level tasks.
        std::coroutine_handle<> continuation_;

        /**
         * Create the CoroTask return object.
         * Called by the compiler at coroutine creation.
         */
        CoroTask
        get_return_object()
        {
            return CoroTask{Handle::from_promise(*this)};
        }

        /**
         * Lazy start. Coroutine body does not run until explicitly resumed.
         */
        std::suspend_always
        initial_suspend() noexcept
        {
            return {};
        }

        /**
         * Symmetric-transfer awaiter at coroutine completion.
         * Same pattern as CoroTask<void>::FinalAwaiter.
         */
        struct FinalAwaiter
        {
            bool
            await_ready() noexcept
            {
                return false;
            }

            /**
             * Returns continuation for symmetric transfer, or
             * noop_coroutine if this is a top-level task.
             *
             * @param h Handle to this completing coroutine
             *
             * @return Continuation handle, or noop_coroutine
             */
            std::coroutine_handle<>
            await_suspend(Handle h) noexcept
            {
                if (auto cont = h.promise().continuation_)
                    return cont;
                return std::noop_coroutine();
            }

            void
            await_resume() noexcept
            {
            }
        };

        FinalAwaiter
        final_suspend() noexcept
        {
            return {};
        }

        /**
         * Called by the compiler for `co_return value;`.
         * Moves the value into result_ at index 1.
         *
         * @param value The value to store
         */
        void
        return_value(T value)
        {
            result_.template emplace<1>(std::move(value));
        }

        /**
         * Captures unhandled exceptions at index 2 of result_.
         * Rethrown later in await_resume().
         */
        void
        unhandled_exception()
        {
            result_.template emplace<2>(std::current_exception());
        }
    };

    /**
     * Default constructor. Creates an empty (null handle) task.
     */
    CoroTask() = default;

    /**
     * Takes ownership of a compiler-generated coroutine handle.
     *
     * @param h Coroutine handle to own
     */
    explicit CoroTask(Handle h) : handle_(h)
    {
    }

    /**
     * Destroys the coroutine frame if this task owns one.
     */
    ~CoroTask()
    {
        if (handle_)
            handle_.destroy();
    }

    /**
     * Move constructor. Transfers handle ownership, leaves other empty.
     */
    CoroTask(CoroTask&& other) noexcept : handle_(std::exchange(other.handle_, {}))
    {
    }

    /**
     * Move assignment. Destroys current frame (if any), takes other's.
     */
    CoroTask&
    operator=(CoroTask&& other) noexcept
    {
        if (this != &other)
        {
            if (handle_)
                handle_.destroy();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    CoroTask(CoroTask const&) = delete;
    CoroTask&
    operator=(CoroTask const&) = delete;

    /**
     * @return The underlying coroutine_handle
     */
    Handle
    handle() const
    {
        return handle_;
    }

    /**
     * @return true if the coroutine has run to completion (or thrown)
     */
    bool
    done() const
    {
        return handle_ && handle_.done();
    }

    // -- Awaiter interface: allows `T val = co_await someCoroTask;` --

    /**
     * Always false. co_await always suspends to set up continuation.
     */
    bool
    await_ready() const noexcept
    {
        return false;
    }

    /**
     * Stores caller as continuation, returns our handle for
     * symmetric transfer.
     *
     * @param caller Handle of the coroutine doing co_await on us
     *
     * @return Our handle for symmetric transfer
     */
    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> caller) noexcept
    {
        XRPL_ASSERT(handle_, "xrpl::CoroTask<T>::await_suspend : handle is valid");
        handle_.promise().continuation_ = caller;
        return handle_;
    }

    /**
     * Extracts the result: rethrows if exception, otherwise moves
     * the T value out of the variant. Single-shot: calling twice
     * on the same task is undefined (moved-from T).
     *
     * @return The co_return-ed value
     */
    T
    await_resume()
    {
        XRPL_ASSERT(handle_, "xrpl::CoroTask<T>::await_resume : handle is valid");
        auto& result = handle_.promise().result_;
        if (auto* ep = std::get_if<2>(&result))
            std::rethrow_exception(*ep);
        return std::get<1>(std::move(result));
    }

private:
    // Exclusively-owned coroutine handle. Null after move or default
    // construction. Destroyed in the destructor.
    Handle handle_;
};

}  // namespace xrpl
