# `StatsDCollector.h` — StatsD Metrics Export Interface

This header is the public face of XRPL's live metrics subsystem. It declares `StatsDCollector`, the concrete `Collector` implementation that ships runtime metrics to an external StatsD aggregator over UDP. The header is deliberately minimal — a class declaration and a single factory function — with all real machinery hidden in `StatsDCollector.cpp`.

## Role in the Insight Subsystem

The `beast::insight` module provides a metrics abstraction that any XRPL subsystem can consume by accepting a `Collector::ptr`. Code that needs telemetry calls `make_counter()`, `make_gauge()`, etc., on this interface without knowing whether metrics go to a StatsD server or nowhere. `NullCollector` is the no-op alternative used when metrics are disabled; `StatsDCollector` is the live backend. This separation means instrumentation code has zero cost when metrics are disabled, and no StatsD-specific code leaks into application logic.

## Factory Design

`StatsDCollector` exposes only a static `New()` factory rather than a public constructor. This is the standard pimpl-plus-factory pattern: the real work is done by `detail::StatsDCollectorImp`, a class private to the `.cpp` that inherits `StatsDCollector` and `std::enable_shared_from_this`. Callers receive a `shared_ptr<StatsDCollector>`, so the implementation type is fully encapsulated. The explicit `= default` constructor at line 17 ensures no inadvertent construction paths exist from outside the factory.

`New()` takes three parameters: an `IP::Endpoint` identifying the StatsD server (address + port), a `prefix` string prepended to every metric name (e.g., `"rippled.mainnet"`), and a `Journal` for diagnostic logging.

## Implementation Architecture (from `StatsDCollector.cpp`)

The concrete `StatsDCollectorImp` owns a private `boost::asio::io_context` and launches a dedicated `std::thread` that calls `m_io_context.run()`. All metric I/O is serialized through a `boost::asio::strand`, so every mutation, timer callback, and UDP send runs on that single background thread. This design trades fine-grained per-metric locking for a simpler, strand-serialized event loop.

### Metric Registration

Each metric type (`StatsDCounterImpl`, `StatsDGaugeImpl`, etc.) inherits `StatsDMetricBase`, which is a `beast::List<>::Node` — an intrusive linked-list node. Upon construction, each metric calls `m_impl->add(*this)`, inserting itself into the collector's `metrics_` list under `metricsLock_` (a `std::recursive_mutex`). Destruction calls `remove()`. The recursive mutex accommodates the case where a `Hook` callback creates or destroys additional metrics during the timer sweep.

### Periodic Flush Cycle

A `boost::asio::basic_waitable_timer` fires every one second. `on_timer()` locks `metricsLock_`, iterates every registered `StatsDMetricBase` calling `do_process()`, then calls `send_buffers()` and re-arms the timer. The `do_process()` implementations check a `m_dirty` flag and, if set, format the metric as a StatsD line (e.g., `"prefix.name:42|c\n"` for counters, `"|g"` for gauges, `"|m"` for meters) and enqueue it into `m_data`.

**Counters and meters** accumulate increments between flushes and reset to zero after each send — appropriate for rate metrics. **Gauges** track the last sent value and suppress redundant emissions when the value hasn't changed. **Events** (`|ms` timer type) bypass the periodic cycle entirely; `notify()` dispatches immediately to `do_notify()`, which formats and posts the buffer on the spot.

### Thread-Safe Metric Mutation

Public methods like `Counter::increment()` do not touch shared state directly. Instead they call `boost::asio::dispatch()` targeting the collector's `io_context`, binding a `do_increment()` call with a `shared_ptr` to the metric itself as the lifetime anchor. This ensures that even if the metric object is destroyed on another thread, the dispatched work holds a reference and executes safely.

### UDP Batching

`send_buffers()` packs accumulated StatsD lines into UDP datagrams up to `max_packet_size = 1472` bytes (Ethernet MTU minus IP/UDP headers — the previous comment shows 484 was the original value, updated to the practical maximum for LAN deployments). It moves `m_data` into a `shared_ptr<deque<string>>` captured by the `async_send` completion handler (`keepAlive`), ensuring string memory survives the async operation. Each send is fire-and-forget, logging errors through the `Journal` but not retrying.

### Lifecycle and Shutdown

The destructor cancels the timer, resets the `executor_work_guard` (allowing `io_context::run()` to drain), then joins the I/O thread. The final `m_io_context.poll()` after socket shutdown processes any remaining completions. Errors from `timer.cancel()` are explicitly swallowed since cancellation can race with an already-fired callback.

A compile-time flag `BEAST_STATSDCOLLECTOR_TRACING_ENABLED` (default 0) enables stderr dumps of every outgoing UDP buffer — useful for debugging metric wire format without needing a live StatsD sink.