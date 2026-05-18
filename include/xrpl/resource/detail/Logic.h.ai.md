# `include/xrpl/resource/detail/Logic.h`

## Role in the Resource Management Subsystem

`Logic` is the central state machine of the XRPL resource-management subsystem. Its job is to track how much work each connected peer or RPC client has imposed on the server and decide whether to warn them or drop the connection. All of the meaningful logic in the `Resource` namespace — endpoint registration, balance tracking, gossip exchange, and periodic garbage collection — lives here. The `Manager` layer that external code interacts with delegates entirely to this class.

## Entry Lifecycle and the Intrusive List Design

Every peer or client is represented by an `Entry` object stored in `table_`, a hash map keyed by `Key` (a `{Kind, IP::Endpoint}` pair). The `Logic` class simultaneously maintains four mutually exclusive intrusive lists (`inbound_`, `outbound_`, `admin_`, `inactive_`) that categorize active versus dormant entries. The critical invariant — enforced throughout — is that an `Entry` can belong to at most one list at any moment. Moving an entry between lists always follows a strict remove-then-add sequence.

Entries are reference-counted by `Consumer` handles. When `refcount` drops to zero in `release()`, the entry is moved from its typed active list into `inactive_` and given a 300-second expiration timestamp (`secondsUntilExpiration`). When `periodicActivity()` runs, any inactive entry whose expiration has passed is fully erased from `table_`. If a new connection arrives for an endpoint that is currently inactive, the entry is rescued from `inactive_` and re-promoted to the appropriate active list before the refcount is incremented — the table_ entry is reused rather than discarded.

## Three Kinds of Endpoints and Their Key Normalization

`newInboundEndpoint()` normalizes the address with `at_port(0)`, stripping the ephemeral source port entirely. This means all connections from the same IP address — regardless of which port they originate from — share one `Entry`. This is correct for inbound connections: the limiting unit of concern is the remote IP, not the socket.

`newOutboundEndpoint()` uses the full address (host + port), which is appropriate since the node itself chose those outbound connections and they represent distinct peers.

`newUnlimitedEndpoint()` uses `at_port(1)` as an arbitrary sentinel. Unlimited (admin) endpoints are grouped together in `admin_` and bypass enforcement in `warn()` and `disconnect()` via the `isUnlimited()` guard. They still accumulate balances for observability.

## Balance and Disposition

Each `Entry` carries two balance components: `local_balance`, an exponentially decaying `DecayingSample` (32-second window), and `remote_balance`, a plain integer representing load reported by other cluster peers via gossip. The composite `balance()` is their sum.

The `charge()` method adds a fee to `local_balance` using the entry's `add()` method and then calls `disposition()` to classify the result:

- Below 5000 (`warningThreshold`): `Disposition::ok`
- 5000–24999: `Disposition::warn`
- 25000+ (`dropThreshold`): `Disposition::drop`

Charge severity also determines log verbosity: costs below 100 log at trace, 100–999 at debug, 1000–2999 at info, 3000+ at warn. This tiering lets operators distinguish casual query load from malformed or expensive requests without flooding logs.

`warn()` checks if the balance has crossed `warningThreshold` and, if so, applies `feeWarning` to penalize the consumer and records `lastWarningTime`. The time-equality guard (`elapsed != entry.lastWarningTime`) ensures only one warning is issued per clock tick, preventing alarm storms from tight loops.

`disconnect()` applies `feeDrop` on top of a balance already at or above `dropThreshold`. This is intentional: by inflating the balance at disconnect time, the system ensures that a reconnecting client must first decay down through the penalty before being treated normally again — a brief but effective reconnection backoff without any stateful timer.

## Gossip: Cross-Node Load Propagation

The gossip system allows a cluster of XRPL nodes to share load information about shared clients. `exportConsumers()` snapshots inbound entries with `local_balance >= minimumGossipBalance` (1000) and returns them as a `Gossip` value. Only entries above this threshold are exported to avoid propagating noise.

`importConsumers()` receives gossip from a named origin node and applies it to `remote_balance` fields of local entries. The design handles incremental updates cleanly: when gossip from an already-seen origin arrives, the method first constructs the new set of weighted entries (incrementing their `remote_balance`), then walks the *previous* import set and decrements its `remote_balance` contributions, and finally swaps the new set into place. This add-new-then-remove-old ordering means balances are never transiently under-reported, which matters for enforcement correctness.

Imported gossip data expires after 30 seconds (`gossipExpirationSeconds`). The `periodicActivity()` method handles expiration of both inactive entries and stale imports, reversing each expired import's remote balance adjustments before deleting it.

## Concurrency

The class uses a `std::recursive_mutex` rather than a plain `std::mutex`. The recursion is necessary because `warn()` and `disconnect()` both acquire `lock_` and then call `charge()`, which also acquires it. All public methods — including `acquire()`, `release()`, `getJson()`, `onWrite()`, and the gossip functions — take the lock, making `Logic` safe for concurrent use from multiple threads.

## Destructor Ordering

The destructor explicitly clears `importTable_` before `table_`. This order matters because `Import::Item` holds a `Consumer` value, and destroying a `Consumer` calls `Logic::release()`, which attempts to look up and modify an `Entry` in `table_`. If `table_` were destroyed first, those callbacks would access dangling memory. The ordered clear guarantees all import-held `Consumer` handles are properly released before their underlying `Entry` objects disappear.

## Observability

`onWrite()` serializes all four entry lists into a `beast::PropertyStream` for diagnostic inspection. `getJson()` (and its threshold-filtered overload) produces a JSON report of any endpoint whose composite balance meets a minimum, categorized by type. The `Stats` inner struct publishes `warn` and `drop` meters to the configured telemetry collector, giving operators a real-time view of enforcement activity.