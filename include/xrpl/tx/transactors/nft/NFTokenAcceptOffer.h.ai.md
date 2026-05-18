# `NFTokenAcceptOffer.h` — NFT Offer Acceptance Transactor

This header declares `NFTokenAcceptOffer`, the transaction processor responsible for settling NFT trades on the XRP Ledger. It inherits from `Transactor` and implements the standard three-phase execution model — `preflight`, `preclaim`, `doApply` — while adding four private helpers that encapsulate the financial mechanics of an NFT sale.

## Role in the NFT Transaction Family

Within the `transactors/nft/` directory, the related transaction types (`NFTokenMint`, `NFTokenBurn`, `NFTokenCreateOffer`, `NFTokenCancelOffer`, `NFTokenModify`) each deal with a single actor and a single ledger object. `NFTokenAcceptOffer` is the most complex because a sale may involve up to four distinct parties simultaneously: the buyer, the seller, the NFT issuer collecting a royalty, and an optional broker. The interface reflects that complexity with helpers for routing payments separately from the NFT ownership transfer.

## Operation Modes: Direct vs. Brokered

The transaction supports two mutually exclusive logical modes:

**Direct mode** supplies exactly one of `sfNFTokenBuyOffer` or `sfNFTokenSellOffer`. The transaction submitter is either the NFT owner selling directly into a buy offer, or a buyer purchasing directly from a sell offer. The `acceptOffer()` helper handles this path, computing the issuer's royalty cut before forwarding the remainder to the seller, then calling `transferNFToken()`.

**Brokered mode** supplies both offer IDs. The submitting account is a broker who matches a buyer's bid against a seller's ask without owning the token. The broker may also claim a fee via `sfNFTokenBrokerFee`. Payment sequencing in this mode, implemented inline in `doApply()`, is architecturally deliberate: the broker collects their cut first, the issuer's royalty is computed on what remains, and the seller receives whatever is left after both deductions. Computing the royalty before removing the broker's fee would allow total payouts to exceed what the buyer authorised — a correctness invariant enforced by this ordering.

## Preflight and Preclaim Validation

`preflight()` performs stateless sanity checks: at least one offer ID must be present, and `sfNFTokenBrokerFee` is only valid in brokered mode and must be strictly positive. These rules are enforced before any ledger reads.

`preclaim()` is the heavy validation stage. For each offer it verifies existence, that the offer's amount field is non-negative, and handles expiration. Pre-`fixExpiredNFTokenOfferRemoval`, expired offers return `tecEXPIRED` immediately from `preclaim`, leaving the ledger object stranded. After the amendment, `preclaim` allows expired offers through to `doApply`, where they are deleted before returning `tecEXPIRED` — guaranteeing proper garbage collection.

In brokered mode, `preclaim` also confirms that the two offers reference the same token ID and the same payment asset, that the buyer's bid is at least as large as the seller's ask (and also at least as large as the seller's ask plus the broker's fee), and that neither party is trying to sell to themselves. In direct mode, it checks that the submitter actually owns the token (for buy-offer acceptance) or has sufficient funds (for sell-offer acceptance).

Trust-line hygiene is incrementally enforced through two amendments. `fixEnforceNFTokenTrustline` prevents the issuer from being granted an unintended trust line if a royalty is due but no line exists. `fixEnforceNFTokenTrustlineV2` extends this to check that every payment recipient — seller, buyer, broker, and issuer — has an authorised, non-deep-frozen trust line for the IOU being transferred.

## The `pay()` Helper and Post-Transfer Balance Checks

Rather than calling `accountSend` directly, all money movement inside this transactor goes through `pay()`. After a successful transfer it re-checks both the sender's and receiver's balances with `accountFunds`. A successful `accountSend` can still leave a balance negative in pathological IOU-with-transfer-fee scenarios; the post-payment assertion in `pay()` catches this and aborts with `tecINSUFFICIENT_FUNDS` before the ledger write is committed.

## `transferNFToken()` and the Reserve Invariant

Ownership transfer is a two-step ledger operation: `nft::removeToken` strips the token from the seller's NFToken page, then `nft::insertToken` places it into the buyer's. After insertion, if `fixNFTokenReserve` is enabled and the buyer's owner count increased (meaning a new page was allocated), the method checks whether the buyer's current balance satisfies the reserve requirement for the new count. Using the current balance rather than `preFeeBalance_` is necessary here because the buyer may have already paid for the token — using the pre-fee balance would overstate their available reserve and allow an under-reserved purchase.

## Design Constraints

`ConsequencesFactory` is set to `Normal`, meaning this transaction does not impose any extraordinary sequencing constraints on other transactions in the same ledger. The constructor simply forwards `ApplyContext` to the base class `Transactor`, which holds the view, journal, and account state for the duration of apply.