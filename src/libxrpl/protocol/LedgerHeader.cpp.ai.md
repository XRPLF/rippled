# `LedgerHeader.cpp` — Ledger Header Serialization and Hash Calculation

`LedgerHeader.cpp` implements the four canonical operations on `LedgerHeader`, the plain-data struct that captures all metadata about a closed (or closing) XRP Ledger: binary serialization via `addRaw`, binary deserialization via `deserializeHeader` and `deserializePrefixedHeader`, and canonical hash calculation via `calculateLedgerHash`. Every path a ledger takes through the system — network propagation, node-store persistence, validation, and replay — passes through one or more of these functions.

## The `LedgerHeader` Struct

Defined in `LedgerHeader.h`, the struct carries the complete identity of a ledger:

- `seq` — the ledger index (a monotonically increasing `uint32`)
- `drops` — total XRP in existence, expressed in drops (`uint64`)
- `parentHash`, `txHash`, `accountHash` — 256-bit hashes identifying the parent ledger, the transaction Merkle root, and the account-state Merkle root respectively
- `parentCloseTime`, `closeTime` — `NetClock::time_point` values encoding seconds since XRPL epoch (1 January 2000, not Unix epoch)
- `closeTimeResolution` — a `NetClock::duration` specifying close-time rounding granularity, stored as a single byte (valid range 2–120 seconds)
- `closeFlags` — a byte-sized bitmask; bit `sLCF_NoConsensusTime` (0x01) signals that validators did not agree on close time
- `hash` — the ledger's own 256-bit identity, computed from all the above fields

The `validated` and `accepted` booleans are runtime state flags that are deliberately *not* part of the wire format and therefore do not appear in any of the serialization functions.

## `addRaw` — Canonical Serialization

```cpp
void addRaw(LedgerHeader const& info, Serializer& s, bool includeHash)
```

Appends the header fields to a `Serializer` in network byte order. The field order is fixed by protocol: `seq` (32-bit), `drops` (64-bit), then the three 256-bit hashes, then `parentCloseTime` and `closeTime` as 32-bit epoch counts, then the single-byte `closeTimeResolution` and `closeFlags`. The `hash` field is appended last only if `includeHash` is true.

Separating the hash from the body is a deliberate choice: `hash` is derived from the other fields, so including it in the authoritative serialization for hashing would be circular. However, when persisting a ledger to the node store or sending it over the wire, including the precomputed hash saves the receiver the cost of recomputing it. The boolean flag makes both use cases possible with a single function.

## `calculateLedgerHash` — Protocol-Defined Identity

```cpp
uint256 calculateLedgerHash(LedgerHeader const& info)
```

Computes the canonical ledger hash by feeding all header fields (except `hash` itself) into `sha512Half`, which returns the first 256 bits of a SHA-512 digest. The first input is `HashPrefix::ledgerMaster`, a four-byte constant `LWR\0`. This prefix namespaces the hash: even if two different object types happened to produce identical binary content, their hashes would differ because they use different `HashPrefix` values. All XRPL hash computations follow this pattern — `transactionID`, `txNode`, `leafNode`, `innerNode`, etc. each have their own prefix.

The comment in the source is notable: "This has to match addRaw in View.h." The field ordering and integer widths in `calculateLedgerHash` must exactly mirror those in `addRaw`. This is an unenforced invariant — the compiler does not prevent them from diverging. A mismatch would cause the node to compute hashes that don't match what the rest of the network expects, leading to consensus failures. The explicit cast to `std::uint32_t`, `std::uint64_t`, and `std::uint8_t` in `calculateLedgerHash` makes the wire types unambiguous and prevents accidental widening or narrowing from silently breaking the hash.

## `deserializeHeader` and `deserializePrefixedHeader`

```cpp
LedgerHeader deserializeHeader(Slice data, bool hasHash)
LedgerHeader deserializePrefixedHeader(Slice data, bool hasHash)
```

`deserializeHeader` constructs a `SerialIter` over the incoming `Slice` and reads fields in the exact order `addRaw` writes them. The time fields require explicit wrapping: `sit.get32()` yields a raw integer which must be stuffed into `NetClock::duration` and then into `NetClock::time_point` to restore the typed representation.

`deserializePrefixedHeader` is a thin wrapper: it advances the `Slice` by four bytes (`data + 4`) to skip a `HashPrefix` that was prepended during storage or transmission, then delegates to `deserializeHeader`. This variant is used in `InboundLedger.cpp` when decoding ledger data received over the peer-to-peer protocol, where the database node entry begins with a `HashPrefix` tag.

Neither deserialization function performs explicit size or range validation. `SerialIter` will throw if the buffer is exhausted, but no semantic checking of field values occurs — the callers (`Ledger.cpp`, `InboundLedger.cpp`) are responsible for verifying that the deserialized hash matches `calculateLedgerHash` before trusting the data.

## Callers and Integration Points

In `Ledger.cpp`, `calculateLedgerHash` is called in three places after constructing or modifying a `Ledger` object to seal its identity into `header_.hash`. In `InboundLedger.cpp`, `deserializeHeader` and `deserializePrefixedHeader` reconstruct ledger headers from network-received byte buffers when replaying or assembling a ledger from peer responses. The symmetry between these sites defines the full lifecycle of a ledger header: constructed in memory → hash computed → serialized → transmitted or stored → deserialized → hash verified.