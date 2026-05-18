# `include/xrpl/protocol/LedgerHeader.h`

## Role in the System

`LedgerHeader` is the compact, canonical summary of a single ledger — the XRPL equivalent of a blockchain block header. Every ledger that passes through the system, whether open (still accumulating transactions), closed (transaction set determined), or validated (confirmed by quorum), is identified and authenticated through this structure. The header encapsulates the cryptographic commitments (hashes) that link the ledger chain together, the timing metadata produced by consensus, and the lifecycle flags that track each ledger's progression through the validation pipeline.

This file pairs with `src/libxrpl/protocol/LedgerHeader.cpp`, which provides the serialization, deserialization, and hash-calculation implementations.

## The `LedgerHeader` Struct

The fields divide naturally into two groups, and the source comments make this partitioning explicit.

**Fields valid for all ledgers** (including open, in-progress ledgers):
- `seq` (`LedgerIndex`, a `uint32_t`) — the ledger's sequence number, the monotonically increasing chain counter.
- `parentCloseTime` — the close time of the prior ledger, expressed as a `NetClock::time_point`. `NetClock` is a custom clock whose epoch is January 1, 2000 (offset 946684800 seconds from Unix epoch), and its `duration` is integer seconds stored as `uint32_t`. This 32-bit second counter fits comfortably within four bytes on the wire.

**Fields valid only for closed ledgers** (where the transaction set has been finalized):
- `hash`, `txHash`, `accountHash`, `parentHash` — all `uint256`. The hash chain is the backbone of ledger integrity: `parentHash` links this ledger to its predecessor; `txHash` and `accountHash` commit to the SHAMap roots of the transaction set and the account state tree, respectively; `hash` is the ledger's own identity, computed by `calculateLedgerHash`.
- `drops` (`XRPAmount`) — the total XRP in existence at this ledger, in drops (1 XRP = 1,000,000 drops).
- `closeTime`, `closeTimeResolution`, `closeFlags` — consensus-produced timing metadata (see below).

The `validated` flag is declared `mutable`, which is an acknowledged design wart (the comment reads "VFALCO TODO Make this not mutable"). It's mutable because `LedgerHeader` is frequently embedded in `const`-qualified objects, yet the validation state — which transitions one-way from `false` to `true` and never reverts — must remain updatable after the fact. The `accepted` flag tracks a distinct local concept: whether this node has accepted the ledger's transaction set, independent of the network-wide validation quorum.

## Consensus Close-Time Flags

`sLCF_NoConsensusTime` (value `0x01`) is the sole defined close-flag bit. When the consensus round cannot agree on a close time, this bit is set and `getCloseAgree()` returns `false`. In practice, this happens when the validator set is small or when significant clock skew exists across validators. The application-level code in `Ledger.cpp` writes `header_.closeFlags = correctCloseTime ? 0 : sLCF_NoConsensusTime` after each consensus round, and `LedgerToJson.cpp` propagates this to RPC responses as `close_flags`. Code that needs a reliable close time (such as the ledger replay subsystem) guards against this flag explicitly.

The field is declared as `int` in the struct but serialized as a single `uint8_t` on the wire, meaning only the low 8 bits are meaningful; the current protocol has room for seven more close-time flags without changing the wire format.

## Wire Format and Serialization

`addRaw()` writes the header in a fixed, canonical byte layout:

| Field | Size | Notes |
|---|---|---|
| `seq` | 4 bytes | uint32 big-endian |
| `drops` | 8 bytes | uint64, in drops |
| `parentHash` | 32 bytes | raw bytes |
| `txHash` | 32 bytes | raw bytes |
| `accountHash` | 32 bytes | raw bytes |
| `parentCloseTime` | 4 bytes | seconds since epoch 2000-01-01 |
| `closeTime` | 4 bytes | seconds since epoch 2000-01-01 |
| `closeTimeResolution` | 1 byte | uint8, range 2–120 s |
| `closeFlags` | 1 byte | uint8 |

Total: 118 bytes without hash, 150 with. The `includeHash` parameter appends the ledger's own `hash` field — used when storing headers to the database so that the hash can be read back without recomputation, but **not** used in the hash-calculation path itself (doing so would be circular).

`deserializePrefixedHeader()` is a thin wrapper that simply advances the data pointer by 4 bytes before calling `deserializeHeader()`. Network messages prepend a 4-byte type discriminant before the raw header, so this variant handles peer-wire parsing while keeping the core deserialization path clean.

## Hash Calculation

`calculateLedgerHash()` produces the ledger's canonical identity hash using `sha512Half`, which computes SHA-512 over the input and returns the first 256 bits. The hash is domain-separated by prepending `HashPrefix::ledgerMaster` (the ASCII bytes `'L'`, `'W'`, `'R'` followed by a zero byte), preventing collisions with hashes computed over other XRPL object types (transactions, account state nodes, etc.) that may share binary structure.

The hash input mirrors `addRaw` precisely — this invariant is even called out in the source comment "VFALCO This has to match addRaw in View.h." The notable design difference is that `calculateLedgerHash` feeds fields directly into `sha512Half`'s variadic template rather than going through the `Serializer` class. Both paths must produce identical byte sequences, and any divergence would silently break hash verification across the network. The hash does **not** include `validated`, `accepted`, `closeTime` for open ledgers, or the hash itself — only the consensus-finalized fields.

## Relationship to Ledger Views

`LedgerHeader` is embedded inside the `ReadView`/`ApplyView` hierarchy that provides access to ledger state. The header fields are accessible as `view.info()`, returning a `const LedgerHeader&`. The separation of the header struct from the view interface means metadata can be cheaply copied, compared, and transmitted without dragging in the full account-state SHAMap. This is exploited heavily in the overlay layer (peer synchronization), the database persistence layer, and RPC handlers that need to report ledger metadata without loading the full ledger object.