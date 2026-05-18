# `NFTokenCancelOffer` Transactor

## Role in the System

`NFTokenCancelOffer` is the transactor responsible for removing one or more outstanding `NFTokenOffer` ledger objects — the buy or sell offers created via `NFTokenCreateOffer`. It lives in the `nft/` transactor subdirectory alongside the rest of the NFT lifecycle operations (`NFTokenMint`, `NFTokenBurn`, `NFTokenCreateOffer`, `NFTokenAcceptOffer`, `NFTokenModify`), and it follows the same three-phase pipeline that all XRPL transactors use: `preflight` → `preclaim` → `doApply`.

## Class Design

`NFTokenCancelOffer` inherits from `Transactor` and adds no new data members. Construction simply forwards the `ApplyContext` to the base class. The `static constexpr ConsequencesFactory{Normal}` tag tells the framework that this transaction has ordinary reserve consequences — it does not block other transactions from the same account (`Blocker`) and does not need a custom consequences calculation (`Custom`).

The absence of a `getFlagsMask` override (unlike `NFTokenCreateOffer`, which does define one) means this transactor accepts only the universal transaction flags; there are no NFT-cancel–specific flag bits.

## Preflight: Stateless Validity Checks

`preflight` runs before any ledger state is consulted and performs two lightweight structural checks on the `sfNFTokenOffers` vector:

1. **Bounds check** — the list must be non-empty and must not exceed `maxTokenOfferCancelCount` (defined as 500 in `Protocol.h`). An empty list has no meaningful purpose, and an oversized list is rejected as a denial-of-service vector: transactions grow with the number of offer IDs, and processing 500 deletions per transaction is already a generous upper bound.

2. **Duplicate check** — the list is sorted and scanned for adjacent duplicates via `std::adjacent_find`. Any duplicate returns `temMALFORMED`. This prevents a submitter from padding a transaction with repeated IDs to waste validator CPU or artificially inflate fee refunds, and it is the reason the check sorts a *copy* of the IDs rather than the original field.

## Preclaim: Permission Verification Against Live Ledger State

`preclaim` verifies that the submitting account is entitled to cancel every offer in the list. It iterates the IDs and for each one applies the following logic, short-circuiting on the first forbidden entry:

- **Missing offer** — if the ledger object does not exist, the offer was already consumed or cancelled; silently skip it. This makes the operation idempotent with respect to already-gone offers.
- **Wrong object type** — if an ID resolves to a ledger object that is *not* an `ltNFTOKEN_OFFER`, the submitter has no permission. This guards against a submitter passing an ID that belongs to a different object type (an escrow, a check, etc.).
- **Expired offer** — any account may cancel an expired offer regardless of ownership, so the check returns `false` (allowed) immediately via `hasExpired`.
- **Owner or designated recipient** — the offer's `sfOwner` is always allowed to cancel their own offer. The optional `sfDestination` field, if present and matching the submitting account, grants the same right — a designated counterparty can withdraw from a directed offer.

If any offer in the list fails all of the above permission checks, `preclaim` returns `tecNO_PERMISSION`, preventing the transaction from being applied.

## doApply: Ledger Mutation

`doApply` iterates the same offer ID list and calls `nft::deleteTokenOffer` on each live entry (peered via `keylet::nftoffer`). Missing offers are skipped silently (consistent with `preclaim`'s idempotency stance). If deletion fails for any offer — a situation deemed impossible under correct invariants and therefore excluded from code coverage — the transactor logs a fatal message and returns `tefBAD_LEDGER`, which signals internal ledger corruption rather than a user error.

## Design Observations

The split between `preclaim` and `doApply` is architecturally deliberate: `preclaim` reads ledger state through a `ReadView` (no mutation possible) and decides whether the fee will be claimed; `doApply` mutates through an `ApplyView`. This separation means the ledger is never partially modified when a permission error is detected.

The duplicate-rejection in `preflight` is particularly worth noting: it uses a sort-then-adjacent-find pattern on a local copy, which is O(n log n) but bounded by 500 elements and completely stateless. Sorting the original field object would be incorrect (fields are conceptually immutable at this stage), so the copy is necessary.

The permission model for offer cancellation is deliberately permissive compared to most ledger mutations: expiry removes access control entirely, and the designated destination can cancel just as easily as the creator. This reflects the NFT design goal that expired and directed offers should be easy to clean up without requiring the original creator to be online.