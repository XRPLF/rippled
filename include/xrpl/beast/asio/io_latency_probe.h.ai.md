# `io_latency_probe.h` — IO Context Latency Measurement

## Role in the System

`io_latency_probe` exists to answer a critical operational question: how backed-up is the ASIO `io_context` dispatch queue? In the XRPL node, nearly all networked and timer-driven work flows through a single `io_context`. If that context becomes saturated — because handlers are slow, threads are starved, or work is piling up — handlers experience real but invisible delay between being posted and actually running. This class makes that invisible delay visible by injecting sentinel handlers whose sole purpose is to be timed.

The class lives in `include/xrpl/beast/asio/io_latency_probe.h` inside the `beast` namespace and is instantiated in `Application.cpp` as `io_latency_sampler`, sampling at 100ms intervals and emitting a metrics event for any latency above 10ms, plus a journal warning above 500ms.

## Measurement Mechanism

The core technique is straightforward: `sample_one()` records `Clock::now()` then calls `boost::asio::post()` to queue a `sample_op` handler. When the `io_context` eventually dispatches that handler, it captures `Clock::now()` again and computes `elapsed = now - start`. The difference is not the handler's own execution time — it is purely the queue depth, expressed as time. A healthy, idle context shows near-zero latency; a loaded one can show hundreds of milliseconds.

`sample()` works identically for the first measurement but then reschedules itself using `m_timer` to continue sampling at the configured `m_period`.

## The Timer Compensation Formula

The repeated-sampling path contains a subtle but important design choice. After measuring `elapsed` latency, the code computes the next wake time as:

```cpp
typename Clock::time_point const when(now + m_probe->m_period - 2 * elapsed);
```

The factor of `2 * elapsed` is intentional. After observing latency `elapsed`, the timer is set to expire in `period - elapsed`. But the timer's async-wait completion handler itself must pass through the same `io_context` queue before executing, incurring another `elapsed` of delay. By subtracting the full `2 * elapsed`, the two delays cancel out, keeping the inter-sample interval close to `m_period` even under moderate load. When latency is so severe that `when <= now`, the code bypasses the timer entirely and calls `boost::asio::post()` directly — there is no point asking a timer to wait a negative duration.

The timer completion path (the `error_code` overload of `operator()`) does not call the user handler at all — it just posts a fresh `sample_op` using `now` as the new start time. The handler is only invoked from the no-argument overload, which runs the real elapsed-time measurement.

## Reference Counting and Safe Teardown

The class uses an intrusive reference count (`m_count`) to guarantee the destructor blocks until all in-flight `sample_op` objects have finished. The count starts at 1 representing the probe itself. Every `sample_op` constructor calls `addref()` and every destructor calls `release()`. When `release()` drops the count to zero it notifies `m_cond`, waking the destructor's `m_cond.wait()`.

Cancellation consumes that initial "1": calling `cancel()` sets `m_cancel = true` and does `--m_count`. With `wait = true` (the synchronous path used by the destructor), it then waits on `m_cond` until all outstanding ops drain. `cancel_async()` sets the flag and returns immediately, useful when calling from within the `io_context` thread itself to avoid a deadlock.

Once `m_cancel` is true, `sample_one()` and `sample()` throw `std::logic_error` rather than silently accept new work. `sample_op::operator()()` checks `m_cancel` before scheduling the next repetition, so a running probe stops cleanly after its current dispatch.

## Recursive Mutex Rationale

`m_mutex` is a `std::recursive_mutex` rather than a plain `std::mutex`. Inside `sample()` and `sample_one()`, the public lock guard is held when a temporary `sample_op` is constructed — which immediately calls `addref()`, which tries to acquire the same mutex. Without reentrancy the constructor would deadlock against the caller. The temporary `sample_op` created inline as an argument to `post()` also gets destroyed (after being moved into the queue), triggering `release()` under the same lock. The recursive mutex handles both.

## Production Usage

In `ApplicationImp::io_latency_sampler`, the probe is wrapped with a `beast::insight::Event` to push latency readings into the application's collector metrics system. Any sample ≥ 10ms fires a stats event; any sample ≥ 500ms writes a journal warning. The `getIOLatency()` method on the `Application` interface returns the most recent sample atomically via `std::atomic<std::chrono::milliseconds>`, allowing other subsystems to inspect current io_context health without blocking.