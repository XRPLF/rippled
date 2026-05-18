# `NFTokenCancelOffer.cpp` — NFT Offer Cancellation Transactor

This file implements the `NFTokenCancelOffer` transaction type, which allows accounts to remove one or more outstanding NFT buy or sell offers from the XRPL ledger. It follows the three-phase transactor pattern shared by all XRPL transaction types: `preflight` (stateless input validation), `preclaim` (stateful permission checks against a read-only ledger view), and `doApply` (ledger mutation).

## Three-Phase Transactor Pattern

`NFTokenCancelOffer` inherits from `Transactor`. The framework calls the three phases in sequence, aborting at the first failure. `preflight` and `preclaim` are static methods that operate on context structs rather than on the object itself; `doApply` is a virtual override with access to `ctx_` and the mutable `view()`.

## `preflight` — Stateless Input Validation

Two invariants are enforced before any ledger access occurs.

**Bounds check.** The `sfNFTokenOffers` vector must be non-empty and must not exceed `maxTokenOfferCancelCount` (defined as 500 in `Protocol.h`). An empty list serves no purpose and is rejected as `temMALFORMED`. The upper bound guards against oversized transactions: each offer ID is a 256-bit hash, and allowing an unbounded list would make the transaction a denial-of-service vector both in terms of transaction size and server-side processing.

**Duplicate detection.** Rather than an O(n²) pairwise comparison, the IDs are copied into a local `STVector256`, sorted, and then checked with `std::adjacent_find`. This detects duplicates in O(n log n) time and keeps the code concise. Duplicates are rejected because they would inflate transaction size without doing any additional work and could obscure intent.

Note the subtle field-access pattern: the first read uses the direct accessor `ctx.tx[sfNFTokenOffers]` to get a const reference for the size check, then a second call to `getFieldV256` creates a mutable copy to sort. This avoids mutating the transaction's own field storage.

## `preclaim` — Per-Offer Permission Model

After stateless validation passes, `preclaim` checks whether the submitting account actually has the right to cancel each named offer. The logic uses `std::find_if` with a predicate that returns `true` (meaning "deny") for any offer the account may not cancel, and `false` (meaning "allow") otherwise. If any offer returns deny, `preclaim` returns `tecNO_PERMISSION`.

Three categories of callers may cancel an offer:

1. **The offer owner** (`sfOwner == account`). The account that placed the offer may always retract it.
2. **The designated destination** (`sfDestination == account`). If the offer was restricted to a specific counterparty, that counterparty may decline it by cancelling.
3. **Anyone, if the offer has expired** (`hasExpired(ctx.view, (*offer)[~sfExpiration])`). Once an offer's expiration time has passed relative to the ledger's close time, it has no economic value and any account is permitted to clean it up. This is a useful garbage-collection mechanism.

Two additional edge cases are handled gracefully:

- **Missing offer.** If `ctx.view.read(keylet::child(id))` returns null, the offer no longer exists — it was presumably consumed by `NFTokenAcceptOffer` between transaction submission and this validation pass. The predicate returns `false` (allow), effectively treating a missing offer as silently skipped rather than an error. This prevents a race condition where a batch cancel fails entirely because one offer was accepted just before the cancel landed.
- **Wrong ledger entry type.** If the 256-bit ID resolves to a ledger entry that is *not* of type `ltNFTOKEN_OFFER`, the account has no permission to delete it. Returning `tecNO_PERMISSION` here prevents a class of spoofing attacks where an attacker tricks a victim into submitting a cancel transaction that targets an unrelated ledger object.

## `doApply` — Ledger Mutation

After the permission gauntlet, `doApply` iterates the offer IDs and calls `nft::deleteTokenOffer(view(), offer)` for each one that still exists in the mutable view. The `deleteTokenOffer` helper (declared in `NFTokenHelpers.h`) handles the two-location bookkeeping that every NFT offer deletion requires: removing the offer from the token's buy or sell directory and from the offer owner's account directory, and releasing the one-offer-per-entry reserve held against the owner's account.

If `deleteTokenOffer` returns `false` despite the offer having been found in the view, `doApply` logs a fatal message and returns `tefBAD_LEDGER`. This path is marked `LCOV_EXCL_START / LCOV_EXCL_STOP`, signalling that it is a defensive catch for an internal consistency failure that should never be triggered in a well-formed ledger. Because `preclaim` already verified that all targeted entries are valid `ltNFTOKEN_OFFER` objects, reaching this path would imply ledger corruption rather than user error.

## Design Observations

The permission model deliberately separates *who placed the offer* from *who should be allowed to retract it*, giving both the creator (owner) and the intended recipient (destination) independent veto rights. This symmetry means neither party can lock the other into an unwanted trade. The expiration escape hatch completes the design by ensuring that offers do not accumulate indefinitely when participants become inactive.

The silent skip for already-consumed offers in both `preclaim` and `doApply` is an intentional idempotency concession: a cancel transaction submitted alongside an accept transaction should not fail simply because the accept arrived first. This is consistent with XRPL's broader design philosophy of avoiding unnecessary transaction failures for economically harmless conditions.