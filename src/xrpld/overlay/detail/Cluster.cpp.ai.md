# `Cluster.cpp` — Trusted Peer Cluster Registry

## Role in the System

`Cluster.cpp` implements `xrpl::Cluster`, the in-process registry of trusted peer nodes that form an XRPL server cluster. A cluster is a group of `rippled` instances under common administrative control — typically a hub-and-spoke topology where each member is listed in the others' `[cluster_nodes]` configuration section. Being in the cluster grants a peer special trust: its messages are treated with higher credibility, its load-fee gossip is propagated, and it is permitted to behave differently than an untrusted stranger on the overlay.

`Application` owns a single `Cluster` instance (created in `Application.cpp` line 387), loads it from configuration at startup via `SECTION_CLUSTER_NODES` (`[cluster_nodes]`), and then makes it available to the rest of the codebase through `Application::cluster()`. The overlay layer checks cluster membership per-connection to decide how to handle incoming data.

## Data Structure and Comparator

The registry is stored as `std::set<ClusterNode, Comparator>`. `ClusterNode` is a simple value type carrying four fields: a `PublicKey` identity (const), a human-readable name, a `uint32_t` load fee, and a `NetClock::time_point` report time.

The inner `Comparator` struct uses `is_transparent = std::true_type` — the heterogeneous lookup extension. This lets the set's `find()`, `lower_bound()`, and similar methods accept a bare `PublicKey` without constructing a temporary `ClusterNode`. The three overloads of `operator()` handle every combination of `(ClusterNode, ClusterNode)`, `(ClusterNode, PublicKey)`, and `(PublicKey, ClusterNode)` comparisons, all delegating to `PublicKey::operator<`. This is the reason `member()` and `update()` can call `nodes_.find(identity)` directly with a `PublicKey` argument.

## `update()` — The Erase-Reinsert Pattern

`std::set` elements are immutable through iterators because the set's ordering invariant would break if a key could silently change. This creates a design constraint: to update a node's name, load fee, or report time, the implementation must erase the old node and insert a new one.

`update()` handles this carefully:

1. **Stale rejection**: if the incoming `reportTime` is not strictly greater than the stored one, the update is rejected immediately (`return false`). This guards against replayed or out-of-order gossip messages.
2. **Name persistence**: if the caller passes an empty name but an entry already exists, the prior name is retained (`name = iter->name()`). Once a human-readable label is assigned, it cannot be silently erased by receiving a nameless heartbeat.
3. **Erase-then-reinsert**: the iterator returned by `nodes_.erase(iter)` is passed as a hint to `nodes_.emplace_hint(iter, ...)`, allowing the set to place the new element near where the old one was — a minor performance optimisation since the key is unchanged.

When called from `load()` with the default `reportTime = NetClock::time_point{}` and no existing entry, the time check never fires and the node is simply inserted.

## `load()` — Config Parsing

`load()` is called once at startup with the `Section` parsed from the `[cluster_nodes]` block. Each line is expected to be a base58-encoded node public key, optionally followed by whitespace and a free-form comment (the node's name).

The regex used is deliberately lenient about whitespace around the key and comment but strict about what constitutes valid comment characters — only alphanumeric characters are accepted for the identity token (`[[:alnum:]]+`), while the optional comment accepts anything non-whitespace up to trailing whitespace. Lines containing unexpected punctuation immediately after the key (e.g. `nHxxx!Comment`) fail the regex and cause `load()` to return `false` immediately, aborting the entire load. This fail-fast behaviour means a partially-parsed section is never used: the test suite explicitly verifies that even nodes appearing after a bad entry are not registered.

The validation pipeline for each entry is:
1. `boost::regex_match` — rejects anything not matching the expected format
2. `parseBase58<PublicKey>(TokenType::NodePublic, ...)` — confirms the alphanumeric token is a valid base58 node public key
3. `member(*id)` — silently skips duplicates with a `warn()` log (does not abort)
4. `update(*id, trim_whitespace(match[2]))` — registers the node

The distinction between invalid format (hard failure, `return false`) and duplicate identity (soft warning, `continue`) reflects the difference between a configuration error that should block startup and an operator oversight that can be tolerated.

## Thread Safety

Every public method acquires `mutex_` via `std::lock_guard` before accessing `nodes_`. This makes all operations safe to call from any thread — important because overlay connections arrive on I/O threads while `load()` runs on the startup thread and `for_each()` may be called from RPC handlers or the cluster gossip handler.

The header comment on `for_each()` explicitly warns that `update()` must not be called from within its callback. The reason is straightforward: both hold the same non-recursive `std::mutex`. Calling `update()` from inside the `for_each` callback would attempt to re-lock `mutex_` on the same thread, causing a deadlock.

## Relationship to the Overlay

`Peer.h` declares `isClusterMember()` and overlay peers use `Cluster::member()` to populate that flag at connection time. The `for_each()` iterator is used by cluster-gossip handlers to broadcast node-state updates to all current cluster members, and by the `peers` RPC command to report cluster status. The load-fee and report-time fields on `ClusterNode` carry the gossip payload that allows each cluster member to know the current load across the whole group.