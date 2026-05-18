# `NFTokenCreateOffer.h` — NFT Offer Creation Transactor

This header declares `NFTokenCreateOffer`, one of the six NFT-related transactor classes in the `xrpl/tx/transactors/nft/` directory. It handles transaction type `ttNFTOKEN_CREATE_OFFER` (type code 27), which places either a sell or buy offer for an NFT into the ledger's offer directory. The class is deliberately thin — the header is purely a declaration, and the implementation in the paired `.cpp` file delegates almost all work to shared helper functions in `NFTokenHelpers`.

## The Three-Phase Transactor Pipeline

`NFTokenCreateOffer` fits the standard `Transactor` lifecycle used throughout the XRPL codebase:

1. **`preflight`** — stateless structural validation against the serialized transaction only.
2. **`preclaim`** — read-only ledger checks after signature verification.
3. **`doApply`** — ledger mutation.

Each of these is invoked by the framework via compile-time polymorphism: `Transactor::invokePreflight<T>` calls `T::preflight`, `T::getFlagsMask`, etc. using name hiding rather than virtual dispatch. This means the base class `preclaim` (which returns `tesSUCCESS` unconditionally) is properly overridden here because the framework calls `NFTokenCreateOffer::preclaim` by type, not through a vtable. Only `doApply` uses virtual dispatch and requires `override`.

`ConsequencesFactory{Normal}` declares that this transaction follows standard fee-claiming behavior — it can claim a fee even when it fails, unlike a `Blocker` transaction that would hold up a multi-transaction sequence.

## Flag Masking

`getFlagsMask()` overrides the base class (which returns `tfUniversalMask`) to return `tfNFTokenCreateOfferMask`. This mask defines which transaction flags are legal for this type. The framework calls `preflight1()` passing this mask, which in turn calls `preflight0()` to reject any transaction carrying unrecognized flags — preventing a submitter from setting arbitrary bits that might be interpreted differently by future amendments.

The primary relevant flag is `tfSellNFToken`. When set, the submitter is offering to sell an NFT they own. When unset, the submitter is offering to buy an NFT from its current owner. This distinction is central to the `preclaim` logic.

## Delegation to Shared Helpers

A key architectural decision is that all three implementation methods delegate to functions in `NFTokenHelpers` (`tokenOfferCreatePreflight`, `tokenOfferCreatePreclaim`, `tokenOfferCreateApply`). These same helpers are called by `NFTokenMint`, which can create an offer atomically as part of minting. Rather than duplicating validation logic — which would create a maintenance hazard where NFTokenMint and NFTokenCreateOffer could drift apart — the XRPL codebase factors all offer-creation logic into a single shared path. The transactor itself then acts as a thin adapter that extracts the relevant fields from `ctx_.tx` and passes them through.

## `preclaim` Logic and the Buy/Sell Asymmetry

`preclaim` performs two checks beyond what the shared helper does:

First, it checks whether the offer has already expired before it can even be placed — `hasExpired(ctx.view, ctx.tx[~sfExpiration])`. This cannot be done in `preflight` because expiration is relative to ledger close time, which is part of ledger state, not available at stateless validation time.

Second, it verifies that the NFT actually exists and is owned by the right account:

```cpp
nft::findToken(
    ctx.view,
    ctx.tx[((txFlags & tfSellNFToken) != 0u) ? sfAccount : sfOwner],
    nftokenID)
```

For sell offers, the submitter (`sfAccount`) must own the NFT. For buy offers, the `sfOwner` field must identify the current NFT holder. This asymmetry explains why `sfOwner` is an optional field on the transaction: it is irrelevant for sell offers and required for buy offers.

## Transaction Fields

The auto-generated `xrpl::transactions::NFTokenCreateOffer` wrapper (in `protocol_autogen/`) provides type-safe field access and confirms the on-wire structure:

- `sfNFTokenID` (required): the 256-bit identifier of the NFT being offered.
- `sfAmount` (required): the price — zero is valid for a gift offer.
- `sfDestination` (optional): restricts acceptance to a specific counterparty.
- `sfOwner` (optional): for buy offers, identifies who currently holds the NFT.
- `sfExpiration` (optional): a ledger-time deadline after which the offer cannot be accepted.

The transaction is marked delegable (`Delegation::delegable`), meaning another account may submit it on the owner's behalf if delegation is enabled via the relevant amendment.

## Relationship to Sibling Classes

All six NFT transactors (`NFTokenMint`, `NFTokenBurn`, `NFTokenCreateOffer`, `NFTokenAcceptOffer`, `NFTokenCancelOffer`, `NFTokenModify`) share the same directory and follow the same pattern of a thin header declaration plus a `.cpp` that delegates to helpers. `NFTokenCreateOffer` is the most closely related to `NFTokenMint` — both can create offer objects in the ledger, which is why they share the `tokenOfferCreate*` helper suite rather than each independently reimplementing identical validation paths.