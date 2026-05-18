# `NFTokenAcceptOffer.cpp` — NFT Offer Settlement Implementation

This file implements the `NFTokenAcceptOffer` transactor, the most financially complex transaction in the XRPL NFT subsystem. While other NFT transactors (`NFTokenMint`, `NFTokenBurn`, `NFTokenCreateOffer`, `NFTokenCancelOffer`) each interact with a single account and a single object, this transactor must coordinate up to four parties — buyer, seller, NFT issuer collecting a royalty, and an optional broker — across multiple atomic ledger writes.

## Operation Modes

The transaction operates in one of two modes determined by which offer field(s) appear in the transaction:

**Direct mode** provides exactly one of `sfNFTokenBuyOffer` or `sfNFTokenSellOffer`. The submitter either owns the NFT and is accepting a buyer's bid, or is a buyer accepting the seller's listed price. The `acceptOffer()` helper routes this path.

**Brokered mode** provides both offer IDs simultaneously. The submitting account is a third-party broker who has matched a standing buy offer against a standing sell offer. The broker does not own the token, never receives it, and may collect a fee via `sfNFTokenBrokerFee`. This path is handled inline within `doApply()` rather than through `acceptOffer()` because the payment sequencing differs structurally.

## Preflight: Stateless Field Validation

`preflight()` enforces two invariants without touching the ledger. First, at least one offer ID must be present — providing neither is `temMALFORMED`. Second, `sfNFTokenBrokerFee` is only legal in brokered mode (both offer IDs present) and must be strictly positive; a broker fee in direct mode, or a zero fee, returns `temMALFORMED`. These are caught before any I/O because they reflect purely malformed transaction construction.

## Preclaim: Stateful Offer Validation

`preclaim()` performs the bulk of offer-level validation using a local `checkOffer` lambda that takes an optional offer ID and returns the loaded `SLE` alongside a `TER`. For each offer it confirms: the ID is non-zero, the ledger object exists, the stored amount is not negative, and the offer hasn't expired.

Expiration handling reveals a deliberate amendment-driven behavior change. Before `fixExpiredNFTokenOfferRemoval`, expired offers immediately returned `tecEXPIRED` from `preclaim`, permanently stranding the ledger object since the apply phase was never reached. After the amendment, `checkOffer` allows expired offers through `preclaim` and returns the SLE; `doApply` then deletes the object before returning `tecEXPIRED`. This amendment fixes a ledger garbage-collection bug without changing the externally visible error code.

For brokered mode, `preclaim` verifies that both offers reference the same `sfNFTokenID` and the same payment asset (`tecNFTOKEN_BUY_SELL_MISMATCH` otherwise), that the buyer's bid is at least as large as the seller's ask, that the broker fee doesn't exceed the bid, and that after subtracting the broker fee the remaining amount still satisfies the seller's ask. The anti-loop check — rejecting the case where both offers share the same `sfOwner` — prevents a degenerate scenario where a broker tries to facilitate a self-trade.

Trust-line hygiene is enforced across two graduated amendments. `fixEnforceNFTokenTrustline` prevents the NFT issuer from being granted an unintended trust line when a transfer fee is owed but no line already exists. `fixEnforceNFTokenTrustlineV2` extends coverage to every IOU payment recipient: seller, buyer, broker, and issuer are each validated via `nft::checkTrustlineAuthorized` and `nft::checkTrustlineDeepFrozen` as appropriate to the mode. The checks are gated at the level of each participant's involvement — for example, in brokered mode the submitter's (broker's) trustline for the sell-side asset is not checked, because the broker in that mode is not the party receiving the IOU payment.

## `pay()`: Atomic Payment with Balance Assertions

Rather than calling `accountSend` directly, all monetary transfers within this transactor route through the private `pay()` helper. After a successful `accountSend`, `pay()` re-evaluates both the sender's and receiver's effective balance using `accountFunds` with `fhZERO_IF_FROZEN`. A negative result from either check returns `tecINSUFFICIENT_FUNDS` immediately.

This extra step exists because IOU transfer fees can produce counterintuitive outcomes: a payer with exactly enough balance may end up with a small negative after the fee is credited to the issuer, or a receiver holding the issuing account's own currency may end up with a positive IOU balance from the issuer's perspective. The post-payment assertion catches these edge cases before the ledger write is finalised, converting what would otherwise be a corrupt ledger state into an orderly transaction failure.

## `transferNFToken()`: Ownership Move with Reserve Check

Ownership transfer is a page-management operation. `nft::findTokenAndPage` locates the token object and its containing `NFTokenPage` in the seller's directory. `nft::removeToken` removes it from that page, potentially collapsing pages and decrementing the seller's owner count. `nft::insertToken` places the token into the buyer's directory, potentially allocating a new page and incrementing the buyer's owner count.

After insertion, if `fixNFTokenReserve` is enabled and the buyer's owner count increased, the method checks whether the buyer's current `sfBalance` meets the reserve for the new count. Critically, this uses the buyer's current balance — which has already been reduced by the NFT purchase price and the transaction fee — rather than `preFeeBalance_`. Using the pre-fee balance would overstate available reserve and allow a buyer to acquire an NFT without sufficient backing, which was the original bug this amendment addresses. The comment notes the small caveat that the transaction fee has already been deducted, making the effective reserve requirement a few drops higher than the pure owner-count-based calculation would suggest.

## `acceptOffer()` and `doApply()`: Direct vs. Brokered Payment Sequencing

`acceptOffer()` handles direct mode. It reads the offer's `sfAmount`, computes the issuer's royalty cut using `nft::getTransferFee` and `nft::transferFeeAsRate`, pays the issuer (if the cut is non-zero and neither buyer nor seller is the issuer), pays the seller the remainder, then calls `transferNFToken`. Skipping the royalty when the buyer or seller is the issuer prevents nonsensical self-payments.

`doApply()` handles the full dispatch. First, with `fixExpiredNFTokenOfferRemoval` active, it attempts to delete any expired offer via `nft::deleteTokenOffer` and returns `tecEXPIRED` if any were found — ensuring expired objects don't persist. Then it unconditionally deletes the valid offers being accepted (both buy and sell objects are always removed from the ledger regardless of mode). For brokered mode, payments are sequenced strictly: broker cut first, then the issuer's royalty is calculated on what remains, then the seller receives the final remainder. The comment in the code makes the ordering invariant explicit — computing the issuer's royalty before removing the broker's fee would allow total disbursements to exceed what the buyer originally authorised.

## Error Handling and Defensive Patterns

Several `tecINTERNAL` returns are marked with `// LCOV_EXCL_LINE` annotations, indicating paths that are logically unreachable given the invariants established in `preclaim` but are included as defensive guards. Fatal-level log messages accompany the two cases where `nft::deleteTokenOffer` unexpectedly fails, which would indicate a ledger consistency problem rather than a user input issue. The overall design avoids throwing exceptions, relying entirely on `TER` return codes propagated up the call chain.