# `PeerReservationsAdd.cpp` — `peer_reservations_add` RPC Handler

## Role in the System

This file implements `doPeerReservationsAdd`, the admin RPC handler that backs the `peer_reservations_add` command. Peer reservations give a specific XRPL node a guaranteed connection slot in the overlay network: when a peer's public key has a reservation, the overlay will hold a slot open for it even when the general inbound connection pool is full. This handler is the write path — it inserts or replaces a reservation in the live `PeerReservationTable` and persists the change to the relational database.

The file lives under `src/xrpld/rpc/handlers/admin/peer/`, alongside `PeerReservationsDel.cpp` and `PeerReservationsList.cpp`, which form the complete CRUD surface for peer reservations. All three are admin-only commands.

## Validation Pipeline

`doPeerReservationsAdd` validates its two inputs — `public_key` (required) and `description` (optional) — through a strict, layered sequence before touching any application state:

1. **Presence check** — `params.isMember(jss::public_key)` returns `missing_field_error` if the key is absent.
2. **Type check** — `params[jss::public_key].isString()` returns `expected_field_error` if the JSON value is not a string.
3. **Cryptographic parse** — `parseBase58<PublicKey>(TokenType::NodePublic, ...)` attempts to decode the string as a NodePublic-type base58 key. Failure returns `rpcPUBLIC_MALFORMED`.

A notable comment embedded in the source explicitly argues against pulling field extraction into a helper that returns `Json::Value` — because copying whole JSON objects just to propagate an error code is expensive and clutters the calling code. The comment acknowledges that exceptions would be cleaner for control flow but notes their runtime cost for error paths, and calls out that an error monad (essentially a typed `std::optional`) would be the right abstraction for this recurring pattern. The code doesn't implement that monad yet, but the comment serves as a design marker for future refactoring.

A second comment explains why only base58 encoding is accepted rather than hex (even though `channel_verify` accepts both): per an explicit design preference, node key input is standardized to base58. This eliminates an entire class of ambiguous-format bugs at the cost of one decoder option.

## Upsert Semantics and Response Shape

Once the `PublicKey` is parsed, the handler calls `context.app.getPeerReservations().insert_or_assign(PeerReservation{nodeId, desc})`. This is an upsert: if no reservation exists for that node, one is created; if one already exists, it is replaced. Either way, the `PeerReservationTable` holds a mutex-protected `std::unordered_set` keyed by `nodeId`, and the underlying SQL store is updated atomically within the same lock.

The return value from `insert_or_assign` is `std::optional<PeerReservation>` — the previous reservation if one was displaced, or `std::nullopt` for a fresh insert. The handler surfaces this directly in the JSON response under `jss::previous`, serialized via `PeerReservation::toJson()` which emits the node key in base58 and the description (omitting the description field entirely if it was empty). A fresh insert returns an empty JSON object `{}`.

This pattern — returning the displaced value — is shared symmetrically with `doPeerReservationsDel`, which also returns a `jss::previous` field on successful deletion, providing callers idempotent-safe confirmation of what changed.

## Relationship to `PeerReservationTable`

`PeerReservationTable` (defined in `include/xrpl/core/PeerReservationTable.h`, implemented in `src/xrpld/overlay/detail/PeerReservationTable.cpp`) is a thread-safe in-memory registry backed by a SQL table. Its `insert_or_assign` method intentionally uses a remove-then-reinsert approach rather than in-place mutation because `std::unordered_set` keys are immutable — a recognized limitation documented in the source with a Stack Overflow reference. The comment notes this is acceptable because reservations are small, the table is expected to be short, and the method is rarely called. The handler itself has no concurrency concerns of its own — all thread safety is encapsulated within the table.