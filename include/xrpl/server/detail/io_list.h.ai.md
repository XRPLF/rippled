# `io_list.h` — Lifecycle Manager for Asynchronous I/O Objects

## Role in the System

`io_list` is a thread-safe registry that tracks and terminates a set of asynchronous I/O objects. It lives in `include/xrpl/server/detail/` and is the backbone of the XRPL server's graceful-shutdown mechanism. Without it, there would be no safe way to signal every live connection and listener to stop, wait for their I/O to drain, and then resume teardown — all without data races or dangling pointers.

`ServerImpl` (in `ServerImpl.h`) owns one `io_list` instance (`ios_`) and uses it to track every `Door` (the TCP acceptor) and every peer connection that the server spawns. `Door` itself is an `io_list::work`, and when it accepts a connection it creates child peers — also `io_list::work` objects — by calling `ios().emplace<SomePeer>(...)`. This single registry therefore spans the entire live connection tree for a listening port.

## The Two-Class Design

The file defines exactly two classes: `io_list` and its inner base `io_list::work`.

**`io_list::work`** is the required base class for anything the list tracks. It carries a single pointer back to its owning `io_list` (`ios_`), set by `emplace()` at registration time. Its destructor calls `destroy()`, which erases the work object from the parent map, decrements the outstanding-work counter `n_`, and — if this was the last item and the list is already closed — swaps out and fires the finisher callback, then wakes any threads blocked in `join()`. The finisher is invoked *outside* the lock (the swap happens inside, the call happens after) to avoid re-entrant locking.

`work` requires every subclass to implement `close()`. This is the signal for a work object to begin cancelling its pending I/O (e.g., closing a socket). In `BasePeer`, `close()` posts to the strand to call the socket's `close()`. In `Door`, it cancels the backoff timer and closes the acceptor. Neither of these calls is blocking — they initiate cancellation and return.

**`io_list`** maintains the registry as a `boost::container::flat_map<work*, std::weak_ptr<work>>`. The raw pointer is the key for O(log n) erasure in `work::destroy()`, and the `weak_ptr` value lets `close()` attempt to extend the object's lifetime for long enough to call `close()` on it without assuming the object is still alive. Using `flat_map` (a sorted vector internally) is a cache-friendly choice for what is expected to be a relatively small collection that is iterated more than mutated during normal operation.

## Atomicity of Registration

`emplace<T>(args...)` has a carefully constructed double-check pattern. It checks `closed_` once before acquiring the mutex as a fast-path bail-out, then checks again while holding the lock. If the list became closed between those two checks, the newly constructed object is swapped into a local `dead` variable and destroyed outside the lock. This ensures two invariants hold simultaneously: if `emplace` returns a non-null pointer, the object is guaranteed to be in the registry before the lock releases, so any subsequent `close()` call *will* reach it; and if `emplace` returns `nullptr`, the caller knows not to `run()` the object. The constructor is intentionally called before acquiring the lock, so slow or throwing constructors don't hold up other threads registering or closing work.

## The Close/Join Protocol

`close(Finisher&&)` is idempotent after the first call. It acquires the mutex, sets `closed_ = true`, and moves the entire map out before releasing the lock. This is key: it avoids holding the lock while iterating and calling `close()` on each work item (which may post to an executor). If the map is empty at close time, the finisher is called immediately and synchronously. Otherwise it is stored in `f_` and called by whichever `work::destroy()` decrements `n_` to zero.

`join()` simply blocks on the condition variable until both `closed_` is true and `n_` is zero. The destructor calls `close()` then `join()` in sequence, ensuring that destroying an `io_list` always waits for all tracked work to finish — a critical property for shutdown safety.

## Concurrency Contract

The comments document the constraints precisely. `emplace()` is safe to call concurrently. `close()` must not be called concurrently (there is no internal guard against a double concurrent close; the idempotency check is not atomic). `join()` is safe to call concurrently, but callers must not be running an `io_context` that the work objects dispatch onto, or deadlock results — `join()` waits for work to be destroyed, but the work's async completions need `io_context::run()` to proceed.

The `closed()` accessor deliberately has no mutex guard, with the comment that it has undefined behavior if called concurrently with `close()`. This is a performance compromise: reading a single `bool` that is only ever set once (from false to true) is safe in practice on all mainstream architectures, but the official contract is conservative.

## Why This Pattern

An alternative to `io_list` would be reference counting alone: let `shared_ptr` destruction trigger cleanup. But that gives no way to *initiate* cancellation — objects would only be destroyed once all external shared_ptr holders released them, which doesn't happen until I/O completes, which doesn't happen until the socket is closed, creating a chicken-and-egg problem. `io_list` breaks this by providing an explicit `close()` signal that propagates to all live work items, after which their natural lifetime (governed by `shared_ptr`) determines when the finisher fires.