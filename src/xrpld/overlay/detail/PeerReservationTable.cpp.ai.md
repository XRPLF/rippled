# `PeerReservationTable.cpp` — Persistent Peer Slot Registry

This file implements `PeerReservationTable` and the serialization method for `PeerReservation`, providing the XRPL overlay with a managed allowlist of nodes that are guaranteed a connection slot. When the overlay has reached its maximum peer count it will still accept inbound connections from reserved nodes, making this mechanism essential for validators that must maintain connectivity to trusted peers regardless of network load.

## Structure and Lifecycle

`PeerReservation` is a plain value type carrying a `PublicKey nodeId` and an optional human-readable `description`. Its identity for equality and hashing purposes is entirely determined by `nodeId` — the companion `KeyEqual` functor and the `hash_append` overload in the header both ignore `description`. This means two `PeerReservation` objects with the same public key but different descriptions are considered the same reservation, which is the intended semantic: a node either has a reserved slot or it doesn't.

`PeerReservationTable` undergoes two-phase construction, a pattern forced on it by `ApplicationImp::setup`. The object is created first (with an optional `Journal`), and the database connection is only wired up later via `load()`. The class stores a raw `DatabaseCon*` pointer rather than owning the resource — `connection_` is a non-owning observer that remains valid for the lifetime of the application. All post-`load` mutations (`insert_or_assign`, `erase`) call `connection_->checkoutDb()` directly on this stored pointer, so `load()` must always be called before either mutation method.

## The `load()` Non-Failure Contract

`load()` has an intentionally lenient error contract: it always returns `true`. The comment explains this is to fit the error-handling convention of `ApplicationImp::setup`, where `false` signals a fatal startup failure. Because the application can always start with an empty reservation table and reconcile state later, a database read failure during startup is treated as yielding zero reservations rather than aborting the process. This is a deliberate resilience decision, not an oversight.

## Concurrency Model

All four public methods lock `mutex_` via `std::lock_guard`, making the table safe for concurrent use by the overlay (which checks `contains()` on inbound connections) and the RPC handlers (which call `insert_or_assign` and `erase`). The `list()` method minimizes lock hold time with a deliberate scope split: it copies the set into a `std::vector` while holding the lock, then releases the lock before calling `std::sort`. Sorting a snapshot outside the lock is correct because `list()` is used only for informational RPC responses, where a momentary stale view is acceptable.

## The `insert_or_assign` Workaround

`std::unordered_set` has no `insert_or_assign` method, and its iterator-based API makes an efficient in-place update impossible without a `find`+`erase`+`insert` sequence. The code acknowledges this explicitly with a reference to a Stack Overflow discussion of the container design limitation. The implementation saves the hint iterator's successor before erasing (since the found position becomes invalid after erase), then passes that hint to `insert`. In practice this is inconsequential because the reservation table is tiny and mutations are infrequent admin operations, but the code is careful to do it correctly anyway.

The method returns a `std::optional<PeerReservation>` containing the displaced reservation if one existed. This enables RPC handlers to report the previous state in their response without a separate lookup.

## Database Persistence Layer

All persistence is delegated to three free functions in `src/libxrpl/server/Wallet.cpp`: `getPeerReservationTable`, `insertPeerReservation`, and `deletePeerReservation`. These functions use SOCI to interact with a `PeerReservations` SQL table (defined in `include/xrpl/rdb/DBInit.h`). `insertPeerReservation` uses an upsert idiom (`INSERT ... ON CONFLICT ... DO UPDATE SET Description=excluded.Description`) so it naturally handles both new insertions and description updates without needing separate SQL for each case. The in-memory erase-before-insert in `insert_or_assign` mirrors this upsert semantic at the application level.

`erase()` is correctly defensive: it only issues the `DELETE` SQL if the key was found in the in-memory set, avoiding a no-op database round-trip for unknown node IDs. Like `insert_or_assign`, it returns the removed reservation wrapped in `std::optional`, again to support informative RPC responses.

## JSON Serialization

`PeerReservation::toJson()` encodes `nodeId` as a Base58-encoded node public key string under `jss::node`. The `description` field is omitted entirely when empty, keeping the JSON compact. The encoding via `toBase58(TokenType::NodePublic, ...)` is the canonical on-wire representation of node identities throughout XRPL — the same format used in configuration files and peer protocol messages.