# `PeerReservationsList.cpp` — `peer_reservations_list` RPC Handler

This file implements `doPeerReservationsList`, the read-only member of the three-part peer reservation management family alongside `PeerReservationsAdd.cpp` and `PeerReservationsDel.cpp`. It handles the `peer_reservations_list` admin RPC command, returning a snapshot of all currently configured peer reservations as a JSON array.

## Role in the System

Peer reservations are a network-management feature that allows a node operator to guarantee connection slots for specific trusted peers, identified by their base58-encoded `NodePublic` keys. The three handlers — add, delete, and list — form the complete CRUD surface for this feature. The list handler exists so operators can inspect the current state of the table through the same RPC interface used to mutate it, rather than having to query the underlying SQLite database directly.

The handler is registered in `Handler.cpp` with `Role::ADMIN` and `NO_CONDITION`:

```cpp
{"peer_reservations_list", byRef(&doPeerReservationsList), Role::ADMIN, NO_CONDITION},
```

The `ADMIN` role means the handler is only accessible over the admin port (typically `127.0.0.1`), not the public-facing JSON-RPC or WebSocket interfaces. `NO_CONDITION` indicates no ledger state is required — the data lives entirely in an in-memory table backed by persistent storage, independent of ledger availability.

## Implementation

`doPeerReservationsList` accepts no input parameters. Unlike its siblings, it requires no validation pass because there is nothing to validate — the function is a pure enumeration of internal state. All three handlers follow the standard `RPC::JsonContext` contract and return a `Json::Value`, but only this one ignores `context.params` entirely.

The work is delegated in a single chain: `context.app.getPeerReservations().list()`. The `getPeerReservations()` call returns the `PeerReservationTable` singleton managed by the application, and `list()` performs a mutex-guarded copy of the internal `std::unordered_set<PeerReservation>`, then sorts the copy before returning it. This design is worth noting: the handler receives a stable, sorted `std::vector` snapshot rather than a live view of the concurrent data structure. Sorting on each `list()` call ensures deterministic output even though the underlying container is unordered, without requiring the table to maintain a sorted invariant during writes.

Serialization is pushed back to the data layer. Each `PeerReservation` object exposes a `toJson()` method that converts the internal `PublicKey nodeId` back to base58 via `toBase58(TokenType::NodePublic, nodeId)` and conditionally includes `jss::description` only when non-empty. The handler has no knowledge of what fields a reservation contains; it only iterates and appends:

```cpp
for (auto const& reservation : reservations)
    jaReservations.append(reservation.toJson());
```

The result is a JSON object with a single `"reservations"` array key (`jss::reservations`). An empty table produces `{"reservations": []}` rather than an error, consistent with how the `PeerReservationTable::load()` function always succeeds (returning `true`) even when no rows exist in the database.

## Design Observations

The handler is intentionally thin. Compared to `doPeerReservationsAdd`, which validates field presence and type, parses base58, and returns the previous reservation if overwritten, the list handler has nothing comparable to do. This asymmetry is natural: write operations must guard against malformed input and return diagnostic information, while a read on internal state can assume correctness. The pattern across all three handlers — delegate to `getPeerReservations()`, serialize with `toJson()` — keeps the RPC layer free of business logic.