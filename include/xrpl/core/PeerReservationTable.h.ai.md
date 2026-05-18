# `include/xrpl/core/PeerReservationTable.h`

## Purpose

The XRPL peer overlay has a bounded slot pool — a node only maintains connections with a limited number of peers. `PeerReservationTable` exists to guarantee that specific, operator-designated peers can always connect even when that pool is saturated. A reserved peer bypasses the normal acceptance policy and is granted a connection unconditionally.

This header defines two public types — the `PeerReservation` value type and the `PeerReservationTable` container — along with the `KeyEqual` comparator helper that bridges them.

## `PeerReservation` — Value Type Design

A reservation is simply a `PublicKey nodeId` paired with an optional human-readable `description`. The design decision that drives everything else is that **identity is the public key alone**: the `hash_append` friend delegates only to `nodeId`, and `KeyEqual` compares only `nodeId`. The `description` field is pure metadata and plays no role in lookup or equality.

`operator<` provides a total ordering over `nodeId`, used exclusively by `list()` to return reservations in a deterministic, sorted order for display or API responses. It is not involved in the hash-based storage.

## `KeyEqual` — Bridging the `unordered_set` Gap

The standard library's `std::unordered_set` is parameterized on a full equality predicate, not a projection. Because `PeerReservation` is not equality-comparable by default on its full value (description could differ for the same node), `KeyEqual` provides a minimal comparator that compares only `nodeId`. A companion comment marks a C++20 improvement opportunity: heterogeneous lookup with "equivalence" would allow passing a bare `PublicKey` to `find()` without constructing a throwaway `PeerReservation{nodeId}`. Until then, callers use the workaround of brace-initializing a partial struct.

## `PeerReservationTable` — Thread-Safe, DB-Backed Cache

The table stores reservations as an `std::unordered_set<PeerReservation, beast::uhash<>, KeyEqual>` — an in-memory cache that is loaded from and persisted to a SQLite database via `DatabaseCon`. The `mutex_` and `journal_` members are both `mutable` so that `const`-qualified operations (`list()`, `contains()`) can still acquire the lock and log without requiring a non-const object.

**Two-phase initialization** mirrors `ApplicationImp`'s own setup lifecycle: the constructor only accepts a `beast::Journal` and initializes a null `connection_` pointer; the actual database connection is not available until `ApplicationImp::setup()` runs, at which point `load(DatabaseCon&)` is called. This is acknowledged in a comment as a forced dependency: `load()` stores the `DatabaseCon*` for later mutations and bulk-reads the persisted reservation table via `getPeerReservationTable()` (defined in `src/libxrpl/server/Wallet.cpp`). The `load()` return type is `bool` to fit the error-handling convention of `ApplicationImp::setup`, though it unconditionally returns `true` — an empty table is always a valid starting state.

## Core Operations

`contains(PublicKey const&)` is the hot path. It is called by `OverlayImpl` during peer activation to decide whether an incoming connection should receive a reserved slot:

```cpp
bool const reserved =
    static_cast<bool>(app_.getCluster().member(publicKey)) ||
    app_.getPeerReservations().contains(publicKey);
auto const result = m_peerFinder->activate(slot, publicKey, reserved);
```

A reserved peer — whether by cluster membership or an explicit reservation — passes the `reserved` flag to `PeerFinder::activate`, which allows the connection to proceed past the normal slot limit. This is the reason the table needs to be in-memory rather than querying the DB on each incoming connection.

`insert_or_assign(PeerReservation const&)` exposes a conceptual upsert, but `std::unordered_set` provides no native implementation. Since set elements are logically immutable (mutating them would break hash-bucket placement), updating a reservation requires erase-then-insert. The implementation is deliberate about iterator safety: it increments the found iterator before erasing — rather than decrementing — because decrement is illegal if the element is at `begin()`, while increment to `end()` is always valid and still serves as a useful insertion hint. The method returns the previous `PeerReservation` if one existed, giving callers confirmation that an update (rather than a fresh insert) occurred. The database layer uses an SQL upsert (`ON CONFLICT ... DO UPDATE`) for idempotency, so the in-memory erase-then-insert and the DB operation stay in sync without separate code paths.

`erase(PublicKey const&)` follows the same return convention: it returns the erased reservation wrapped in `std::optional`, or `std::nullopt` if the key was absent. Database removal via `deletePeerReservation()` only happens if the key was found in memory, avoiding unnecessary DB round-trips.

`list()` copies the set under lock, releases the lock, then sorts the copy using `operator<` before returning. Sorting outside the lock keeps the critical section short and avoids holding the lock across a potentially non-trivial `std::sort`. The result is a stable, ordered snapshot suitable for RPC responses.

## Relationships

- **`ApplicationImp`** owns the singleton `PeerReservationTable` as `peerReservations_`, constructs it with a dedicated `Journal`, calls `load()` during `setup()`, and exposes it via `getPeerReservations()`.
- **`OverlayImpl`** is the only consumer of `contains()`, querying it on every inbound peer handshake.
- **`Wallet.cpp`** (`src/libxrpl/server/`) implements the three free functions (`getPeerReservationTable`, `insertPeerReservation`, `deletePeerReservation`) that bridge SOCI/SQLite — the separation is flagged in a comment as an unfortunate constraint of where the `CREATE TABLE` schema lives.
- **`DatabaseCon`** provides the `checkoutDb()` mechanism for scoped SOCI session access, following the same checkout pattern used elsewhere in rippled's database layer.