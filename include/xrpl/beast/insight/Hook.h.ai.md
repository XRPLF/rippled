# `Hook.h` — Polled Metric Collection Handle

`Hook` is a lightweight reference-counted handle used in the `beast::insight` metrics framework to register a callback that the metrics backend calls at each collection interval. Where `Counter`, `Gauge`, `Event`, and `Meter` all use a push model (the application updates them as events occur), `Hook` inverts that control: the collector calls back into application code on its own schedule, letting the application snapshot internal state on demand.

## Role in the Insight System

The `beast::insight` subsystem provides an abstraction layer over metrics backends, with `StatsDCollector` as the primary live implementation and `NullCollector` as a no-op drop-in. All metric types in the system — `Counter`, `Gauge`, `Event`, `Meter`, and `Hook` — follow the same design: a thin public handle wraps a `std::shared_ptr` to an abstract `*Impl` base. This lets callers hold, copy, and discard metric handles freely without caring about the lifetime of the backend object. `Hook` is no different; it holds a `std::shared_ptr<HookImpl>`.

## Design of `HookImpl`

`HookImpl` defines the interface the backend must implement, with a single `HandlerType = std::function<void(void)>` typedef. The base class is abstract (pure virtual destructor) and inherits from `std::enable_shared_from_this<HookImpl>`, enabling the backend implementation to safely vend weak references to itself. The destructor is defaulted out-of-line in `Hook.cpp` to pin the vtable to a single translation unit — a standard technique for abstract bases in header-only-adjacent designs.

The concrete backend, `StatsDHookImpl` in `StatsDCollector.cpp`, registers the `HandlerType` handler and calls it via `do_process()` on the collector's polling timer. The handler is responsible for reading application state and pushing values to any associated counters or gauges.

## Null State and Lifecycle

`Hook` provides a default constructor that leaves `m_impl` as a null `shared_ptr`. This null state is intentional: code can unconditionally call `collector->make_hook(handler)` on a `NullCollector` and receive a `Hook` that holds a null impl — the metric silently does nothing. This eliminates the need for conditional checks throughout the application when metrics are disabled.

Lifetime management is entirely through reference counting. When the last `Hook` copy referring to a given `HookImpl` is destroyed, the impl is destroyed and the backend deregisters the handler. This gives callers precise control: storing the `Hook` as a member field keeps the handler active for the lifetime of the owning object; letting it go out of scope silently cancels it.

## Factory Pattern

The `Hook(std::shared_ptr<HookImpl> const&)` constructor is `explicit` and marked for implementation use. Callers create hooks exclusively through `Collector::make_hook(handler)`, which is templated to accept any callable and forwards to the virtual `make_hook(HookImpl::HandlerType const&)`. This indirection keeps the handle type backend-agnostic: the same `Hook` type works regardless of whether the underlying collector is `StatsDCollector`, `NullCollector`, or a test double.

## Usage Pattern in the Codebase

Throughout the XRPL server, classes that own metrics (`OverlayImpl`, `LedgerMaster`, `NetworkOPs`, `PeerfinderManager`) declare a `beast::insight::Hook` as a struct member alongside their other metric handles. The hook is initialized in the stats struct constructor by calling `collector->make_hook(handler)` with a lambda or bound member function that reads the component's current runtime state — peer counts, queue depths, ledger sequence numbers — and assigns those values to the sibling `Gauge` or `Counter` members. This pattern cleanly separates the metrics snapshot logic from the event-driven update paths used by the push-model metric types.