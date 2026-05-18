# `make_Manager.h` — PeerFinder Factory Declaration

This header is the public entry point for constructing the PeerFinder subsystem. It declares a single factory function, `make_Manager()`, that produces an owned `Manager` instance, hiding the concrete implementation type entirely from callers.

## Role in the System

The PeerFinder subsystem is responsible for discovering, tracking, and managing peer connections on the XRP Ledger network. It maintains a database of known peer endpoints, governs slot allocation for inbound and outbound connections, and periodically runs logic to achieve the target peer counts defined by `Config`. The `Manager` abstract class (declared in `PeerfinderManager.h`) is the façade through which all of this behaviour is driven.

`make_Manager.h` provides the sole mechanism by which a `Manager` is created. The concrete class — `ManagerImp`, defined inside `detail/PeerfinderManager.cpp` — is never exposed in any public header. This enforces a hard compile-time boundary: consumers of the PeerFinder subsystem link against the interface, not the implementation.

## The Factory Function

```cpp
std::unique_ptr<Manager>
make_Manager(
    boost::asio::io_context& io_context,
    clock_type& clock,
    beast::Journal journal,
    BasicConfig const& config,
    beast::insight::Collector::ptr const& collector);
```

The implementation is a one-liner that delegates directly to `std::make_unique<ManagerImp>(...)`. The indirection through a named factory rather than exposing the constructor directly is deliberate: it keeps `ManagerImp` out of header scope, preventing any translation unit from constructing or depending on the concrete type.

Each parameter maps to a distinct concern:

- **`io_context`** — The Boost.Asio executor used for all asynchronous timer and I/O operations inside the manager. PeerFinder schedules periodic tasks (endpoint fetching, cache flushes) through this context.
- **`clock`** — A `beast::abstract_clock<std::chrono::steady_clock>` used for all time-based decisions. Accepting an abstract clock rather than calling `std::chrono::steady_clock::now()` directly makes the manager unit-testable with a mock clock.
- **`journal`** — A `beast::Journal` sink for structured diagnostic output, labelled `"PeerFinder"` by the caller in practice.
- **`config`** — A `BasicConfig` carrying the raw configuration (from the server's config file) used during construction. The manager converts this into its own `PeerFinder::Config` via `Config::makeConfig()`.
- **`collector`** — A `beast::insight::Collector` pointer for publishing metrics. The manager registers internal stats counters (active inbound/outbound peer counts, etc.) against this collector.

## Caller Context

The sole production callsite is in `OverlayImpl`'s constructor (`src/xrpld/overlay/detail/OverlayImpl.cpp`), where `m_peerFinder` is initialised as a member:

```cpp
, m_peerFinder(
      PeerFinder::make_Manager(
          io_context,
          stopwatch(),
          app_.getJournal("PeerFinder"),
          config,
          collector))
```

The `Overlay` layer owns the `Manager` for its entire lifetime and is the only entity that drives it — calling `start()`, `stop()`, `new_inbound_slot()`, `new_outbound_slot()`, `on_closed()`, `once_per_second()`, and so on. Peer connection events propagate upward through `Slot` handles; the manager never calls back into the overlay directly.

## Design Pattern

The header follows the same factory-function idiom used elsewhere in the rippled codebase (e.g., `make_Overlay.h` for the overlay subsystem, `Resource::make_Manager` for the resource manager). The pattern achieves three things simultaneously: the `unique_ptr` return communicates exclusive ownership clearly; the opaque concrete type prevents accidental direct construction; and the header dependency is kept minimal — only `PeerfinderManager.h`, `<boost/asio/io_context.hpp>`, and `<memory>` are required to consume this interface.