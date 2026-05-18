# `ResourceManager.cpp` — Resource Manager Implementation

## Role in the System

`ResourceManager.cpp` is the concrete implementation of the `Resource::Manager` interface, the entry point for XRPL's per-peer resource-consumption tracking subsystem. The resource system exists to protect a rippled node from overload: every inbound or outbound connection is assigned a `Consumer` handle that accumulates a load balance as it makes requests. When that balance exceeds defined thresholds, the node can warn or drop the offending peer. This file wires together the stateless `Logic` engine, a background maintenance thread, and the public factory function `make_Manager()` that the rest of the node calls at startup.

## The `ManagerImp` Private Implementation

The entire implementation is hidden inside `ManagerImp`, a class private to the translation unit that inherits `Manager`. Callers only ever see `std::unique_ptr<Manager>` returned by `make_Manager()`; the concrete type is completely opaque. This is a deliberate pImpl-style design: the header declares a pure virtual interface and the implementation detail — including the `Logic` hash tables, the background thread, and the mutex — never leak into headers that consumers must compile against.

`ManagerImp` owns two major resources: a `Logic` instance and a `std::thread`. The `Logic` object (defined in `detail/Logic.h`) holds the actual hash tables of `Entry` records, the intrusive lists of active inbound/outbound/admin entries, and the gossip import table. Every public method on `ManagerImp` is a thin pass-through to the corresponding `Logic` method; `ManagerImp`'s own responsibility is purely lifecycle and threading.

## Background Thread and Shutdown

Construction launches a dedicated thread named `"Resource::Mngr"` that runs `ManagerImp::run()`. The loop calls `logic_.periodicActivity()` — which decays load balances, evicts stale entries, and fires telemetry meters — then waits up to one second on a `std::condition_variable` before repeating. The one-second cadence is intentional: load balances are time-weighted, and a sub-second tick would distort the decay math while a multi-second tick would make the system sluggish to respond to traffic spikes.

Shutdown is clean and deterministic. The destructor acquires `mutex_`, sets `stop_ = true`, and signals the condition variable before calling `thread_.join()`. Because the `cond_.wait_for()` in `run()` holds the same `mutex_` via `std::unique_lock`, the signal interrupts the sleep immediately regardless of where in the one-second window the destructor fires. This avoids any fixed-sleep teardown latency. The ordering — signal under lock, then join outside the lock — is correct: the lock is released before `wait_for` returns, so the destructor can re-acquire it without deadlock.

## Consumer Creation and Proxy Awareness

The `Manager` interface exposes three consumer flavors, mapped directly to the `Kind` enum in `detail/Kind.h`:

- **Inbound** (`kindInbound`): connections arriving at the server port, subject to normal rate limits.
- **Outbound** (`kindOutbound`): connections the node initiates to peers, tracked separately.
- **Unlimited** (`kindUnlimited`): trusted or administrative connections that bypass rate limiting but can still be subject to administrative RPC restrictions.

The overloaded `newInboundEndpoint(address, proxy, forwardedFor)` is the most interesting variant. When a reverse proxy sits in front of the node, the TCP source address is the proxy's IP rather than the actual client's. This overload accepts the raw `X-Forwarded-For` string value and uses `boost::asio::ip::make_address()` to parse it. If parsing succeeds, the proxied IP is converted to a `beast::IP::Endpoint` via `beast::IPAddressConversion::from_asio()` and used as the consumer key. If parsing fails — because the header is absent, malformed, or contains a non-IP token — the code logs a warning at journal warn level and falls back to keying on the proxy's own address. This fallback is correct: it prevents a malformed header from crashing the node or creating an untracked consumer, at the cost of grouping all traffic through a broken proxy under one entry.

## Gossip Protocol Integration

The resource system participates in a peer-to-peer gossip mechanism for sharing load information. `exportConsumers()` serializes the current inbound consumer table into a `Gossip` struct (a flat vector of `{balance, address}` items), which the network layer can broadcast to peers. `importConsumers(origin, gossip)` ingests gossip received from another node, keying imported data by the `origin` identifier. This allows a cluster of rippled nodes to share knowledge about misbehaving IPs even if the abusive peer is connected to only one node — a critical property for protecting the peer-to-peer mesh as a whole.

## Observability

`Manager` inherits `beast::PropertyStream::Source` and registers under the name `"resource"`, making all resource state accessible through the node's diagnostic property-stream tree. `onWrite(map)` delegates to `Logic::onWrite()`, which walks the live tables and emits entries into the stream. The two `getJson()` overloads provide JSON snapshots — one unfiltered, one filtered by a minimum balance threshold — suitable for RPC introspection commands.

## Thread Safety

All mutable state lives inside `Logic`, which guards it with a `std::recursive_mutex`. `ManagerImp` itself only owns the `stop_` flag (protected by its own `mutex_`) and the background thread handle. Because `Logic`'s lock is recursive, `periodicActivity()` can call helper methods that also acquire the lock without deadlocking, a pattern that matters because `Logic` methods are called from both the background thread and from network-layer threads creating consumers.