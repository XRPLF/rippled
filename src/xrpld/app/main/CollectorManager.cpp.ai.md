# CollectorManager.cpp

`CollectorManager.cpp` provides the application-level entry point for XRPL's metrics telemetry infrastructure. Its purpose is narrow but important: translate node configuration into a live `beast::insight::Collector` instance that the rest of the application uses to emit counters, gauges, events, and meters — optionally shipping them to a StatsD endpoint.

## Role in the Application

The XRP Ledger daemon (`rippled`) exposes internal performance metrics through the `beast::insight` subsystem, a thin abstraction over metrics backends. During application initialization, `Application.cpp` calls `make_CollectorManager()` with the contents of the `[insight]` configuration section (keyed by `SECTION_INSIGHT`) and receives back a `std::unique_ptr<CollectorManager>`. The resulting object is stored for the application's lifetime and surfaced to subsystems via `getCollectorManager()`.

## Implementation Pattern

The file uses an opaque-implementation pattern: the abstract `CollectorManager` interface is declared in the header with only two pure-virtual methods, while the concrete class `CollectorManagerImp` is defined entirely within the `.cpp` file and never visible to callers. The `make_CollectorManager()` factory function is the sole public construction path. This keeps the concrete implementation's dependencies (StatsD, IP address parsing) out of every translation unit that includes the header, avoiding unnecessary recompilation.

## Collector Selection and Fallback

The constructor of `CollectorManagerImp` reads a single configuration key — `server` — from the `params` section:

- If `server == "statsd"`, it constructs a `beast::insight::StatsDCollector` by additionally reading `address` (parsed through `beast::IP::Endpoint::from_string`) and `prefix` (a string prepended to all metric names).
- For any other value — including an absent key or unrecognized backend name — it falls back silently to `beast::insight::NullCollector::New()`.

The fallback-to-null design is deliberate and defensively sound. All application code that emits metrics does so unconditionally, without checking whether a real backend is connected. `NullCollector` absorbs all metric calls as no-ops. This means misconfiguration or a missing `[insight]` block never crashes the node; it simply disables telemetry. The tradeoff is that silent misconfiguration is possible — if `server` is misspelled, the node starts without metrics and gives no warning.

Address validation is delegated entirely to `beast::IP::Endpoint::from_string`. If an invalid address is supplied alongside `server = statsd`, `from_string` will throw (or produce an undefined endpoint), propagating the error up through the constructor and ultimately failing application startup. There is no explicit try/catch here, so the error handling is caller-enforced.

## Groups Abstraction

After creating the collector, the constructor calls `beast::insight::make_Groups(m_collector)`, storing the result as `std::unique_ptr<beast::insight::Groups>`. The `group(name)` method delegates to `m_groups->get(name)`, which performs a find-or-create lookup for a named `Group::ptr`.

`Groups` lets different subsystems partition their metrics under separate name prefixes without ever needing direct access to the underlying collector. A component asks the `CollectorManager` for the group named `"ledger"` or `"consensus"`, then creates its counters and gauges within that group's namespace. This keeps metric naming organized and prevents collisions across unrelated subsystems.

## Ownership and Lifetime

`m_collector` is held as `beast::insight::Collector::ptr`, which is a `std::shared_ptr<Collector>`. The `Groups` object holds its own `shared_ptr` to the same collector internally, which is why `CollectorManagerImp` can destroy its own `m_collector` reference without invalidating any `Group` objects that were handed out — group lifetimes can safely outlive the manager reference if needed, though in practice `CollectorManagerImp`'s `unique_ptr` destructor tears down both `m_groups` and `m_collector` in order when the application shuts down.