# `Source.h` — Abstract Peer Address Provider

## Role in the System

`Source.h` defines the `Source` abstract base class within the `xrpl::PeerFinder` namespace. It sits at the heart of the PeerFinder bootstrapping strategy: when a node starts cold (no local peer cache) or exhausts all known-working addresses, it must find new peers from somewhere. `Source` is the uniform interface behind every mechanism that can supply a list of IP addresses for that purpose.

The comment in the header names the intended implementations: static addresses from the config file, addresses from a local file on disk, peer lists fetched from a remote HTTPS URL, or DNS-based peer discovery. The only shipped concrete subclass in this codebase is `SourceStrings`, which backs the `[ips]` and `[ips_fixed]` config-file stanzas.

## Interface Design

The class is deliberately minimal — three pure or defaulted virtual methods and one nested result type:

- `name()` returns a diagnostic label used in log messages by `Logic::fetch()`. It carries no operational meaning beyond identifying the source in log output.
- `fetch(Results& results, beast::Journal journal)` is the single operation that matters: populate `results.addresses` with `beast::IP::Endpoint` values, or set `results.error` on failure. The `Results` struct is a value type (not a future or callback), reflecting that the fetch is **synchronous** — the `Logic` layer calls it while holding no lock, but documents this as a known concern (`// VFALCO NOTE The fetch is synchronous, not sure if that's a good thing`).
- `cancel()` has a default no-op implementation. It exists for sources that might eventually run asynchronously (e.g., an HTTP fetch that could be in-flight when the node stops). `Logic` tracks the currently executing `Source` in `fetchSource_` and calls `cancel()` on it during shutdown to give future async implementations a hook to abort early. Since all current implementations are synchronous and `cancel()` is a no-op, this is purely defensive future-proofing.

## Lifecycle and Usage in `Logic`

`Logic` maintains two separate collections: `m_sources` (sources polled periodically) and the immediate use via `addStaticSource()`, which calls `fetch()` directly at registration time. The `fetch()` wrapper in `Logic` records the current source in `fetchSource_` before calling through and clears it afterward, checking `stopping_` on both sides of the call — this is the cancellation rendezvous point. If `stopping_` is set between the two checks, `Logic` drops the results silently rather than inserting addresses into a shutting-down bootcache.

Successful fetches pipe their `IPAddresses` vector into `Bootcache::insertStatic()`, seeding the bootstrap cache that the connection engine draws from when it has no live peers to connect to.

## Design Tradeoffs

The synchronous `fetch()` signature is the most notable constraint. It simplifies implementations (no strand/executor threading concerns, no callback lifetime management) but means a slow or unresponsive remote source can stall the bootstrap thread. The `cancel()` hook was clearly added with the intention of revisiting this — it provides the extension point needed to move to an async model without changing the interface, if a concrete async implementation is ever written.

The `Results` struct uses `boost::system::error_code` rather than exceptions, keeping error propagation explicit and cheap for the common success case. `IPAddresses` is `std::vector<beast::IP::Endpoint>`, the same type used throughout PeerFinder, so results flow directly into the bootcache without conversion.