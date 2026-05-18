# `SourceStrings.h` — Static Peer Address Source for PeerFinder

## Role in the System

`SourceStrings.h` declares `SourceStrings`, a concrete subclass of the `Source` bootstrap interface used by the XRPL `PeerFinder` subsystem. Its purpose is to wrap a pre-configured list of IP address strings — typically read from the node's configuration file — into a form that the PeerFinder bootstrap logic can query uniformly alongside other address sources such as remote HTTPS endpoints or DNS-based sources.

PeerFinder uses `Source` objects as fallbacks during startup when the local peer cache is empty or stale. `SourceStrings` represents the simplest and most immediate of these fallbacks: hard-coded or operator-specified peer addresses.

## Design: Public Interface, Hidden Implementation

The header exposes only the minimal public surface needed for callers: a type alias `Strings = std::vector<std::string>` and a static factory method `New(name, strings)`. The actual implementation class, `SourceStringsImp`, lives entirely in `SourceStrings.cpp` and inherits privately from `SourceStrings`, which itself inherits from `Source`. This two-level inheritance isolates all parsing logic and stored state from header consumers, keeping compile dependencies minimal and the ABI stable.

The `New()` factory returns a `std::shared_ptr<Source>` — not `std::shared_ptr<SourceStrings>` — which means callers work exclusively through the `Source` interface. This deliberate upcast at the boundary prevents accidental coupling to the concrete type and matches how `PeerfinderManager` passes the result directly to `m_logic.addStaticSource()`.

## What `fetch()` Does

The `SourceStringsImp::fetch()` override (in the `.cpp`) iterates the stored string vector, attempting to parse each entry into a `beast::IP::Endpoint` via `Endpoint::from_string()`. Any entry that parses to an unspecified (invalid) endpoint is silently skipped; valid endpoints are appended to `results.addresses`. There is a minor quirk in the implementation: a failed parse attempt is immediately retried with the identical string before the `is_unspecified` guard — effectively a no-op retry — likely a leftover from an earlier version that tried alternate parsing strategies.

The `Results` struct inherited from `Source` provides both an error code and an address list, but for `SourceStrings` the error code is never set: since the data is already in memory, there are no I/O failure modes. The `cancel()` hook inherited from `Source` is also a no-op for the same reason — there is no asynchronous operation to interrupt.

## Call Site

`SourceStrings::New` is invoked exactly once, inside `PeerfinderManager::addFallbackStrings()`, which forwards the node's configured IP strings from the rippled config layer into the PeerFinder logic engine as a static (non-refreshable) source. Static sources are fetched once and never re-polled, which is appropriate for a list of fixed operator-defined addresses.

## Summary

`SourceStrings.h` is a small but structurally important piece of the PeerFinder bootstrap chain. Its value lies not in complexity but in clean separation: it hides the parsing and storage details behind the `Source` interface, uses a factory method to prevent direct construction of the concrete type, and feeds operator-configured peer addresses into the same polymorphic source pipeline used by dynamic sources like remote URL fetchers.