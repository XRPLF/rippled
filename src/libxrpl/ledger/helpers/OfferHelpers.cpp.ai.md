# `OfferHelpers.cpp` — Offer Deletion from the XRPL Ledger

This file provides the single utility function `offerDelete`, which performs the complete and atomic removal of an offer ledger object from an `ApplyView`. It exists as a shared helper because offer deletion is not a self-contained operation — every offer is simultaneously indexed in multiple directory structures, and all of those registrations must be unwound together before the object itself can be erased.

## Why Deletion Requires a Helper

In the XRPL ledger model, an offer (`ltOFFER` SLE) is not a standalone object. When an offer is created, its key is inserted into two separate directories: the owning account's personal owner directory (used to enumerate all objects an account holds, and to enforce reserve requirements), and the order book directory corresponding to the offer's currency pair (used by the DEX matching engine to locate counterparty offers). Each insertion records a position hint — `sfOwnerNode` and `sfBookNode` respectively — so removal can be done in O(1) without scanning the entire directory.

`offerDelete` encapsulates the complete removal sequence in the correct order: directories first, owner count adjustment next, and the object erasure last. Inverting this order would leave dangling directory entries or produce an incorrect reserve calculation.

## Hybrid Domain Offers

The most architecturally significant branch in this function handles the Permissioned DEX feature, where an offer can be classified as *hybrid*. A hybrid offer carries the `lsfHybrid` flag, a `sfDomainID` identifying which permissioned domain it belongs to, and a `sfAdditionalBooks` array containing extra book-directory registrations.

The motivation is that a hybrid offer participates in both the permissioned domain's book *and* the global open order book simultaneously, giving it exposure to both matching pools. Consequently, removing a hybrid offer requires iterating over `sfAdditionalBooks` and calling `view.dirRemove` for each additional registration, beyond the two standard removals. The `XRPL_ASSERT` before this loop enforces the ledger-level invariant: if `sfAdditionalBooks` is present, the offer must also carry `lsfHybrid` and `sfDomainID`. Any violation signals ledger corruption or a programming error, not a recoverable runtime condition.

## Error Handling and the Null Guard

The null pointer guard at the top of the function (`if (!sle) return tesSUCCESS`) is deliberately lenient. The header contract states the offer must exist, but several callers — notably `OfferCreate.cpp`'s crossing loop and `BookTip.cpp`'s streaming logic — call `offerDelete` in contexts where the offer may have already been removed by a prior step. Returning `tesSUCCESS` rather than an error allows those callers to remain unconditional without requiring pre-checks.

All `dirRemove` failures return `tefBAD_LEDGER`, indicating a structural inconsistency in the ledger state. These paths are annotated `LCOV_EXCL_LINE` — they represent situations that should be impossible in a correctly operating system (the offer's directory position was verified to exist when the offer was created), so they are not reachable in normal tests. They serve as defensive guards against ledger corruption detected late.

## Owner Count and Reserve

After all directory registrations are removed, `adjustOwnerCount` is called with a delta of `-1`. This function (from `AccountRootHelpers`) reads the current `sfOwnerCount` on the account SLE, clamps any underflow to zero while logging a fatal error, and writes the updated count back. The owner count directly determines the XRP reserve requirement for the account — each owned ledger object adds one incremental reserve. Decrementing it only *after* directory removal ensures there is never a window where the ledger shows the offer as gone from the book but still imposing a reserve.

## Callers

`offerDelete` is called from at least four sites: `OfferCancel::doApply` (explicit user cancellation), `OfferCreate` during offer crossing and prior-offer cancellation, `BookTip`'s offer streaming path when it encounters expired or unfunded offers, and `AccountDelete` when cleaning up an account's remaining offers. The function's signature takes an `ApplyView&`, so all callers operate within a transaction-scoped view that can be rolled back; the deletions are not committed to the ledger until the enclosing transaction succeeds.