# CollectorManager.h — Metrics Collection Service Interface

`CollectorManager` is the application-level service interface that gates access to the `beast::insight` metrics subsystem for the entire XRPL node. Its sole purpose is to provide a stable seam between application startup configuration and the rest of the codebase's telemetry needs.

## Role in the System

The `beast::insight` framework defines a family of metric primitives — counters, gauges, events, meters, and hooks — that can be wired to an external monitoring backend. `CollectorManager` acts as the application's ownership and access point for the concrete `Collector` implementation chosen at startup. It is created once, held as `std::unique_ptr<CollectorManager>` by `Application`, and consumed by a dozen or more subsystems that each receive a `shared_ptr<Collector>` or a named `Group` during construction: `JobQueue`, `Resource::Manager`, `LedgerMaster`, `InboundLedgers`, `InboundTransactions`, `NetworkOPs`, `Overlay`, the I/O latency sampler, and more.

## Interface Design

The interface exposes only two methods. `collector()` returns a reference to the shared `Collector` pointer — the root factory for all metric objects. `group(name)` returns a named `Group`, which is itself a `Collector` subtype that automatically prefixes every metric it creates with the group name. Subsystems that need namespaced metrics (e.g., the job queue using `"jobq"`) call `group()` to get a scoped factory; subsystems that manage their own namespacing call `collector()` directly.

Keeping the interface this thin is intentional: `CollectorManager` is not itself a metric creator. It is a lifecycle owner and a locator. Metric creation belongs to the `Collector` interface and is the concern of whichever subsystem needs it.

## Implementation and Configuration

The concrete type `CollectorManagerImp` (defined in `CollectorManager.cpp`) is hidden behind the factory function `make_CollectorManager(params, journal)`, which reads from the `[insight]` config section. When `server = "statsd"`, it instantiates a `StatsDCollector` that ships UDP datagrams to the configured StatsD address with a metric-name prefix. Any other value (or absent configuration) produces a `NullCollector` — an implementation whose operations are intentional no-ops.

This two-path design is a key reliability decision: operators who have not set up a StatsD server pay no overhead and experience no errors. The `NullCollector` fallback means every call site throughout the codebase can unconditionally record metrics without checking whether a backend is configured. The null path is not added later as a workaround — it is the expected default.

Groups are managed through a `beast::insight::Groups` object, which is constructed over the chosen collector and handles group lifecycle. Calling `group("name")` returns a shared pointer that is cached by the `Groups` object, so multiple subsystems asking for the same group name receive the same instance.

## Lifecycle and Ownership

`CollectorManager` is constructed near the top of `Application`'s member-initialization list, before `JobQueue`, `SHAMapStore`, or any network subsystem. This ordering is necessary because those constructors immediately call `m_collectorManager->group(...)` or `m_collectorManager->collector()`. The raw `Collector::ptr` (a `shared_ptr`) is handed to each consumer, so the collector itself outlives the `CollectorManager` wrapper only if a consumer holds the last reference — though in practice `Application` destruction order ensures orderly teardown.

The abstract interface means test harnesses or alternative implementations can replace `CollectorManagerImp` without touching any consumer. Since all consumers hold a `shared_ptr<Collector>`, they are entirely decoupled from the management layer.