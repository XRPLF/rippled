# `Validations.h` — Ledger Validation Tracking for XRPL Consensus

## Purpose and Role

`Validations.h` implements the core data structure that tracks validator attestations as they arrive over the network during XRPL's consensus process. Every time a trusted validator finalizes a ledger, it broadcasts a signed `Validation` message. This header defines the machinery that receives those messages, decides whether each one is fresh and legitimate, indexes them multiple ways for efficient queries, and feeds them into a `LedgerTrie` to determine which ledger chain the network as a whole prefers.

The file is entirely generic: the `Validations<Adaptor>` class template is parameterized so that simulation environments and the production rippled application can share the same logic while substituting test clocks, storage backends, or mutex types. The production instantiation wires it to `RCLValidationsAdaptor` in `RCLValidations.h`, which wraps `STValidation` objects and real XRP Ledger instances.

## `ValidationParms` — Protocol Timing Constants

`ValidationParms` holds the four timing thresholds that govern staleness and expiration. Rather than `static constexpr`, these are mutable instance members — an intentional trade-off documented in the source: simulation code needs to inject alternate values to stress-test edge cases without recompiling. The most important thresholds are `validationCURRENT_WALL` (5 min window around signing time), `validationCURRENT_LOCAL` (3 min window from first local observation), and `validationSET_EXPIRES` (10 min lifetime for per-ledger validation sets). `validationFRESHNESS` (20 s) is a separate concept used only in laggard detection — it identifies validators that are *online right now* versus just historically known.

## `isCurrent()` — Dual-Clock Staleness Check

The free function `isCurrent()` checks two independent time conditions simultaneously. The sign time (from the remote validator's clock) must fall within a window around the local network time, guarding against both future-dated and ancient validations. The seen time (when this node first received the validation, in local steady-clock time) provides an additional backstop for the extremely rare case where network time drifts badly. The implementation comment explains that arithmetic avoids overflow/underflow of unsigned 32-bit timestamps by promoting to signed 64-bit — important because `signTime` comes from untrusted external nodes.

## `SeqEnforcer<Seq>` — Monotonic Sequence Invariant

`SeqEnforcer` enforces that each validator's validations have strictly increasing ledger sequence numbers. Without this, a compromised or replayed message could appear to re-validate an old ledger. However, the enforcer deliberately resets its high-water mark after `validationSET_EXPIRES` has elapsed with no new validation from that node — so a validator returning after a long offline period can start fresh at whatever the current sequence is rather than being permanently locked out.

## `Validations<Adaptor>` — The Core Data Structure

### Storage Layout

The class maintains five coordinated data structures protected by a single `mutex_`:

- **`current_`** (`hash_map<NodeID, Validation>`): The most recent valid validation from each known node. This is the fast-path for quorum queries and is continuously pruned for staleness.
- **`byLedger_`** (aged unordered map, `ID → {NodeID → Validation}`): All validations grouped by ledger hash, with LRU-style time-based expiry. The `aged_unordered_map` container tracks the last access time per entry; `beast::expire()` removes entries untouched for longer than `validationSET_EXPIRES`.
- **`bySequence_`** (aged unordered map, `Seq → {NodeID → Validation}`): Validations grouped by sequence number, used exclusively for Byzantine detection. Allows the `add()` path to check whether a given sequence already has a conflicting validation from the same node.
- **`trie_`** (`LedgerTrie<Ledger>`): A compressed trie over ledger ancestry, keyed on sequence-indexed ancestor IDs. The trie drives the `getPreferred()` computation.
- **`acquiring_`** (`hash_map<{Seq,ID}, hash_set<NodeID>>`): A holding pen for trusted validations whose target ledger has not yet been locally acquired. When the ledger finally arrives, all waiting node IDs are atomically inserted into the trie.

### `add()` — The Critical Path

When a validation arrives, `add()` executes a sequence of escalating checks. First, the staleness guard runs before even acquiring the lock. Inside the lock, it looks up `bySequence_` to detect whether any prior validation from this node already exists for the same sequence number. If the sequence enforcer rejects the validation (non-monotonic), the code additionally inspects the stored entry to classify the violation:

- Same sequence, different ledger or sign time → `ValStatus::conflicting` (possible Byzantine validator)
- Same sequence and ledger, different cookie → `ValStatus::multiple` (likely misconfiguration, duplicate restart)
- Otherwise → `ValStatus::badSeq` (plain sequence regression)

Only after passing these gates does the validation enter `current_` and `byLedger_`. Trusted validations are routed to `updateTrie()`, which either immediately inserts the associated ledger into the trie (if the ledger is locally available) or parks the node in `acquiring_` to wait.

### Trie Management and `withTrie()`

Every trie query flows through `withTrie()`, which first calls `current()` to flush any stale entries from `current_` and update the trie accordingly, then calls `checkAcquired()` to promote any ledgers that have become locally available since the last query. This lazy-flush design keeps the trie accurate without requiring a separate background sweep.

`lastLedger_` tracks exactly which ledger each node currently contributes to the trie, enabling `removeTrie()` to efficiently undo a node's previous contribution before inserting its new one. This is how validation updates are atomic from the trie's perspective.

### `getPreferred()` — Preferred Ledger Selection

The main `getPreferred(Ledger const& curr)` overload follows a three-tier fallback. Normally it delegates to `trie_.getPreferred(localSeqEnforcer_.largest())`, which returns the tip of the heaviest weighted branch accounting for all trusted validators. If no trusted validations are trie-resident yet (typical at startup), it falls back to the `acquiring_` map, selecting the (Seq, ID) pair with the most validators waiting on it. If that is also empty, it returns `std::nullopt`, which causes the caller to use raw peer counts.

Once a preferred ledger is identified, `getPreferred()` applies a conservative "don't switch unnecessarily" heuristic: if the preferred ledger is the immediate child of the current working ledger, the node stays put (it may be about to generate that ledger itself). It only switches to an equal or earlier sequence if the ledgers are on genuinely different chains.

### UNL Changes and `trustChanged()`

When the Unique Node List changes at runtime, `trustChanged()` iterates both `current_` and the full `byLedger_` index to propagate the new trusted status. Newly trusted nodes have their current validations inserted into the trie; newly untrusted nodes are removed from the trie. This keeps the trie exclusively reflecting currently trusted validators, which is what quorum computation requires.

### Expiry Pinning with `setSeqToKeep()`

The `expire()` method normally lets `beast::expire()` evict aged entries from both indexes. The `setSeqToKeep()` mechanism provides an override: callers can designate a half-open range `[low, high)` of sequence numbers that must not be evicted. The `expire()` implementation "touches" all matching entries shortly before their natural expiration time, resetting their LRU timestamp. To avoid doing this work on every `expire()` call, a `refreshTime` static variable throttles the touch to once per near-expiry window — roughly `validationSET_EXPIRES - validationFRESHNESS` apart.

### Concurrency Model

All mutable state is protected by a single `Mutex` (defaulting to `std::mutex` in the production adaptor). Private helper methods receive a `std::lock_guard<Mutex> const&` parameter to document that the caller must hold the lock; no re-entrant locking occurs. The `adaptor_` instance is explicitly excluded from this lock — it manages its own synchronization. Public methods uniformly acquire the lock before delegating to private helpers, and the `withTrie()` helper is the only path into the trie to ensure the flush-then-query invariant is always maintained.