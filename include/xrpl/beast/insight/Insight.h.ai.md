# `include/xrpl/beast/insight/Insight.h`

This file is the single-include convenience header for the `beast::insight` metrics framework. It aggregates all fifteen component headers so that any translation unit wanting to instrument code for observability needs only `#include <xrpl/beast/insight/Insight.h>` rather than managing a list of individual includes.

## The beast::insight Framework

The `beast::insight` subsystem provides a lightweight, backend-agnostic instrumentation layer for the XRPL node. The design centres on one abstract factory — `Collector` — and a set of handle types that represent individual metrics.

`Collector` is a pure virtual class whose factory methods (`make_counter`, `make_gauge`, `make_event`, `make_meter`, `make_hook`) return value-semantic handle objects. Each handle wraps a `shared_ptr` to a corresponding `*Impl` abstract interface. The handle is cheap to copy and assign; crucially, **the metric stops being collected when the last handle is destroyed**. This gives metric lifetime the same shape as object lifetime — a subsystem that shuts down simply drops its handles and the collector automatically stops tracking those metric names.

## Metric Kinds

- **`Counter`** — a monotonically adjusted integral value, supporting `++`, `--`, `+=`, `-=`. Semantically a server-side accumulator; the producer owns the delta, not the absolute value.
- **`Gauge`** — an instantaneous snapshot of an arbitrary integral value. The caller both sets (`operator=`) and adjusts (`+=`/`-=`) it. Because a collector may aggregate multiple updates within one reporting interval, rapid fluctuations between polls are intentionally lossy.
- **`Event`** — a push-only timing metric. The `notify()` method accepts any `std::chrono::duration`, which is ceiling-converted to the implementation's `value_type`. Events model operations with an associated elapsed time rather than a running total.
- **`Hook`** — not a metric itself, but a polling callback registered with the collector. The hook is invoked once per collection interval on a collector-managed thread, making it the right choice for gathering values that are expensive to read or that must be sampled rather than pushed.

## Concrete Backends

Two `Collector` implementations are exposed through this header. `NullCollector` silently discards all operations — it is used when metrics are disabled or in unit tests that do not care about observability. `StatsDCollector` ships metrics over UDP to a StatsD aggregation server, accepting an `IP::Endpoint`, an optional dot-separated prefix string, and a `Journal` for logging. The prefix facility on `StatsDCollector`, combined with the two-argument overloads on `Collector` (e.g., `make_counter(prefix, name)`), lets callers namespace their metrics without embedding the prefix in every call site.

`Group` extends `Collector` with a named scope, and `Groups` manages a registry of such scopes — together they allow different components of the node to own logically separate metric namespaces while sharing a single underlying transport.

## Why a Single Aggregation Header

The split between handle types (`Counter.h`, `Gauge.h`, …) and their abstract implementations (`CounterImpl.h`, `GaugeImpl.h`, …) exists so that concrete backend code can include only the `*Impl` interfaces without dragging in the full `Collector` hierarchy, and vice versa. `Insight.h` bridges that split for the common case: production code that both creates metrics and uses them through the handle API simply includes this file and holds a `Collector::ptr` received at construction time, remaining entirely decoupled from whether the underlying backend is StatsD, a null sink, or any future implementation.