# `xrpld/rpc/detail/Tuning.h`

This header is the single authoritative source for all tunable constants in the XRPL RPC subsystem. Rather than scattering magic numbers throughout the handlers, every paginated command, every throttle decision, and every request-size gate references a named constant defined here — making capacity planning visible and changes safe. The entire file lives inside the nested namespace `xrpl::RPC::Tuning` and is consumed across nearly a dozen handler and infrastructure files.

## `LimitRange` and Paginated Commands

The `LimitRange` struct captures the three-sided contract that every paginated RPC command makes with its callers: a floor (`rmin`), a sensible default (`rDefault`), and a ceiling (`rmax`). The central enforcement lives in `RPCHelpers.cpp`'s `readLimitField()`:

```cpp
limit = std::max(range.rmin, std::min(range.rmax, limit));
```

This clamping only fires for ordinary clients. Connections that carry an *unlimited* role (typically authenticated admin or local connections) bypass it entirely — they receive exactly what they asked for, uncapped. This design keeps the API honest for public endpoints while giving operators full access from trusted connections.

Most account-scoped commands (`accountLines`, `accountChannels`, `accountObjects`, `accountOffers`, `accountTx`) share identical limits — `{10, 200, 400}` — reflecting the uniform internal cost model for iterating ledger objects by account. Two commands stand out from this pattern:

- **`bookOffers`** uses `{0, 60, 100}`. Its floor is zero, not ten. A limit of zero in `readLimitField()` triggers a separate invalid-field error, so the zero minimum is practically unreachable through normal clients; it exists to allow internal callers to request a purely structural probe without requiring a minimum page.
- **`nftOffers`** uses `{50, 250, 500}`, a higher ceiling than account-level commands. NFT offer books can legitimately accumulate hundreds of bids, so the ceiling is raised to avoid forcing callers into excessive pagination.
- **`noRippleCheck`** uses `{10, 300, 400}`, a higher default than most, because the no-ripple audit command is typically invoked precisely to scan a large set of trust lines and collects its results in a single pass.

## Page Length and the Binary/JSON Asymmetry

`pageLength(bool isBinary)` selects between `binaryPageLength = 2048` and `jsonPageLength = 256` — an 8× gap. This isn't arbitrary. JSON-encoded ledger objects carry field names, string representations of amounts, and human-readable type tags; the same data in binary (XRPL's canonical serialisation format) is roughly an order of magnitude more compact. Capping JSON responses at 256 objects and binary at 2048 keeps the wire payload and memory pressure at comparable levels. `LedgerData.cpp` applies this directly:

```cpp
auto maxLimit = RPC::Tuning::pageLength(isBinary);
```

## Pathfinding Throttles

Two constants gate the ripple path-finding subsystem. `maxPathfindsInProgress = 2` is an atomic counter guard in `LegacyPathFind.cpp`: if two path-find operations are already running concurrently, new requests are rejected immediately rather than queued. `maxPathfindJobCount = 50` is a second gate applied to the broader job queue depth — if the job queue is already that long (or local load is high), path-find requests are refused before they are even submitted. Together these prevent path-finding — the most computationally expensive RPC operation — from starving other work.

The limits for source currencies in path-find requests (`max_src_cur = 18`, `max_auto_src_cur = 88`) cap the combinatorial explosion that occurs when the pathfinder searches across many source currency candidates. The auto-source limit is higher because those currencies are generated algorithmically by the server rather than specified by the user, and the server can budget for them more accurately.

## Infrastructure Limits

`maxJobQueueClients = 500` is checked in `RPCHandler.cpp` before submitting an RPC dispatch job. If the queue already has 500 pending client requests, new arrivals are dropped, preventing unbounded memory growth under burst traffic.

`maxRequestSize = 1_000_000` (1 MB) is enforced in `ServerHandler.cpp` before any JSON parsing begins. Parsing is deferred past this check deliberately — a malicious 500 MB payload that passes the size gate would cause the JSON parser to allocate aggressively; rejecting it cheaply at the byte-count level prevents that class of amplification attack.

`maxValidatedLedgerAge = 2 minutes` is used in `TransactionSign.cpp` to refuse transaction signing when the node's most-recently validated ledger is more than two minutes old. A stale ledger implies the node may be partitioned or behind, and signing a transaction against it could reference incorrect fee levels or account states.

`defaultAutoFillFeeMultiplier = 10` and `defaultAutoFillFeeDivisor = 1` seed the fee auto-fill calculation in `TransactionSign.cpp`. The effective multiplier (10×) is intentionally conservative — auto-filled fees must be high enough to clear the network under typical load without user intervention, and the 10× factor over the current base fee provides that headroom.