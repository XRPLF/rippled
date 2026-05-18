# LoadManager.h — Server Stall Detection and Load Monitoring

`LoadManager` is a lightweight watchdog component that gives the XRPL server operator visibility into two failure conditions: a stalled job queue and unsustainable peer-driven load. It owns a dedicated background thread that ticks every second and enforces a liveness contract between itself and the rest of the application.

## Role in the System

The class sits at the intersection of `Application`, `NetworkOPs`, and `LoadFeeTrack`. It is created in `Application`'s constructor via the `make_LoadManager()` factory, started during `Application::setup()`, and stopped during shutdown. The companion `NetworkOPsImp::processHeartbeatTimer()` — which fires on every consensus heartbeat tick — calls `LoadManager::heartbeat()` to prove liveness; if the heartbeat stops arriving, it means the job queue has stalled and work is not being processed.

## Stall Detection

The core logic lives in the private `run()` method. The thread sleeps for one second at a time using `cv_.wait_until()`, which doubles as the shutdown signal: when `stop()` sets `stop_ = true` and signals the condition variable, the wait returns immediately and the loop exits.

Each tick, the thread checks how long ago the last `heartbeat()` call was recorded. The escalation ladder is:

- **0–10 s**: silent. Transient latency is not a stall.
- **≥ 10 s (and every 10-second multiple thereafter)**: `warn`-level log. If the `JobQueue` reports overload at this point, its JSON state is dumped alongside the warning.
- **≥ 90 s**: severity upgrades to `fatal`. The job queue state is always dumped at this level.
- **≥ 600 s**: `LogicError("Fatal server stall detected")` is thrown, causing a controlled crash. This represents complete failure of any self-healing mechanism.

The `armed_` flag is a deliberate interlock: the stall check does nothing until `activateStallDetector()` has been called. As the VFALCO comment in the header notes, this prevents false positives during the potentially lengthy initialization phase, where heartbeats naturally don't arrive yet. `Application` calls `activateStallDetector()` only after full setup completes (line 1493 of `Application.cpp`).

`heartbeat()` captures `steady_clock::now()` *before* acquiring `mutex_`; this is intentional. If the mutex were contended, measuring time inside the lock would make the stored timestamp reflect lock-wait latency rather than the actual moment liveness was confirmed.

## Load Fee Adjustment

There is a fee-tracking block that runs once after the `while(true)` loop exits — that is, only at shutdown. It checks whether the `JobQueue` is overloaded and calls either `FeeTrack::raiseLocalFee()` or `FeeTrack::lowerLocalFee()` accordingly; if the fee changed, it calls `app_.getOPs().reportFeeChange()`. A `// VFALCO TODO` comment acknowledges this should be driven by a listener/observer pattern rather than inline polling. In its current form, this code serves as a single final fee reconciliation at teardown rather than an ongoing regulatory loop.

## Construction and Lifecycle

The constructor is `private`. The only way to create a `LoadManager` is through `make_LoadManager()`, which returns `std::unique_ptr<LoadManager>`. This factory pattern enforces exclusive ownership and prevents stack allocation, which would be unsafe for an object that owns a thread.

The destructor calls `stop()` inside a `try`/`catch`, swallowing any exception. This is a standard defensive pattern for objects that join a thread in their destructor: if `stop()` were to throw (e.g., due to a previously unhandled error), letting it propagate from a destructor would call `std::terminate`. The warning log on catch ensures the exception is not silently lost.

`start()` asserts `!thread_.joinable()` before spawning the thread, guarding against accidental double-starts. `stop()` checks `thread_.joinable()` before calling `join()`, making it safe to call from both `~LoadManager` and an explicit external shutdown path without risk of joining an already-joined thread.

## Concurrency Model

Three shared fields — `lastHeartbeat_`, `armed_`, and `stop_` — are all protected by `mutex_`. The background thread copies `lastHeartbeat_` and `armed_` under the lock, then unlocks before doing the stall arithmetic and logging. This minimizes lock hold time: log calls and duration arithmetic happen outside the critical section, ensuring that verbose stall-log output cannot block the `heartbeat()` callers on the main processing path.