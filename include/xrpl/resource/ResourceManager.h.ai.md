# `include/xrpl/resource/ResourceManager.h`

## Role in the System

`ResourceManager.h` declares `xrpl::Resource::Manager`, the abstract interface that governs **load tracking and resource consumption enforcement** for network endpoints in rippled. Its purpose is to protect the node from abuse — whether from a single misbehaving peer hammering expensive RPCs or from a distributed flood — by tracking cumulative resource usage per IP endpoint and signalling when a connection should be warned or cut.

The interface sits at the boundary between the network layer (which creates connections) and the resource-enforcement logic (which decides when those connections become too costly). Callers never interact with the concrete implementation directly; they receive `Consumer` handles and periodically charge them for work performed.

## The `Manager` Abstract Interface

`Manager` inherits from `beast::PropertyStream::Source`, giving every concrete instance a slot in the node's live diagnostic property tree under the name `"resource"`. This inheritance is set up in the `.cpp` file (`Manager::Manager()` passes `"resource"` to the `PropertyStream::Source` constructor), meaning diagnostics are automatically wired in for any conforming implementation.

The interface exposes four endpoint-creation methods that return `Consumer` objects:

- **`newInboundEndpoint(address)`** — registers an inbound peer connection keyed purely by its IP address (port is ignored for inbound, since client ports are ephemeral).
- **`newInboundEndpoint(address, proxy, forwardedFor)`** — handles proxy-fronted connections. When `proxy` is true the manager parses the `X-Forwarded-For`-style string to recover the originating IP and tracks that instead of the proxy's address. If the forwarded string is malformed, the implementation falls back to the proxy's own address and logs a warning. This is a deliberate defensive default: a bad forward header never silently bypasses tracking.
- **`newOutboundEndpoint(address)`** — registers an outbound peer. Outbound connections are tracked separately from inbound ones; the underlying `Logic` maintains distinct intrusive lists for each category.
- **`newUnlimitedEndpoint(address)`** — creates a `Consumer` that is permanently exempt from resource limits, used for trusted validators and administrative connections. An "unlimited" entry is never placed on the inbound or outbound lists.

The returned `Consumer` is a lightweight handle (a pointer pair to a `Logic` and an `Entry`) with value semantics — it is copy-constructible and reference-counted internally. Callers charge it for work via `Consumer::charge(Charge const&)`, which returns a `Disposition` (`ok`, `warn`, or `drop`).

## Gossip: Cross-Peer Load Sharing

`exportConsumers()` and `importConsumers()` implement a **gossip protocol** for propagating load information across the peer network. `Gossip` is a simple `vector<Item>` where each `Item` holds a `(balance, address)` pair. When a peer tells us that a given IP has been abusive on its end, the local manager can factor that imported balance into its own threshold calculations, making it much harder for an attacker to spread load across many trusted hubs and stay below per-node limits individually.

`importConsumers` takes an `origin` string that uniquely identifies the peer supplying the data. The `Logic` layer stores these in a separate `importTable_` keyed by origin, so stale data from a disconnected peer can be invalidated cleanly.

## Concrete Implementation: `ManagerImp`

`make_Manager()` is the sole factory function and the only way to obtain a concrete instance. It returns a `std::unique_ptr<Manager>` wrapping `ManagerImp`, which is defined entirely inside `ResourceManager.cpp`. This Pimpl-like separation means the heavy `Logic` header (with its hash maps and intrusive list machinery) is never transitively included by callers of the public interface.

`ManagerImp` owns:
- A `Logic` instance — the stateful core that holds a `hash_map<Key, Entry>` plus four intrusive lists (`inbound_`, `outbound_`, `admin_`, `inactive_`) protected by a `std::recursive_mutex`.
- A **background thread** (`Resource::Mngr`) that calls `logic_.periodicActivity()` every second. This periodic sweep decays balances over time (preventing a burst from permanently blacklisting an endpoint) and promotes inactive entries to a cleanup list. The thread uses a `condition_variable` with a one-second timeout so it shuts down promptly when the destructor sets `stop_` and signals the condition.

The destructor acquires the mutex, flips `stop_`, notifies the condition variable, then joins the thread — a clean shutdown sequence that guarantees the background thread never outlives the `Logic` it references.

## `Charge` and `Disposition`

`Charge` is an integer cost with an optional human-readable label (e.g. `"pathfinding"`, `"ledger_request"`). Predefined charges for the standard XRPL operations live in `Fees.h`. Each call to `Consumer::charge()` accumulates cost in the entry's balance and returns one of three `Disposition` values: `ok` (continue), `warn` (throttle warning to the client), or `drop` (disconnect). The `warn` and `drop` transitions are also metered via `beast::insight` counters so operators can monitor rate-limiting activity through the existing stats pipeline.

## Design Rationale

The interface is deliberately narrow: callers create consumers and charge them; the manager decides what to do. This separation means the threshold tuning, decay algorithm, and gossip integration are entirely internal to `Logic` and can evolve without touching call sites. The choice to key inbound endpoints by IP alone (rather than `(IP, port)`) is intentional — client ephemeral ports change on reconnection, so port-based tracking would let a misbehaving host escape throttling simply by reconnecting.