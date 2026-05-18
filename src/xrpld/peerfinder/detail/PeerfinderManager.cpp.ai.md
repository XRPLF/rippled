# `PeerfinderManager.cpp` — PeerFinder Manager Implementation

## Role in the System

This file is the assembly point for the XRPL peer-discovery subsystem. It defines `ManagerImp`, the concrete implementation of the abstract `Manager` interface declared in `PeerfinderManager.h`. Rather than exposing the implementation type to callers, the file provides a single factory function `make_Manager()` that returns a `std::unique_ptr<Manager>`. This keeps the entire implementation — including its heavyweight dependencies — hidden behind the public interface, a deliberate PIMPL-style isolation that allows the subsystem to evolve without touching call sites.

## What `ManagerImp` Assembles

`ManagerImp` does not contain significant algorithmic logic itself; its job is composition. It holds and wires together five major collaborators:

- **`StoreSqdb m_store`** — SQLite-backed persistence (schema version 4) that saves and loads the bootstrap peer cache across process restarts. It is opened lazily inside `start()` so that no disk I/O occurs during construction.
- **`Checker<boost::asio::ip::tcp> checker_`** — An async TCP socket prober that verifies whether a candidate peer's listening port is actually reachable. It is constructed with the shared `io_context` so its async operations participate in the same event loop as the rest of the node.
- **`Logic<decltype(checker_)> m_logic`** — The templated core of peer-finding: it manages the live and boot caches, slot lifecycle, connection counts, endpoint handout strategy, and all policy decisions. Nearly every `Manager` virtual method in `ManagerImp` is a one-liner that forwards to `m_logic`.
- **`boost::asio::executor_work_guard` (via `work_`)** — Stored as `std::optional<executor_work_guard>` and constructed `std::in_place` during initialization to keep the `io_context` running even when the queue is transiently empty. Destroying it (by calling `work_.reset()`) is the signal to let the context drain after queued handlers complete. This is the canonical Asio shutdown pattern.
- **`Stats` (nested private struct)** — Registers a metrics hook with a `beast::insight::Collector`, exposing two gauges: `Active_Inbound_Peers` and `Active_Outbound_Peers`. The `collect_metrics()` callback reads live counts directly out of `m_logic.counts_`, protected by `m_statsMutex` to guard against concurrent metric collection.

## Lifecycle: Start and Stop

`start()` performs two ordered operations: it opens the SQLite store (which runs schema migration if the on-disk version is outdated) and then calls `m_logic.load()` to populate the in-memory bootcache from persisted entries. Nothing is written to disk before `start()` is called, making construction cheap and safe.

`stop()` is idempotent by design. It checks `if (work_)` before acting, so calling it twice — including from the destructor — is harmless. The sequence on shutdown is deliberate: reset the work guard first (unblock the io_context), then `checker_.stop()` (cancel all pending async socket probes), then `m_logic.stop()` (cancel any in-flight source fetch). This ordering matters because `Logic` may hold shared references to `Checker` operations that must be cancelled before the checker itself destructs.

The destructor simply delegates to `stop()`, so `ManagerImp` is safe to destroy at any point after construction without requiring an explicit shutdown call.

## Slot Type Coercion

The public API accepts `std::shared_ptr<Slot>`, but `Logic` operates on the concrete `SlotImp` type. Every slot-event method (`on_endpoints`, `on_closed`, `on_failure`, `onConnected`, `activate`, `redirect`) performs a `std::dynamic_pointer_cast<SlotImp>` before forwarding. This reflects a deliberate layering boundary: callers interact through the opaque `Slot` interface while the internal machinery requires the full `SlotImp`. The cast is safe as long as all `Slot` instances in circulation were created by `m_logic` (which they are — `new_inbound_slot` and `new_outbound_slot` both return `shared_ptr<SlotImp>` wrapped as `shared_ptr<Slot>`).

## The Unimplemented Method

`addFallbackURL()` exists in `ManagerImp` but its body contains only a comment: `// VFALCO TODO This needs to be implemented`. The corresponding pure-virtual declaration is also commented out of the `Manager` base class header with an explicit note that it is unimplemented. Fallback sources are currently only supplied as static string lists via `addFallbackStrings()`, which wraps the strings in a `SourceStrings` object and registers it as a static source on the logic layer.

## Metrics Design

The `Stats` struct uses `beast::insight::Collector` hooks: a single `hook` object whose callback is `collect_metrics()`, plus two `Gauge` objects. The hook mechanism means the collector drives collection — it calls the registered handler when it wants fresh data rather than the application pushing values on every change. The mutex around `collect_metrics()` is therefore protecting against concurrent calls from the collector framework, not from peer connection events.

## Dependency and Interface Summary

`ManagerImp` is completely opaque to its callers — the class definition never appears in any header. The only externally visible symbol beyond `Manager` itself is the `make_Manager` factory. This enforces that all peerfinder consumer code programs to the `Manager` interface, keeping compile-time dependencies minimal and test substitution straightforward.