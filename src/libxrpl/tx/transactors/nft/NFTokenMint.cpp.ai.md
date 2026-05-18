# `NFTokenMint.cpp` — NFT Minting Transactor

## Role and Purpose

This file implements the `NFTokenMint` transactor, which handles the XRPL transaction that creates a new Non-Fungible Token (NFT) on the ledger. It is one of six NFT-specific transactors in the `src/libxrpl/tx/transactors/nft/` module and is responsible for the full lifecycle of token creation: validating inputs before any ledger state is touched, verifying preconditions against live ledger state, and then materializing the token in the owner's NFToken page directory. The file also implements the canonical algorithm for constructing a globally unique 256-bit NFToken ID — a compact encoding that eliminates the need for a separate ID registry.

## The Three-Phase Transaction Model

XRPL transactions follow a pipeline: `preflight` (stateless validation), `preclaim` (read-only ledger checks), and `doApply` (state mutations). `NFTokenMint` implements all three phases as static methods on the `NFTokenMint` class.

**`preflight`** rejects nonsensical inputs before any ledger I/O occurs. Its checks are deliberately sequenced from cheapest to most expensive:

- A `sfTransferFee` exceeding `maxTransferFee` (50,000 basis points / 50%) returns `temBAD_NFTOKEN_TRANSFER_FEE`. A non-zero fee without the `tfTransferable` flag is logically contradictory — you cannot collect royalties on a non-transferable token — so it returns `temMALFORMED`.
- Setting `sfIssuer` equal to `sfAccount` is meaningless (the account is already the issuer by default) and is rejected as `temMALFORMED`.
- A present `sfURI` must be non-empty and no longer than `maxTokenURILength` (256 bytes). The empty-string case is rejected because it is ambiguous with the absent-field signal; callers who want no URI simply omit the field.
- If any of `sfAmount`, `sfDestination`, or `sfExpiration` are present the code delegates to `nft::tokenOfferCreatePreflight`, the shared offer-validation routine also used by `NFTokenCreateOffer`. This reuse enforces consistent offer semantics across both transaction types.

**`preclaim`** performs read-only ledger queries to confirm the issuer exists and the minter has permission. When `sfIssuer` is set, it reads the issuer's `AccountRoot` and verifies that the `sfNFTokenMinter` field on that account matches the transaction sender. If it does not, `tecNO_PERMISSION` is returned. When offer fields are present it also checks expiration and delegates to `nft::tokenOfferCreatePreclaim`.

**`doApply`** applies state changes: it increments the issuer's mint counter, constructs the NFToken ID, inserts the token into the owner's page directory, optionally creates an immediately-attached sell offer, and then checks the reserve.

## Constructing the NFToken ID

`createNFTokenID` produces a deterministic, compact 256-bit identifier by packing five fields into a fixed-layout big-endian byte array:

```
[0–1]   flags (2 bytes)
[2–3]   transfer fee (2 bytes)
[4–23]  issuer AccountID (20 bytes)
[24–27] ciphered taxon (4 bytes)
[28–31] token sequence (4 bytes)
```

This layout is not arbitrary: the accessor functions in `include/xrpl/protocol/nft.h` (e.g., `getFlags`, `getTransferFee`, `getIssuer`, `getTaxon`, `getSerial`) hard-code these byte offsets to extract fields from a live token ID without deserializing anything. The method is `public` and marked as supporting unit tests to allow direct exercising of the ID construction logic independent of ledger machinery.

### The Ciphered Taxon

Before packing, the taxon is passed through `nft::cipheredTaxon(tokenSeq, taxon)`, which applies a linear congruential transformation: `taxon XOR ((384160001 * tokenSeq) + 2459)`. This permutation is chosen because of the Hull-Dobell theorem guarantees: with an appropriate multiplier and increment over a power-of-two modulus it produces a bijection on `[0, 2^32)`. The purpose is ledger performance: if many NFTs shared the same taxon, their IDs would cluster together in the B-tree page directory, forming oversized pages. By mixing the taxon with the sequence number (which is outside the issuer's direct control), the distribution is spread across many pages without adding any storage overhead. Crucially, the transformation is marked as a breaking change: altering these constants would require an amendment because it would change the IDs of tokens that would otherwise have been identical.

## Amendment-Aware Flag Masking

`getFlagsMask` returns the set of legal transaction flags using a nested conditional on two amendments:

- **`fixRemoveNFTokenAutoTrustLine`**: Before this amendment, issuers could set the `tfTrustLine` flag, which allowed an NFT transfer to implicitly create a TrustLine on the issuer's account. This was exploited as a reserve-inflation attack — two accounts could trade the token back and forth indefinitely, adding TrustLines (and thus reserve requirements) to the issuer without their consent. Once the amendment is active, `tfTrustLine` is removed from the valid mask entirely, making it illegal to mint with that flag.
- **`featureDynamicNFT`**: Introduced the `tfMutable` flag, which allows the token URI to be modified after minting via `NFTokenModify`. When this amendment is enabled, `tfMutable` is added to the valid mask.

This cascading logic means the flag mask can be one of four values depending on which amendments have activated, all computed at the `PreflightContext` level before any ledger access.

## The Mint Counter and FirstNFTokenSequence Bootstrap

The `doApply` method manages a subtle bootstrapping problem. Each issuer's `AccountRoot` stores a cumulative `sfMintedNFTokens` counter, and the token's sequence number is derived as `sfFirstNFTokenSequence + sfMintedNFTokens`. The `sfFirstNFTokenSequence` is set only once — on the first mint — and its initial value must match the issuer's account sequence at the time of that first mint.

The complication is that by the time `doApply` runs, the account sequence has already been pre-incremented for normal (non-Ticket) transactions. The code therefore subtracts one for self-minting with a direct sequence, but uses the sequence as-is for Ticket-based transactions (which do not increment the account sequence) and for authorized-minter transactions (where the issuer's sequence is completely untouched). Getting this wrong would cause the first token's ID to encode an incorrect sequence, permanently invalidating the uniqueness guarantee.

Overflow is also defended: `sfMintedNFTokens` wraps at 32 bits, so after incrementing, `doApply` checks for zero (wraparound) and also verifies that `tokenSeq` did not overflow back below the `sfFirstNFTokenSequence` offset. Either condition returns `tecMAX_SEQUENCE_REACHED`. These checks use the `Expected<uint32_t, TER>` return type from the lambda, propagating errors without exceptions.

## Composite Mint-and-Offer

When `sfAmount` is present in the transaction, `doApply` calls `nft::tokenOfferCreateApply` immediately after inserting the token. This creates a sell offer atomically with the mint itself — a common pattern for marketplaces that want to list tokens for sale as part of a single transaction. This feature is gated by `featureNFTokenMintOffer`, checked in `checkExtraFeatures`: if the feature is not active and offer fields are present, the transaction is rejected before preflight. The shared `nft::tokenOfferCreate*` routines enforce that only sell offers (not buy offers) can be created this way, by passing `tfSellNFToken` as the implicit transaction flags.

## Reserve Checking

The reserve check at the end of `doApply` is deliberately conditional on whether the owner count actually increased. Inserting an NFToken into an existing page that has spare capacity does not create a new ledger object and therefore should not require an incremental reserve. The reserve check is only enforced when a new page must be allocated or when a new sell offer is created — both of which increment `sfOwnerCount`. This is an intentional performance and usability tradeoff: issuers with many tokens in a given taxon range can add tokens without repeatedly satisfying reserve requirements, as long as the page isn't full.