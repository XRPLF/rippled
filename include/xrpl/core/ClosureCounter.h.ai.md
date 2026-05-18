# `ClosureCounter.h` — Async Callback Lifecycle Management for Safe Shutdown

## Role in the System

`ClosureCounter` solves a specific and tricky shutdown problem: when a component is torn down, it may have outstanding asynchronous callbacks (timers, I/O handlers) that haven't fired yet and hold references to objects that are about to be destroyed. The component needs a way to either wait for those callbacks to complete or, in the join phase, signal that new callbacks should not be registered while draining the remaining ones.

The pattern is most visible in `NetworkOPs`, which uses a `ClosureCounter<void, boost::system::error_code const&>` for its Boost.Asio timer handlers. When `NetworkOPs` shuts down it calls `waitHandlerCounter_.join(...)`, which blocks until every timer callback copy has been destroyed. New timer registrations that check `waitHandlerCounter_.wrap(...)` and receive `std::nullopt` know to cancel themselves immediately.

## Two-Phase Lifecycle

The design is intentionally linear and irreversible. During the **fork phase**, callers register callbacks by passing them to `wrap()`, receiving a `std::optional<Substitute<Closure>>` in return. So long as `join()` has not been called, the optional will contain a live `Substitute` and the internal counter will have been incremented. Once a `Substitute` is destroyed — even if it was copied multiple times — it decrements the counter once per live copy.

Calling `join()` initiates the **join phase**. From that point on, every `wrap()` call returns `std::nullopt`, preventing new callbacks from entering the system. `join()` then blocks on a condition variable until the closure count reaches zero. Because this transition is one-way (there is no "unjoin"), `ClosureCounter` is not copyable or movable — outstanding counts tied to a specific instance would be impossible to reconcile across a move.

## The `Substitute` Inner Class

`Substitute<Closure>` is the actual counted handle returned by `wrap()`. It holds a reference to its parent `ClosureCounter` and a copy of the wrapped closure. Every constructor — copy, move, and the primary construction from `ClosureCounter::wrap()` — calls `++counter_`, and the destructor calls `--counter_`. This means every live copy of a `Substitute` contributes exactly one to the counter, which is critical because Boost.Asio sometimes copies handlers internally before dispatching them.

Assignment operators on `Substitute` are explicitly deleted. Allowing assignment would require atomically decrementing the old counter, potentially notifying if it reached zero, and incrementing the new one — a sequence that would be error-prone and is simply not needed in practice.

`Substitute::operator()` has an important subtlety noted in the source: because `Args_t` is not deduced at the call site (it is fixed by the outer class template), the parameter pack `Args_t... args` does not undergo reference collapsing. The implementation uses `std::forward<Args_t>(args)...` to forward exactly the value categories the user declared in the template parameters, preserving move semantics for rvalue arguments while correctly passing lvalue references through.

A `static_assert` inside `Substitute` verifies at compile time that the wrapped closure's signature is compatible with the declared `Ret_t` and `Args_t...`, giving a readable error rather than an obscure template instantiation failure.

## Concurrency Design

`closureCount_` is `std::atomic<int>`, which allows `++` in the constructors to proceed without taking the mutex. The decrement in `operator--`, however, is performed under `mutex_` despite the atomicity of the variable itself. The comment explains why: if the decrement to zero happens between the time the waiting thread's `wait_for` predicate evaluates to `true` and when the thread re-acquires the lock (the classic spurious-wakeup window), the notified thread could observe `closureCount_ == 0` before it checks the predicate. By making the decrement and the `notify_all()` both occur under `mutex_`, the code ensures that any thread already inside `wait_for` will see the final zero once it re-evaluates the predicate under the lock.

`join()` itself uses a two-stage wait. It first calls `wait_for` with a configurable timeout, and if that times out it logs a warning via `beast::Journal` and then falls back to an unconditional `wait`. This avoids hiding indefinite hangs silently while still eventually completing shutdown, which is important for distinguishing "slow but legitimate" shutdown from a genuine deadlock.

The destructor calls `join("ClosureCounter", 1s, debugLog())` as a safety net, ensuring that any `ClosureCounter` that goes out of scope without an explicit `join()` still waits for outstanding closures to drain — preventing use-after-free on whatever resources the closures may reference.