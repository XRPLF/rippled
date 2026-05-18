# `NFTokenCreateOffer.cpp` — NFT Offer Creation Transactor

## Role in the System

This file implements the `NFTokenCreateOffer` transactor, which handles the XRPL transaction that places an offer to buy or sell an NFT. It sits within the `src/libxrpl/tx/transactors/nft/` directory alongside the other NFT transaction types (`NFTokenMint`, `NFTokenBurn`, `NFTokenAcceptOffer`, etc.). As a transactor, it is responsible for validating and applying one specific transaction type to the ledger, following the standard XRPL three-phase lifecycle: `preflight` → `preclaim` → `doApply`.

The file itself is intentionally thin — barely 80 lines. Nearly all substantive logic lives in shared helper functions declared in `xrpl/ledger/helpers/NFTokenHelpers.h` and also used by `NFTokenMint`. This is the defining architectural choice of this file.

## The Three-Phase Transactor Pattern

`NFTokenCreateOffer` inherits from `Transactor` and overrides three static methods plus `doApply()`:

**`getFlagsMask`** returns `tfNFTokenCreateOfferMask`, which is generated automatically from the `TxFlags.h` X-macro system. The `TRANSACTION(NFTokenCreateOffer, TF_FLAG(tfSellNFToken, 0x00000001), MASK_ADJ(0))` entry creates this mask, which encompasses `tfSellNFToken` plus the universal flags. The framework uses this mask in `preflight1()` to reject any transaction whose flags include bits not recognized for this type — a forward-compatibility guard against clients setting bits that might acquire meaning under future amendments.

**`preflight`** runs stateless, no-ledger-access validation. It first extracts the token's embedded flags from the `sfNFTokenID` field (NFT flags like `lsfBurnable`, `lsfOnlyXRP`, and `lsfTransferable` are encoded directly into the token ID), then delegates all parameter validation to `nft::tokenOfferCreatePreflight()`. That shared function validates the offer amount, optional destination and expiration fields, ownership rules, and transaction flags.

**`preclaim`** performs read-only ledger checks. It does two things the shared code cannot: first, it calls `hasExpired()` to reject an already-expired offer before any other work — this is a ledger-level time check that needs the current ledger's close time. Second, it resolves which account's NFT directory to search based on the `tfSellNFToken` flag:

```cpp
nft::findToken(ctx.view,
    ctx.tx[((txFlags & tfSellNFToken) != 0u) ? sfAccount : sfOwner],
    nftokenID)
```

For a **sell offer**, the token must be in `sfAccount`'s own directory (the submitter is offering to sell their own token). For a **buy offer**, `sfOwner` names the token's current holder, and the token must be in that account's directory. If the token is not found, `tecNO_ENTRY` is returned immediately, avoiding unnecessary downstream processing. After this check, `nft::tokenOfferCreatePreclaim()` validates business-logic constraints like transfer fee eligibility, destination account existence, and whether the NFT's `lsfTransferable` flag permits third-party trading.

**`doApply`** simply calls `nft::tokenOfferCreateApply()`, forwarding all transaction fields plus `preFeeBalance_` (the submitter's XRP balance before the transaction fee was deducted). The apply function is responsible for creating the `NFTokenOffer` ledger object, inserting it into the token's buy or sell directory, and adding it to the offer creator's owner directory (which consumes one reserve increment).

## Code Sharing with `NFTokenMint`

The most notable design decision is the extraction of all three phases into free functions (`tokenOfferCreatePreflight`, `tokenOfferCreatePreclaim`, `tokenOfferCreateApply`) in `NFTokenHelpers`. This is because `NFTokenMint` optionally creates a sell offer simultaneously with minting — if `sfAmount` is present in a mint transaction, the same validation and apply logic must run. Rather than duplicating rules, the shared functions accept `txFlags` as a parameter with a default of `tfSellNFToken`, since a mint-embedded offer is always a sell offer.

The `NFTokenCreateOffer` transactor passes `ctx.tx.getFlags()` explicitly, while `NFTokenMint` passes `tfSellNFToken` as a compile-time constant. This distinction keeps the shared helpers generic while encoding the semantic constraint that minting can only produce sell offers, not buy offers.

## Invariants and Failure Modes

- `tecEXPIRED` is returned in `preclaim` before `findToken` is even called, since an expired offer cannot be created regardless of token state.
- `tecNO_ENTRY` signals that the referenced NFT does not exist in the expected owner's directory. This check is asymmetric: the owner lookup switches on `tfSellNFToken`, so a sell offer where the submitter does not own the token will always fail here.
- The `ConsequencesFactory{Normal}` declaration means the transaction framework calculates standard fee consequences (deducting the transaction fee and incrementing the account sequence), with no special handling like `Blocker` or custom balance impacts.
- Optional fields (`sfDestination`, `sfExpiration`, `sfOwner`) are accessed via the `~sfField` idiom, which returns `std::optional<T>` rather than throwing on absence — a consistent XRPL pattern for fields that may be omitted without error.