# `NFTokenBurn.cpp` — NFT Burn Transactor

## Role in the System

`NFTokenBurn.cpp` implements the three-phase `NFTokenBurn` transaction handler for destroying an NFT on the XRP Ledger. Burning permanently removes the token from the ledger, cleans up all associated buy and sell offers, and increments a counter on the issuer's account. It sits in the NFT transactor family alongside `NFTokenMint`, `NFTokenCreateOffer`, `NFTokenAcceptOffer`, `NFTokenCancelOffer`, and `NFTokenModify`, all sharing the `nft::` helper namespace for common ledger manipulation.

## Three-Phase Execution Model

Like all XRPL transactors, `NFTokenBurn` follows the framework's canonical three-phase pipeline:

**`preflight`** is intentionally empty — it returns `tesSUCCESS` unconditionally. The framework already validates transaction structure (field presence, account signature, fee) before reaching this hook; there are no additional lightweight checks specific to a burn that can be done without ledger access.

**`preclaim`** is where all meaningful validation happens. It runs against a read-only ledger view before any state is mutated, making it safe to call in parallel across transaction batches. The logic resolves the token owner: if the transaction carries `sfOwner`, that account is taken as the holder; otherwise the submitting account (`sfAccount`) is used as both submitter and holder. This dual-path is what allows an issuer or minter to burn a token they don't currently hold.

**`doApply`** executes the mutation unconditionally, trusting `preclaim` to have already validated all preconditions.

## Permission Architecture

The permission model in `preclaim` is the most nuanced part of the file. Token ownership is always sufficient to burn — no flags or role checks needed. The complexity only enters when a non-owner submits the transaction:

1. The token's `flagBurnable` bit (bit 0 of the 16-bit flags stored in the high bytes of the 256-bit token ID) must be set, or the non-owner receives `tecNO_PERMISSION`. This is the issuer's design-time decision to allow forced recall.
2. Even with `flagBurnable` set, only the **issuer** or the issuer's **designated minter** may burn. The issuer is embedded directly in the `sfNFTokenID` via `nft::getIssuer()`, which reads bytes from the token ID's fixed layout. If the submitting account is not the issuer, the code reads the issuer's account SLE and checks the optional `sfNFTokenMinter` field — a delegated-minting relationship set via `AccountSet`. Only if the submitter matches either the issuer or the current minter is the burn permitted.

This three-tier hierarchy (owner → issuer → minter) is a deliberate design choice. Encoding the issuer directly in the token ID means the permission check never needs to look up historical ledger state or a separate registry; the token ID is self-describing.

## State Mutations in `doApply`

`doApply` performs three distinct mutations:

**Token removal** via `nft::removeToken()` strips the token from the owner's `NFTokenPage` directory. The return value is checked and propagated, but the comment acknowledges this guard "should never happen" because `preclaim` already confirmed existence — it is purely defensive.

**Issuer burn counter** — after removal, the code peeks the issuer's account SLE and increments `sfBurnedNFTokens` using `value_or(0)` because the field is optional and absent until the first burn. This counter is append-only and provides on-chain accounting for how many tokens of that issuance have been destroyed, without requiring a full scan of the token namespace.

**Offer cleanup** deletes all open offers for the burned token up to a hard cap of `maxDeletableTokenOfferEntries` (500, from `Protocol.h`). The allocation between sell and buy offers is non-trivial: sell offers are processed first, and only the remaining budget (`500 - deletedSellOffers`) is applied to buy offers. The rationale in the comment is that sell offers are typically fewer, so attacking them first maximises the chance of completely clearing the sell-offer directory within a single transaction. Any offers that exceed the 500-item budget are simply left orphaned on the ledger — an existing `notTooManyOffers()` preclaim guard in sibling code prevents a token from becoming permanently un-burnable by capping total live offers before this path is reached.

## Key Relationships

- **`NFTokenHelpers.h` / `NFTokenHelpers.cpp`** — provides `findToken()`, `removeToken()`, `removeTokenOffersWithLimit()`, and `notTooManyOffers()`. The burn transactor is essentially a thin orchestration layer over these helpers.
- **`protocol/nft.h`** — `nft::getFlags()` and `nft::getIssuer()` extract permission metadata by reading directly from the packed binary layout of the 256-bit token ID using `memcpy` + big-endian conversion. No ledger lookup is needed.
- **`Transactor` base class** — supplies `view()`, `ctx_`, and the `doApply()` override contract. `ConsequencesFactory{Normal}` tells the transaction queue that this transaction has standard fee consequences.
- **`Protocol.h`** — `maxDeletableTokenOfferEntries = 500` bounds the offer deletion loop, a per-transaction work cap that prevents this operation from being used to consume unbounded compute.