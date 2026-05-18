# `LedgerStateFix.cpp` — Surgical Ledger State Repair Transactor

`LedgerStateFix` is a privileged maintenance transactor introduced under the `fixNFTokenPageLinks` amendment (transaction type `ttLEDGER_STATE_FIX`, code 53). Its purpose is narrow but important: it provides a sanctioned on-chain mechanism for correcting corrupted or inconsistent ledger state that cannot be self-healed through normal transaction execution. Rather than introducing a bespoke transaction type for each class of corruption, the design embeds a `sfLedgerFixType` discriminant in the transaction, making the transactor an extensible dispatch table for future repair operations.

## Transaction Structure and Dispatch

The transactor's class hierarchy inherits from `Transactor` in the standard way, with the `FixType` enum defined directly on the class:

```cpp
enum FixType : std::uint16_t {
    nfTokenPageLink = 1,
};
```

Currently only one variant exists. Every method — `preflight`, `preclaim`, and `doApply` — switches or branches on `sfLedgerFixType`. This architecture means adding a new fix type requires only a new `FixType` constant and a matching case in each phase; no structural changes to the transactor are needed.

## Three-Phase Validation and the LCOV Guards

The standard XRPL transactor lifecycle runs `preflight` (pure validation against the transaction itself), `preclaim` (read-only ledger inspection), then `doApply` (ledger mutation). `LedgerStateFix` uses all three phases defensively:

**`preflight`** validates the `sfLedgerFixType` via a `switch` statement. An unrecognized type returns `tefINVALID_LEDGER_FIX_TYPE` immediately — the `tef` prefix means the transaction is rejected before even entering the engine queue. For `nfTokenPageLink`, it additionally confirms that `sfOwner` is present via `isFieldPresent`; without a target account the repair has no meaning.

**`preclaim`** uses the read-only `view` to confirm that the account identified by `sfOwner` actually exists (`keylet::account(owner)`). If the account is absent from the ledger, it returns `tecOBJECT_NOT_FOUND` and the transaction fails without applying.

**`doApply`** delegates to `nft::repairNFTokenDirectoryLinks`, interpreting a `false` return as `tecFAILED_PROCESSING`.

Both `preclaim` and `doApply` include unreachable `return tecINTERNAL` paths annotated `// LCOV_EXCL_LINE`. These exist because the compiler cannot see that `preflight` guarantees only valid `FixType` values reach these methods. The annotation signals to coverage tooling that these lines are intentionally excluded from metrics — they are defensive code, not dead logic.

## Fee Model: Owner Reserve, Not Base Fee

`calculateBaseFee()` does not return the network's reference fee. Instead, it forwards to `calculateOwnerReserveFee()`:

```cpp
return calculateOwnerReserveFee(view, tx);
```

This is the same pricing strategy used by `AccountDelete` and `AMMCreate`. The owner reserve fee (one reserve increment) is orders of magnitude larger than the standard base fee. The design rationale is economic deterrence: a repair transaction that finds nothing to fix still costs the submitter a full reserve increment, so there is no incentive to probe the ledger speculatively. The fee is non-refundable regardless of whether `repairNFTokenDirectoryLinks` makes any changes. This cost also signals intentionality — operators submit this transaction only when they have strong reason to believe an account's NFToken directory is corrupt.

## What `repairNFTokenDirectoryLinks` Actually Fixes

The underlying repair function in `NFTokenHelpers.cpp` traverses the doubly-linked list of `NFTokenPage` ledger objects for the given account. NFToken pages are keyed by a canonical range marker derived from the highest token ID they contain; a known bug could produce pages whose `sfPreviousPageMin` or `sfNextPageMin` links were incorrect or whose final page carried the wrong key.

The repair walks from the first page to the last, fixing three categories of corruption:

1. **Stale back-pointer on the first page** — the head of the list must not have a `sfPreviousPageMin`; if it does, that field is removed.
2. **Broken forward/backward links between adjacent pages** — each pair of consecutive pages has their `sfNextPageMin` and `sfPreviousPageMin` set to point at each other, correcting any mismatch.
3. **Miskeyed final page** — if the actual last page in the directory does not carry the canonical `nftpage_max(owner)` key (the maximum possible key for the account's range), the function allocates a new SLE at the correct key, copies the token array and prev-link into it, patches the preceding page's forward pointer, erases the old page, and inserts the new one.

The function returns `true` if any repair was performed and `false` otherwise. A `false` return does not indicate an error from the function's perspective, but `doApply` maps it to `tecFAILED_PROCESSING` — meaning the submitter pays the owner reserve fee while achieving no change to the ledger. This reflects the fact that submitting a fix for an account whose directory is already consistent is not an internal error, but is nonetheless a failed operation from the transaction's point of view.

## Relationship to the Amendment System

The auto-generated `protocol_autogen/transactions/LedgerStateFix.h` records that the transaction is gated on the `fixNFTokenPageLinks` amendment. Until that amendment activates on a network, nodes will reject the transaction type entirely at the protocol layer, before `preflight` is even reached. This means the transactor implementation itself contains no amendment guard — that responsibility is handled upstream by the transaction routing infrastructure.