# How to use RPC coroutines.

## Introduction.

By default, an RPC handler runs as an uninterrupted task on the JobQueue.  This
is fine for commands that are fast to compute but might not be acceptable for
tasks that require multiple parts or are large, like a full ledger.

For this purpose, the rippled RPC handler allows *suspension with continuation*
- a request to suspend execution of the RPC response and to continue it after
some function or job has been executed.  A default continuation is supplied
which simply reschedules the job on the JobQueue, or the programmer can supply
their own.

## The classes.

Suspension with continuation uses four `std::function`s in the `ripple::RPC`
namespace:

    using Callback = std::function <void ()>;
    using Continuation = std::function <void (Callback const&)>;
    using Suspend = std::function <void (Continuation const&)>;
    using Coroutine = std::function <void (Suspend const&)>;

A `Callback` is a generic 0-argument function. A given `Callback` might or might
not block. Unless otherwise advised, do not hold locks or any resource that
would prevent any other task from making forward progress when you call a
`Callback`.

A `Continuation` is a function that is given a `Callback` and promises to call
it later.  A `Continuation` guarantees to call the `Callback` exactly once at
some point in the future, but it does not have to be immediately or even in the
current thread.

A `Suspend` is a function belonging to a `Coroutine`.  A `Suspend` runs a
`Continuation`, passing it a `Callback` that continues execution of the
`Coroutine`.

And finally, a `Coroutine` is a `std::function` which is given a
`Suspend`.  This is what the RPC handler gives to the coroutine manager,
expecting to get called back with a `Suspend` and to be able to start execution.

## The flow of control.

Given these functions, the flow of RPC control when using coroutines is
straight-forward.

1.  The instance of `ServerHandler` receives an RPC request.

2.  It creates a `Coroutine` and gives it to the coroutine manager.

3.  The coroutine manager creates a `Coroutine`, starts it up, and then calls
    the `Coroutine` with a `Suspend`.

4.  Now the RPC response starts to be calculated.

5.  When the RPC handler wants to suspend, it calls the `Suspend` function with
    a `Continuation`.

6.  Coroutine execution is suspended.

7.  The `Continuation` is called with a `Callback` that the coroutine manager
    creates.

8.  The `Continuation` may choose to execute immediately, defer execution on the
    job queue, or wait for some resource to be free.

9.  When the `Continuation` is finished, it calls the `Callback` that the
    coroutine manager gave it, perhaps a long time ago.

10. This `Callback` continues execution on the suspended `Coroutine` from where
    it left off.
