# `StatsDCollector.cpp` — Beast Insight StatsD Backend

## Role and Purpose

This file is the concrete implementation of `StatsDCollector`, one of the two `Collector` backends in the Beast insight framework (the other being `NullCollector`). Its job is to accept runtime metrics emitted by XRPL subsystems — counters, gauges, meters, events, and hooks — and relay them over UDP to an external StatsD-compatible aggregator such as Graphite or Telegraf. The file is entirely self-contained: it exposes only the factory function `StatsDCollector::New()` through the public header while burying all implementation classes inside the anonymous `detail` namespace.

## Architecture Overview

The collector lives behind the pimpl idiom. `StatsDCollector::New()` constructs a `StatsDCollectorImp`, which is hidden from callers and returned as a `shared_ptr<StatsDCollector>`. `StatsDCollectorImp` owns a private `boost::asio::io_context`, a UDP socket, and a dedicated background thread that runs the event loop. All metric state mutations and all network I/O happen exclusively on this thread, which is the core concurrency strategy of the entire implementation.

### Thread Model and Synchronization

When application code on any thread calls `counter.increment(n)`, the call enters `StatsDCounterImpl::increment()`, which posts a bound call to `do_increment()` via `boost::asio::dispatch` onto the collector's `io_context`. This means the counter's internal `m_value` is only ever touched on the I/O thread, requiring no per-metric mutex. The same pattern applies to gauges, meters, and events.

The only shared state that crosses threads is the `List<StatsDMetricBase> metrics_` registry, guarded by `metricsLock_` (a `std::recursive_mutex`). Metrics register themselves in their constructor via `m_impl->add(*this)` and deregister in their destructor via `m_impl->remove(*this)`. The registry is also iterated under the same lock inside `on_timer`, making registration and destruction safe from any thread.

### The Polling Loop

A 1-second repeating timer drives metric collection. `on_timer()` acquires `metricsLock_`, iterates every registered `StatsDMetricBase`, and calls `do_process()` on each. For counters, gauges, and meters, `do_process()` delegates to a `flush()` method that checks a `m_dirty` flag. If the metric changed since the last tick, `flush()` serializes the metric name, value, and StatsD type suffix (`|c`, `|g`, `|m`) into a string and calls `post_buffer()`. After all metrics are processed, `send_buffers()` ships the accumulated strings over UDP.

Events (`StatsDEventImpl`) are handled differently: they do not join the registry list and do not wait for the timer. Calling `notify()` immediately dispatches `do_notify()` to the I/O thread, which formats and posts the buffer at once using `|ms` (millisecond timing) format. This is intentional — events represent discrete occurrences whose latency should be captured at notification time, not coalesced.

### Intrusive List for the Metric Registry

`StatsDMetricBase` inherits from `List<StatsDMetricBase>::Node`, making it a node in Beast's intrusive doubly linked list. The intrusive design avoids separate heap allocations for list bookkeeping and, crucially, enables O(1) removal from the registry using `metrics_.iterator_to(metric)` in `remove()`. Because each metric object *is* its own node, no separate tracking data structure is needed.

### UDP Buffer Packing

`send_buffers()` implements a simple greedy packing strategy. It accumulates formatted metric strings into a `std::vector<boost::asio::const_buffer>` until the total byte count would exceed 1472 bytes (chosen as a typical Ethernet MTU after IP and UDP headers), at which point it fires an `async_send` and starts a fresh batch. The source code comments mention an earlier limit of 484 bytes that was later raised. This approach maximizes throughput while keeping each datagram below fragmentation thresholds.

The `keepAlive` pattern in `send_buffers()` and `on_send()` deserves attention. The deque of string data is moved into a `shared_ptr<deque<string>>` before launching `async_send`. This shared pointer is then passed as the first (ignored) argument to the `on_send` completion handler. Because Boost.Asio holds a copy of the completion handler until it fires, the underlying string data stays alive for the entire asynchronous operation, even though `m_data` has already been cleared and ownership transferred.

### Gauge Arithmetic

`StatsDGaugeImpl::do_increment()` applies saturating arithmetic rather than allowing unsigned overflow. Positive increments are capped at `numeric_limits<uint64_t>::max() - m_value`, and negative decrements are floored at zero. Gauges additionally suppress redundant sends: `do_set()` compares the incoming value against `m_last_value` and only sets `m_dirty` if the value changed. This avoids flooding StatsD with unchanging gauge readings every second.

### Lifecycle and Shutdown

`StatsDCollectorImp` uses `std::enable_shared_from_this` so that each metric implementation can safely capture a `shared_ptr` to the collector. This ensures the collector outlives any outstanding metric objects. Shutdown is ordered carefully: the destructor cancels the timer, resets the `executor_work_guard` (`m_work`) to let the event loop drain, then joins the background thread. After `io_context::run()` returns, `m_socket.shutdown()` and `m_socket.close()` are called, followed by a final `io_context::poll()` to flush any trailing completion handlers.

The `m_thread` member is declared last in the class body specifically to ensure it is initialized after all other members, since the thread immediately invokes `run()` which touches `m_socket`, `m_io_context`, and the timer.

### Compile-Time Tracing

The `BEAST_STATSDCOLLECTOR_TRACING_ENABLED` macro (off by default) gates a `log()` function that writes raw UDP payload contents to `std::cerr` before each send. This is useful during development but is entirely compiled out in production builds.