# `NFTokenMint.h` — NFT Minting Transactor

`NFTokenMint` is the transactor that handles `NFTokenMint` transactions on the XRP Ledger. Its job is to create new non-fungible tokens, record them in the issuer's on-ledger state, and optionally produce an initial sell offer for the freshly-minted token in the same atomic step. The class lives inside `include/xrpl/tx/transactors/nft/` alongside the other NFT-specific transactors (`NFTokenBurn`, `NFTokenCreateOffer`, etc.) and follows the same lifecycle contract that every transactor in the system must satisfy.

## Inheritance and Factory Type

`NFTokenMint` publicly inherits from `Transactor`, which provides the common transaction-processing infrastructure: fee consumption, sequence-number management, signature checking, and the entry-point `operator()()`. The `ConsequencesFactory` is set to `Normal`, meaning the transaction claims a fee in the normal way — no special blocking or custom consequence logic is required.

The `Transactor` base uses **compile-time polymorphism**, not virtual dispatch, for the preflight pipeline. The `invokePreflight<T>` template calls `T::checkExtraFeatures`, `T::getFlagsMask`, `T::preflight`, and `T::preflightSigValidated` by name, resolved at compile time. Only `doApply()` uses a true virtual override.

## Preflight Pipeline

Three static methods participate in preflight and each covers a different concern.

**`checkExtraFeatures`** guards the embedded-offer sub-feature. If the transaction contains `sfAmount`, `sfDestination`, or `sfExpiration` (the fields needed to simultaneously create a sell offer), it returns `false` unless the `featureNFTokenMintOffer` amendment is enabled. This ensures that the combined mint-and-offer flow cannot be activated on a network that hasn't voted for it.

**`getFlagsMask`** is amendment-sensitive in a non-trivial way. The `tfTrustLine` flag was historically allowed to let the minting issuer opt into automatic trustline creation during NFT transfers, but this opened a denial-of-service attack: two cooperating accounts could trade an NFT back and forth, each transfer creating a new trustline on the issuer and unboundedly growing the issuer's reserve. The `fixRemoveNFTokenAutoTrustLine` amendment permanently disabled `tfTrustLine` minting. Because the valid flag mask therefore depends on which amendments are active, `getFlagsMask` checks both `fixRemoveNFTokenAutoTrustLine` and `featureDynamicNFT` (which adds the `tfMutable` flag for mutable-URI NFTs) and returns the appropriate bitmask from four possible combinations.

**`preflight`** validates the transaction fields specific to minting:
- The `sfTransferFee` must not exceed `maxTransferFee`; if it is non-zero, `tfTransferable` must also be set (a non-transferable token with a transfer fee is contradictory).
- The `sfIssuer` field, when present, must not equal the signing account — the authorized-minter pattern requires the minter and issuer to be distinct.
- The `sfURI` must be non-empty and within `maxTokenURILength`.
- If offer fields are present, `sfAmount` is mandatory (destination and expiration alone without an amount make no sense), and validation is delegated to `nft::tokenOfferCreatePreflight()` from `NFTokenHelpers.h`, which is shared with `NFTokenCreateOffer`.

## Preclaim

`preclaim` performs the checks that need ledger state but no application-phase writes. It handles the authorized-minter check: when `sfIssuer` is present it reads the issuer's `AccountRoot` and verifies that `sfNFTokenMinter` matches the signing account. A missing issuer account returns `tecNO_ISSUER`; a mismatch returns `tecNO_PERMISSION`. If the transaction includes offer fields, it also invokes `nft::tokenOfferCreatePreclaim()` to check offer-specific ledger conditions (expiry, trustline authorization, deep-freeze status, etc.).

## Token ID Construction — `createNFTokenID`

The static `createNFTokenID` method is exposed publicly to enable unit testing. It packs five fields into a 32-byte big-endian buffer that becomes the `uint256` token ID:

| Bytes | Content |
|-------|---------|
| 0–1   | Flags (2 bytes) |
| 2–3   | Transfer fee (2 bytes) |
| 4–23  | Issuer `AccountID` (20 bytes) |
| 24–27 | Ciphered taxon (4 bytes) |
| 28–31 | Token sequence number (4 bytes) |

The taxon is **scrambled** before packing using `nft::cipheredTaxon()`, which applies a linear congruential transform keyed on the sequence number: `taxon ^ ((384160001 * tokenSeq) + 2459)`. This ensures that an issuer who mints many tokens with the same taxon does not pack them all into the same NFToken page, which would degrade lookup and deletion performance. The Hull-Dobell theorem guarantees that this transform is a permutation of the 32-bit integer space, so the scrambling is lossless and reversible. Crucially, those magic constants are **protocol-frozen** — changing them would break the ability to interpret existing token IDs, requiring a new amendment and a disambiguation mechanism.

All fields are converted to big-endian before packing; the helper functions in `nft.h` (`getFlags`, `getTransferFee`, `getSerial`, `getTaxon`) reverse the process when reading a token ID.

## Application Phase — `doApply`

`doApply` performs all ledger mutations:

1. **Sequence bookkeeping**: The issuer's `AccountRoot` tracks `sfFirstNFTokenSequence` and `sfMintedNFTokens`. On the very first mint, `sfFirstNFTokenSequence` is initialized to the issuer's current account sequence. There is a subtle edge case here: when a sequence-based (non-ticket) transaction is submitted by the issuer themselves, the sequence has already been pre-incremented by the time `doApply` runs, so the stored value must be decremented by one. When an authorized minter submits the transaction (or the issuer uses a ticket), the issuer's sequence is untouched and the raw value is used directly. Each subsequent mint increments `sfMintedNFTokens`; the token's unique sequence is `sfFirstNFTokenSequence + (sfMintedNFTokens - 1)`. Overflow is checked explicitly to return `tecMAX_SEQUENCE_REACHED` rather than wrap.

2. **Token insertion**: The new `STObject` for the NFToken is assembled from the inner-object template registered for `sfNFToken`, populated with the computed ID and optional URI, then inserted via `nft::insertToken()`, which handles the underlying NFToken page structure.

3. **Optional sell offer**: If `sfAmount` is present, `nft::tokenOfferCreateApply()` creates a sell offer for the new token in the same transaction, sharing all the offer-creation logic with `NFTokenCreateOffer`.

4. **Reserve check**: The reserve is checked only if the owner count increased compared to its pre-mint value. This is intentional: packing additional NFTs into an existing page does not increase the owner count, so it should not require a reserve top-up. Only creating a new NFToken page (or a sell offer) triggers the reserve check, keeping the minting experience predictable for issuers operating near their reserve limit.

## Shared Logic with NFTokenCreateOffer

The three helper functions `nft::tokenOfferCreatePreflight`, `nft::tokenOfferCreatePreclaim`, and `nft::tokenOfferCreateApply` (declared in `NFTokenHelpers.h`) are consumed by both `NFTokenMint` and `NFTokenCreateOffer`. This avoids duplication of the offer-validation rules and ensures consistent behavior whether an offer is created standalone or bundled into a mint. `NFTokenMint` always passes `tfSellNFToken` as the transaction flags for these helpers because the embedded-offer path only supports sell offers — you cannot atomically mint and create a buy offer.