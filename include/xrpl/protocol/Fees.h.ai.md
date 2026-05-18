# `include/xrpl/protocol/Fees.h`

## Purpose

`Fees.h` defines the `Fees` struct — a lightweight, value-semantic snapshot of a ledger's fee schedule. Every ledger in the XRP Ledger carries three economically significant parameters: the minimum cost of a transaction, the base account reserve, and the per-object reserve increment. This header packages those three values together so that transaction processing code can query them in a consistent, type-safe way through the `ReadView::fees()` interface.

The design reflects a fundamental ledger invariant: **fees are constant within a ledger**. Validators may vote to change fee parameters, but any change only takes effect at the next ledger boundary. The struct models this by being a plain aggregate with no mutation methods. Code that processes transactions simply captures the current `Fees const&` at the start of ledger application and uses it throughout.

## `FEE_UNITS_DEPRECATED`

```cpp
inline constexpr std::uint32_t FEE_UNITS_DEPRECATED = 10;
```

Before the `XRPFees` amendment, the XRPL expressed transaction costs in abstract "fee units" rather than raw drops. A reference transaction cost 10 fee units, and the actual drop cost was computed by multiplying fee units by a scaling factor stored on the ledger. After the amendment, fees are expressed natively in drops using `XRPAmount`. The constant `FEE_UNITS_DEPRECATED = 10` survives as a compatibility shim — it is inserted into JSON subscription messages and validation objects when code detects that `featureXRPFees` is not enabled on the active ledger, preserving the legacy `fee_ref` field consumed by older clients and tools.

## `Fees` struct

```cpp
struct Fees {
    XRPAmount base{0};
    XRPAmount reserve{0};
    XRPAmount increment{0};
};
```

All three fields are `XRPAmount` — a strongly typed `int64_t` drop count defined in `XRPAmount.h`. Using a distinct type over a raw integer prevents accidental mixing of drop amounts with dimensionless values, and enables the arithmetic needed in `accountReserve()` to remain type-safe. Fields are zero-initialized by default, which is useful for constructing a placeholder or an empty fee schedule in tests.

**`base`** is the minimum fee for a reference transaction in drops. Transactions paying less than this value are rejected.

**`reserve`** is the base account reserve — the minimum XRP balance every account on the ledger must hold simply to exist. An account whose balance falls below its total reserve becomes "reserve-deficient" and cannot send payments.

**`increment`** is the additional reserve required for each "owned" ledger object such as trust lines, offers, escrows, or NFT tokens. This creates a per-object cost that scales with the account's footprint on the shared ledger state.

## `accountReserve()`

```cpp
XRPAmount accountReserve(std::size_t ownerCount) const {
    return reserve + ownerCount * increment;
}
```

This is the only non-trivial behavior in the file. It computes the total XRP an account must hold given its current `ownerCount` — the number of ledger objects it owns. The formula `reserve + ownerCount * increment` is applied ubiquitously across transactors before creating new ledger objects. Examples from the codebase:

- `OfferCreate` calls `sb.fees().accountReserve(sfOwnerCount + 1)` before booking a new offer, checking that the account can afford the incremental reserve.
- `TrustSet` uses it to gate creation of a new trust line.
- `DIDSet`, `MPTokenIssuanceCreate`, and `VaultCreate` each guard new object creation with the same pattern.

The `+ 1` idiom — passing `ownerCount + 1` rather than the current count — is deliberate: it checks whether the account will be able to afford the new object *after* its `ownerCount` increases, not just whether it currently meets the existing reserve.

The multiplication `ownerCount * increment` relies on `XRPAmount::operator*(value_type)`, which returns a new `XRPAmount`. The subsequent addition uses `XRPAmount::operator+=(XRPAmount const&)`. Both are `constexpr`-friendly and overflow-silent at the type level; correctness depends on `ownerCount` being bounded by protocol limits (currently capped at 4,294,967,295 by the `uint32` on-ledger field).

## Relationship to the rest of the system

`Fees` enters the transaction processing path through `ReadView`, the abstract base class for all ledger views. `ReadView::fees()` returns a `Fees const&` so that any code holding a view — transactors, preflight checks, RPC handlers — can query fee parameters without knowing whether the view is a closed ledger, an open ledger, or a sandbox. The fee schedule itself is loaded from the `FeeSettings` SLE on each ledger by the view implementation.

Fee *changes* are handled separately by `FeeVoteImpl`, which runs during the consensus round and embeds the validator's desired `base`, `reserve`, and `increment` values into validation messages. When a supermajority of validators agree on a new fee schedule, the change is applied as a pseudo-transaction that updates the `FeeSettings` SLE — after which the next ledger's `ReadView::fees()` will return the updated `Fees` snapshot.