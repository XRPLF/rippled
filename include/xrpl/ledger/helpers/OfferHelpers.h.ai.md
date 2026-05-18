# `include/xrpl/ledger/helpers/OfferHelpers.h`

This header is part of the `ledger/helpers` module — a collection of focused utility headers, each scoped to a single ledger object type. `OfferHelpers.h` exposes exactly one function: `offerDelete`, the canonical routine for removing an offer from the XRPL ledger state. Its narrow scope reflects the design philosophy of this module: each helper encapsulates the multi-step bookkeeping for its object type so that callers deal only with intent, not mechanics.

## Why a dedicated helper exists

Deleting an offer is not a single-object erasure. An offer entry (`ltOFFER`) participates in two separate ledger directory structures simultaneously: the owner's `ownerDir` (indexed by account, used to enforce reserve requirements and enumerate an account's objects) and the order-book directory (`keylet::page(sfBookDirectory)`, used by the DEX path-finding engine to enumerate offers at a given quality tier). Both directory back-references must be cleaned up atomically within the `ApplyView` transaction buffer before the SLE itself is erased, and the owner's reserve count must be decremented. Scattering this sequence across callers would be error-prone, so it lives here.

## Callers and context

`offerDelete` is invoked from three distinct contexts in the codebase:

- **`OfferCancel::doApply`** — the explicit cancellation transaction. The caller first resolves the offer by sequence number via `keylet::offer(account_, offerSequence)` and then delegates the teardown entirely to `offerDelete`.
- **`OfferCreate::doApply`** — during offer crossing, fully-consumed counter-offers in `removableOffers` are deleted from the sandbox view. Partially-consumed offers that are replaced by the new order are also pruned here.
- **`BookTip::step`** — during payment path traversal. When the path engine advances through order-book pages, it deletes the previously-consumed (or expired/unfunded) offer from the previous `step` call before probing the next candidate. This is the hot path for AMM and DEX payments.

The breadth of these call sites explains the deliberate choice to comment out `[[nodiscard]]`. The standard flow and `BookTip` callers do not always inspect the `TER` return, and making the attribute mandatory would have broken compilation across the engine. The comment preserves the intent without enforcing it.

## Implementation walkthrough

The implementation in `OfferHelpers.cpp` starts with a null guard: if `sle` is empty, it returns `tesSUCCESS` immediately. This handles the case where a caller peeks an offer that has already been deleted in the same transaction batch — a defensive idiom that prevents double-delete panics without requiring the caller to pre-check.

The deletion sequence for a normal offer is:

1. Remove the entry from the owner's directory via `view.dirRemove(keylet::ownerDir(owner), sfOwnerNode, offerIndex, false)`.
2. Remove the entry from the order-book directory via `view.dirRemove(keylet::page(uDirectory), sfBookNode, offerIndex, false)`.
3. Decrement the owner count by calling `adjustOwnerCount(..., -1, j)`, which writes back into the account root SLE and keeps the on-ledger reserve calculation accurate.
4. Erase the offer SLE itself via `view.erase(sle)`.

If either `dirRemove` call returns false, the function returns `tefBAD_LEDGER`. Both such branches carry `LCOV_EXCL_LINE` annotations, signalling that they represent invariant violations — if an offer SLE exists with a valid `sfOwnerNode` and `sfBookNode`, its corresponding directory entries must exist. The error code is present for safety, not for expected operation.

## Hybrid domain offers

A notable extension handles offers flagged `lsfHybrid` — offers that participate in a Permissioned DEX domain in addition to the global order book. These offers carry an `sfAdditionalBooks` array, each element encoding an additional `sfBookDirectory` and `sfBookNode`. When present, the function iterates this array and issues one additional `dirRemove` call per extra book directory before proceeding to the owner-count adjustment and SLE erasure. An `XRPL_ASSERT` validates that the `lsfHybrid` flag and `sfDomainID` are both set whenever `sfAdditionalBooks` is present, enforcing the invariant that hybrid metadata is always consistent.

## Contract and caller responsibilities

The header comment states two preconditions explicitly: the offer must exist, and the caller must have already verified permissions. The function does not re-check whether the submitting account owns the offer — that is the responsibility of each calling transactor. This keeps `offerDelete` a pure bookkeeping utility, free of policy logic, which is exactly the right abstraction boundary for shared ledger-layer helpers.