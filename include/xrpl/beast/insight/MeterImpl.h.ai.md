# `MeterImpl.h` — Abstract Interface for Increment-Only Metric Counters

`MeterImpl` defines the abstract backend interface for the `Meter` metric type within the `beast::insight` telemetry subsystem. It occupies the same position in the insight hierarchy as `CounterImpl`, `GaugeImpl`, and `EventImpl` — it is the pure virtual contract that concrete collector backends (such as the StatsD implementation) must fulfill to participate in the metrics pipeline.

## Role in the Insight Architecture

The `beast::insight` subsystem follows a two-layer design. Each metric type has a lightweight, copyable **handle** class (`Meter`) and a separately-defined **implementation** base class (`MeterImpl`). The handle wraps a `shared_ptr<MeterImpl>` and delegates all operations through it. This separation allows the handle to be freely copied and passed around at near-zero cost while the implementation itself carries the real state — aggregated counts and I/O context references — inside the collector backend.

`MeterImpl.h` is the implementation side of this split: it declares the abstract interface that any backend must provide, while `Meter.h` includes it to alias `value_type` and to accept `shared_ptr<MeterImpl>` in its constructor.

## Semantics of a Meter vs. a Counter

The only public method is `increment(value_type amount)`, and `value_type` is `std::uint64_t` — unsigned. This is a deliberate distinction from `CounterImpl`, which uses `std::int64_t` and therefore permits both positive and negative adjustments. A `Meter` is strictly monotonically increasing: it models a cumulative rate — events fired, bytes sent, transactions processed — where the value only ever grows. Callers who need to decrement a metric must use `Counter` instead.

## `enable_shared_from_this` and Lifetime Management

`MeterImpl` inherits `std::enable_shared_from_this<MeterImpl>`. This is not incidental. The concrete StatsD backend (`StatsDMeterImpl`) must dispatch `increment()` calls asynchronously onto the collector's `boost::asio` I/O thread to avoid locking. The dispatch looks like:

```cpp
boost::asio::dispatch(
    m_impl->get_io_context(),
    std::bind(&StatsDMeterImpl::do_increment,
              std::static_pointer_cast<StatsDMeterImpl>(shared_from_this()),
              amount));
```

Without `enable_shared_from_this`, obtaining a safe `shared_ptr` to `this` from inside a member function would be impossible. The base class provides the mechanism; `StatsDMeterImpl` uses `shared_from_this()` to extend the object's lifetime across the asynchronous dispatch gap, ensuring the object isn't destroyed between when `increment()` returns and when `do_increment()` actually runs on the I/O thread.

The same mechanism drives metric lifetime: the `Meter` handle holds the only user-facing `shared_ptr<MeterImpl>`. When all handles referencing a metric go out of scope, the refcount falls to zero, the destructor runs, and the implementation unregisters itself from the collector — no explicit teardown is needed.

## Concrete Implementation Shape

`StatsDMeterImpl` inherits from both `MeterImpl` and `StatsDMetricBase`. Its `increment()` dispatches asynchronously; the actual accumulation happens in `do_increment()` on the I/O thread where it safely mutates `m_value` and sets a `m_dirty` flag. At each collection interval, `do_process()` calls `flush()`, which formats the StatsD wire message with the `|m` type suffix and posts the buffer to the UDP socket, then resets the accumulator to zero. This means the StatsD backend reports *delta* counts per interval rather than a lifetime total.

## Relationship to `NullCollector`

`NullCollector` provides a no-op implementation of the `Collector` interface used in testing or when metrics are disabled. Its `make_meter()` returns a default-constructed `Meter` (null handle containing no `shared_ptr`), which means `Meter::increment()` silently does nothing. The design at the `MeterImpl` level cleanly supports this: the null path never instantiates a `MeterImpl` at all.

## Summary

`MeterImpl.h` is intentionally minimal — 23 lines declaring one type alias and two virtual functions. Its significance lies not in its size but in the structural role it plays: it is the seam point between the user-facing `Meter` handle and whichever telemetry backend is active, enforces the unsigned-only increment contract that distinguishes meters from counters, and inherits `enable_shared_from_this` to underpin the safe asynchronous dispatch pattern used by the StatsD backend.