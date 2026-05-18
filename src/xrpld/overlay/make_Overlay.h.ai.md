# `make_Overlay.h` — Factory Interface for the Overlay Subsystem

This header defines the two-phase construction interface for the XRPL overlay network: a configuration-parsing function and a factory that produces the concrete `Overlay` implementation. It exists as an intentional dependency seam, keeping construction machinery entirely separate from the `Overlay.h` abstract interface that the rest of the codebase depends on.

## Why a Separate Factory Header

`Overlay.h` is included broadly across the XRPL server — consensus, ledger acquisition, transaction relaying, and RPC all reference the `Overlay` interface. Embedding `make_Overlay` and `setup_Overlay` in that header would force every consumer to also compile against `ServerHandler`, `Resolver`, Boost.Asio, and other heavyweight construction-time dependencies. By isolating those two functions here, the vast majority of the codebase pays only the cost of the abstract interface, while application startup code — the sole caller of the factory — bears the full include burden. This is a classic dependency-inversion boundary and directly mirrors how other major subsystems in rippled are structured (compare `make_NetworkOPs.h`, `make_Manager.h` in peerfinder, etc.).

## Two-Phase Construction

**`setup_Overlay(BasicConfig const&)`** reads four configuration sections and builds an `Overlay::Setup` value object with no live resources attached. From `[overlay]` it extracts an SSL context (via `make_SSLContext`), an optional `ip_limit` cap on connections per IP address, and an optional `public_ip` address that must resolve to a non-private IP or the function throws. From `[crawl]` it assembles a bitmask of `CrawlOptions` flags governing what topology information the node will expose to crawlers (overlay peers, server info, counts, UNL). From `[vl]` it toggles whether the validator list subsystem is active. Finally it parses `[network_id]`, accepting the string aliases `"main"` (0), `"testnet"` (1), and `"devnet"` (2) in addition to raw integers, storing the result in an `std::optional<std::uint32_t>`. The function throws immediately on any invalid configuration, so errors surface at startup before any I/O infrastructure is allocated — exactly the right time to fail loudly.

**`make_Overlay(...)`** accepts the parsed `Setup` alongside all live runtime dependencies and returns `std::unique_ptr<Overlay>`. Internally it is a one-liner that constructs `OverlayImpl` — the concrete class defined entirely within the `detail/` subdirectory — and returns it behind the abstract pointer. No caller ever sees the concrete type; the implementation is fully opaque behind this boundary.

The split matters because it separates concerns cleanly: `setup_Overlay` validates static configuration and can be unit-tested against `BasicConfig` alone, while `make_Overlay` wires up live system resources and cannot meaningfully run without them. There is no intermediate "partially initialized" overlay state to reason about.

## Dependency Surface

The parameters to `make_Overlay` reveal the overlay's runtime requirements:

- **`Application&`** — the central application context; `OverlayImpl` uses it to reach the ledger master, job queue, hash router, and most other subsystems.
- **`ServerHandler&`** — incoming peer connections arrive as HTTP upgrade requests through the same HTTP server infrastructure that handles RPC and admin endpoints. The overlay must integrate with that HTTP layer rather than owning a separate listening socket. This is why `ServerHandler` is passed at construction rather than discovered later.
- **`Resource::Manager&`** — rate-limiting and resource accounting for peer connections, shared with the RPC server and consistent across the whole node's back-pressure strategy.
- **`Resolver&`** — async DNS resolution for bootstrapping peer addresses from hostnames. Keeping this as an injected dependency makes DNS behavior testable and replaceable.
- **`boost::asio::io_context&`** — the single shared I/O context for all async operations. `OverlayImpl` creates its own strand from this context for thread-safe internal dispatch.
- **`BasicConfig const&`** — a second pass at raw config, allowing `OverlayImpl` to read subsections (e.g., peerfinder settings) beyond what `setup_Overlay` extracts into `Setup`.
- **`beast::insight::Collector::ptr const&`** — the metrics collector for overlay-specific counters and gauges. Passing it at construction ensures all performance instrumentation is registered once during initialization and remains stable for the server's lifetime.

## Usage in Application Bootstrap

`Application.cpp` calls both functions together during server initialization:

```cpp
overlay_ = make_Overlay(
    *this,
    setup_Overlay(*config_),
    *serverHandler_,
    *m_resourceManager,
    *m_resolver,
    get_io_context(),
    *config_,
    m_collectorManager->collector());
add(*overlay_);  // register with PropertyStream
```

A comment in that code acknowledges a known technical debt: the overlay is instantiated unconditionally, even in standalone mode where it should be a no-op, because some downstream code incorrectly calls `app.overlay()` regardless of networking state. This does not affect the factory interface itself, but explains why the `if (!config_.standalone())` guard mentioned in the comment was never added.

## Relationship to Sibling Files

This header is the sole construction entry point for the system implemented across `Overlay.h`, `Peer.h`, `Message.h`, `Slot.h`, `Squelch.h`, `ReduceRelayCommon.h`, and the `detail/` directory. The `Overlay::Setup` struct populated by `setup_Overlay` lives in `Overlay.h` and carries exactly the configuration fields that `OverlayImpl` needs at construction time. Everything beyond this narrow interface is an implementation detail invisible to the rest of the server.