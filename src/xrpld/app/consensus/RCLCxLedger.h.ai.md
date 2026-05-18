# RCLCxLedger.h

## Role in the System

`RCLCxLedger` is the ledger-type adapter that bridges the XRPL's concrete `Ledger` class into the generic, policy-based `Consensus<Adaptor>` engine. The consensus engine in `src/xrpld/consensus/Consensus.h` is intentionally ledger-agnostic — it operates against a user-supplied `Adaptor` type, which in turn declares a `Ledger_t` alias. For the live XRP network (the RCL — Ripple Consensus Ledger), that alias is `RCLCxLedger`:

```cpp
// In RCLConsensus::Adaptor:
using Ledger_t = RCLCxLedger;
```

The file's sole job is to present just enough of `Ledger const`'s surface area to satisfy the interface contract the generic engine expects, while keeping the full-fledged ledger object reachable through the public `ledger_` member.

## Design: Thin Wrapper over Shared Ownership

`RCLCxLedger` owns a `std::shared_ptr<Ledger const>` rather than a value or a raw pointer. This is consistent with the rest of the XRPL codebase, where ledgers are large, immutable objects shared across many subsystems simultaneously. The wrapper adds no heap allocation of its own — copying an `RCLCxLedger` merely increments the reference count on the underlying ledger.

The public `ledger_` member is an explicit design choice (and acknowledged departure from ideal encapsulation). When `RCLConsensus::Adaptor` needs to build a *new* ledger by applying a transaction set to the previous one, it must pass the concrete `Ledger const*` to the application layer. Exposing `ledger_` directly avoids a proliferation of accessor overloads while keeping the wrapper simple. The accompanying TODO note flags the long-term intent to replace this with `shared_ptr<ReadView const>` — which would enforce read-only access at the type level — but that would require a mechanism to construct a new ledger from a `ReadView`, which doesn't yet exist.

## Interface Contract Satisfied

The generic `Consensus` template calls the following methods on its `Ledger_t`:

- `id()` → `LedgerHash` — used as the primary key when tracking which ledger a round is building on.
- `seq()` → `LedgerIndex` — used to compute the next sequence number (`previousLedger_.seq() + Seq{1}`).
- `parentID()` → `LedgerHash` — for tracing ledger ancestry.
- `closeTimeResolution()` → `NetClock::duration` — the consensus engine uses this to determine acceptable close-time windows for a new ledger; validators only agree if their proposed close times fall within the same resolution bucket.
- `closeAgree()` → `bool` — delegates to `xrpl::getCloseAgree(header)`, which decodes a flag in the ledger header indicating whether consensus validators agreed on the close time or were forced to accept a default.
- `parentCloseTime()` → `NetClock::time_point` — used in timing calculations for whether to close the current open ledger.

These accessors are all one-liners that forward directly into `ledger_->header()` — there is no caching or computation here, by design. The `Ledger` object itself caches its parsed header, so repeated calls are cheap.

## Sibling Types

`RCLCxLedger` is one of three RCL-specific consensus wrappers defined in this directory:

| Wrapper | Wraps | Adaptor alias |
|---|---|---|
| `RCLCxLedger` | `shared_ptr<Ledger const>` | `Ledger_t` |
| `RCLCxTx` / `RCLTxSet` | `SHAMapItem` / `SHAMap` | `TxSet_t` |
| `RCLCxPeerPos` | Peer proposal message | `PeerPosition_t` |

Together they form the complete set of concrete types that parameterize `Consensus<RCLConsensus::Adaptor>`. The pattern is uniform: each wrapper defines an `ID` type alias and an `id()` accessor, giving the generic engine a consistent way to identify and compare ledgers, transaction sets, and peer proposals without knowing their internal structure.

## Known Design Debt

Two TODO comments in the file call out intentional shortcuts:

1. **Default constructor**: `RCLCxLedger() = default` leaves `ledger_` as a null `shared_ptr`. The comment notes this should be eliminated by guaranteeing `RCLConsensus` is always initialized with a valid ledger, but no callers yet enforce that invariant, and the adaptor methods do not null-check before dereferencing.

2. **`ledger_` type**: Ideally this would be `shared_ptr<ReadView const>` to express that consensus never modifies a ledger after creation. The concrete `Ledger` type is needed only when building the *next* ledger; using `ReadView` everywhere else would make the immutability contract explicit at the type system level.

## `getJson()`

The `getJson()` method delegates to `xrpl::getJson({*ledger_, {}})` (from `LedgerToJson.h`), producing a full JSON representation of the ledger. This is used by the consensus engine for diagnostic logging when a node detects it is working on the wrong ledger (`ConsensusMode::wrongLedger`), making it straightforward to dump the complete state of the ledger in question to the journal or RPC output without any extra serialization logic in `RCLCxLedger` itself.