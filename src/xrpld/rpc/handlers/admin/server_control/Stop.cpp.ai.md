# `Stop.cpp` — RPC Handler for Graceful Server Shutdown

This file implements `doStop`, the handler for the `stop` RPC command. Its entire body is two lines: signal the application to stop, then return a confirmation message. The simplicity is deliberate — every meaningful concern is separated into the surrounding infrastructure.

## Role in the Dispatch System

The handler is registered in `src/xrpld/rpc/detail/Handler.cpp` as:

```cpp
{"stop", byRef(&doStop), Role::ADMIN, NO_CONDITION},
```

Two properties of this registration are significant. First, `Role::ADMIN` means the RPC dispatch layer rejects any caller that does not hold admin credentials before `doStop` is ever reached — there is no permission check inside the function itself because the framework enforces it unconditionally at a higher level. Second, `NO_CONDITION` means the handler does not require the server to be synced to the network or hold a current ledger; a node that is disconnected or still starting up can still be stopped via RPC, which is exactly the right behavior for an administrative shutdown command.

## What `signalStop("RPC")` Actually Does

`doStop` delegates entirely to `Application::signalStop()`, whose implementation in `ApplicationImp` is:

```cpp
void ApplicationImp::signalStop(std::string msg)
{
    if (!isTimeToStop.test_and_set(std::memory_order_acquire))
    {
        JLOG(m_journal.warn()) << "Server stopping: " << msg;
        isTimeToStop.notify_all();
    }
}
```

`isTimeToStop` is a C++20 `std::atomic_flag`. The `test_and_set` call is a lock-free atomic that returns `false` only on the first invocation, ensuring the stop message is logged exactly once regardless of concurrent callers. After setting the flag, `notify_all()` unblocks the `run()` loop, which is suspended on `isTimeToStop.wait(false, std::memory_order_relaxed)`. The string argument `"RPC"` becomes the reason recorded in the log — operators can distinguish an administrative RPC shutdown from a SIGTERM (`"Signal: 15"`), a DB space exhaustion (`"Out of transaction DB space"`), or a PerfLog timeout.

## Response-Before-Shutdown Ordering

`doStop` returns the confirmation message immediately after calling `signalStop()`, before the server actually begins teardown. This ordering is intentional: the `run()` loop wakes up and starts unwinding only after the current RPC dispatch returns and the response is queued for delivery. If the handler waited for teardown before returning, the response would never reach the caller because the HTTP/WebSocket layer would already be shutting down. The client therefore receives `"rippled server stopping"` reliably, while the actual shutdown proceeds asynchronously.

## Relationship to Other Files

`doStop` is declared in `src/xrpld/rpc/handlers/Handlers.h` alongside every other RPC handler function. The `systemName()` call used to compose the response message returns the server's compiled-in product name (e.g., `"rippled"`), making the response self-identifying if the binary is rebranded. The `RPC::makeObjectValue` helper from `Handler.h` wraps the string into a `{"message": "..."}` JSON object, consistent with the response envelope used across all scalar-valued handlers.

The file sits in `handlers/admin/server_control/`, a directory that currently also holds `LedgerAccept.cpp` — the two together represent the only commands that make irreversible changes to the running server's state rather than reading it.