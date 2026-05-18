# `beast/insight/Collector.h` — Metrics Collection Interface

## Role in the System

`Collector` is the central factory interface for the `beast::insight` metrics subsystem within rippled. It defines the contract that any metrics backend must satisfy, allowing ledger components to instrument themselves with counters, gauges, meters, events, and polling hooks — all without coupling to a specific reporting destination. Components receive a `Collector::ptr` (a `std::shared_ptr<Collector>`) at construction time and use it to create metric objects; the actual reporting destination is determined entirely by which concrete implementation was injected.

The two known implementations are `NullCollector`, which silently discards all metrics (useful for tests or when monitoring is disabled), and `StatsDCollector`, which encodes metrics in the StatsD UDP wire format and sends them to a configured server address.

## Design: Factory + Handle Separation

`Collector` uses a two-level design. The interface itself acts as a factory: `make_counter()`, `make_gauge()`, `make_meter()`, `make_event()`, and `make_hook()` each return a small value-type handle (`Counter`, `Gauge`, `Meter`, `Event`, `Hook`). These handles are cheap to copy and assign — they are simply `shared_ptr` wrappers around an `Impl` base class (`CounterImpl`, `GaugeImpl`, etc.). The lifetime of the underlying metric is tied to the handle: when the last copy of a handle is destroyed, the metric stops being collected.

This separation is deliberate. The `Collector` holds shared state (network connections, aggregation buffers, collection threads), while the returned handles are per-metric tokens that instrument code can store as member variables. A component that holds a `Counter` member needs no knowledge of the `Collector`'s lifecycle beyond the initial construction call.

## The `make_hook` Overloads and Polling Style

The most architecturally notable factory method is `make_hook`. Rather than requiring components to push every individual metric update immediately, `Hook` supports a *polling* style: the component registers a callback that the collector fires at each collection interval on its own internal thread. This allows a class to compute and update all its metrics in one burst, which can be more efficient than individual pushes for values that change frequently. The `Collector` class provides a template `make_hook(Handler)` overload that wraps any callable into `HookImpl::HandlerType` (a `std::function<void()>`), forwarding to the virtual `make_hook(HookImpl::HandlerType const&)`. This pattern avoids virtual template methods while still giving callers a convenient, type-erased interface.

## Prefix Namespacing

Each `make_*` factory comes in two overloads. The single-argument form takes only a `name`; the two-argument form takes a `prefix` and a `name`, concatenating them with a dot separator (`prefix + "." + name`) before forwarding to the virtual single-argument form. When `prefix` is empty, the concatenation is skipped and the bare name is used directly. This small convenience lets subsystems build hierarchically namespaced metric names (e.g., `"ledger.fetcher.hits"`) without requiring every call site to manually construct the full string, while keeping all the real dispatch logic in a single virtual method per metric type.

## Null Handle Safety

All metric handle types default-construct to a "null" state with no backing `Impl`. Every mutation operation on a handle (e.g., `Counter::increment`, `Gauge::set`) guards against the null case with an `if (m_impl)` check. This means that passing a default-constructed handle — or one returned from `NullCollector` — is always safe; instrumentation code never needs special-case logic to disable itself, and the cost in the null path is a single pointer comparison.

## Relationship to Concrete Implementations

`NullCollector::New()` and `StatsDCollector::New(...)` are static factory functions that return `shared_ptr<Collector>`, reinforcing that callers should only ever interact with the base interface. `StatsDCollector` additionally accepts an `IP::Endpoint` and a `Journal` for logging, but that complexity is fully hidden behind the `Collector` abstraction. Any component that stores a `Collector::ptr` can be tested with a `NullCollector` and deployed with a `StatsDCollector` without any changes to the component itself.