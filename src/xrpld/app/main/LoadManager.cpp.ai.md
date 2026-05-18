# `LoadManager.cpp` — Server Stall Detection and Load Monitoring

`LoadManager` is a small but safety-critical subsystem that runs a single dedicated background thread responsible for two jobs: detecting when the XRPL server's main processing loop has stalled, and adjusting the node's local transaction fee when the job queue is overloaded. The file is the complete implementation of the class declared in `LoadManager.h`.

## Why It Exists

The XRPL server processes everything — consensus rounds, transaction validation, ledger closes — through a timed event loop in `NetworkOPs`. If that loop stalls (deadlock, starvation, an I/O thread hung under the master mutex), the server appears alive but is silently frozen. Without an independent watchdog thread, such a stall would be invisible: no logs, no automatic recovery. `LoadManager` provides that watchdog, operating outside the main event loop so it can report stalls even when the event loop itself is blocked.

## Thread Lifecycle

`start()` spawns `std::thread{&LoadManager::run, this}` and enforces via `XRPL_ASSERT(!thread_.joinable())` that only one thread is ever running. `stop()` sets `stop_ = true` under the mutex and calls `cv_.notify_all()`, waking the sleeping thread so it can exit cleanly. `thread_.join()` then blocks until the thread finishes.

The destructor calls `stop()` inside a `try/catch` that swallows exceptions, which is the standard defensive pattern for destructors that must not propagate — a stall during shutdown should not also crash the process with an uncaught exception.

## Stall Detection: The Heartbeat Pattern

The stall detector relies on a two-party contract. Every ~1 second, `NetworkOPs::processHeartbeatTimer()` — the main ledger/consensus timer — calls `LoadManager::heartbeat()` under the master mutex, which updates `lastHeartbeat_` to the current monotonic time. Concurrently, the `run()` loop wakes each second, snapshots `lastHeartbeat_` under the lock, unlocks, and then computes `timeSpentStalled = now - lastHeartbeat_`.

If `heartbeat()` is being called regularly, `timeSpentStalled` stays near zero. If the event loop is blocked, `heartbeat()` stops being called, and `timeSpentStalled` grows. The threshold cascade is:

- **10 seconds**: First warning log. If the job queue is also overloaded, its full JSON state is dumped.
- **90 seconds**: Log level escalates from `warn` to `fatal` — same heartbeat threshold, but the message category changes.
- **600 seconds**: `LogicError("Fatal server stall detected")` is thrown. At this point the stall-resolution mechanisms have clearly failed and the process is expected to abort.

The reporting fires every `reportingIntervalSeconds` (10s) via the expression `(timeSpentStalled % reportingIntervalSeconds) == 0s`, which suppresses duplicate log lines within each 10-second window. This avoids flooding the log with one message per second once a stall is detected.

## The "Armed" State

The stall detector starts disarmed (`armed_ = false`). `activateStallDetector()` must be called explicitly — in practice, `Application::run()` calls it after all initialization is complete, just before entering the main wait on `isTimeToStop`. A VFALCO note in `Application.cpp` even questions whether this arming step is necessary at all, suggesting it was introduced specifically to prevent false stall alarms during what could be a lengthy startup sequence. The armed flag prevents the watchdog from firing during cold-start initialization, when long operations like loading the ledger state from disk could otherwise trigger spurious stall reports.

## Fee Adjustment — An Unusual Placement

The `run()` function contains a fee-adjustment block that reads the `JobQueue` overload state and calls either `getFeeTrack().raiseLocalFee()` or `lowerLocalFee()`. Critically, this block sits **after** the `while (true)` loop — it executes exactly once, when the thread exits because `stop_` has been set. This means fee adjustment happens only at shutdown, which is architecturally odd.

A VFALCO TODO comment at the call site notes the intent to replace the direct `reportFeeChange()` call with a listener/observer pattern. This suggests the fee-adjustment logic is vestigial from an earlier design where `run()` also managed periodic fee updates from within the loop. The current code, as written, raises or lowers the fee once on teardown, then notifies `NetworkOPs` via `app_.getOPs().reportFeeChange()`.

## Concurrency Design

The class uses a single `std::mutex` guarding `lastHeartbeat_`, `armed_`, and the condition variable `cv_`. The background thread uses `std::unique_lock` with `cv_.wait_until()` for the 1-second sleep, releasing the lock during the actual stall-time computation so `heartbeat()` on the main thread can proceed without contention. The brief copy-under-lock / compute-outside-lock pattern at lines 99–101 is a correct and minimal critical section: only the two `std::chrono::time_point` and `bool` copies are taken inside the lock, and the `duration_cast` arithmetic is done after releasing it.

The `stop_` flag itself is also protected by `mutex_` (not `std::atomic`) because it must be coordinated with the condition variable notification: `stop_ = true` and `cv_.notify_all()` must be atomic from the waiting thread's perspective, which `std::lock_guard` on `mutex_` ensures.

## Relationship to Surrounding Code

`LoadManager` is constructed by the `make_LoadManager()` factory (which uses `new` directly because the constructor is private, exposing it only to the `friend` factory) and stored as `m_loadManager` in `ApplicationImpl`. `NetworkOPs` holds a reference to the `LoadManager` through `Application::getLoadManager()` and calls `heartbeat()` once per consensus timer tick. `LoadFeeTrack` provides the fee escalation/reduction primitives, and `JobQueue::isOverloaded()` / `getJson()` supply the diagnostic data that the monitor logs when a stall is detected.