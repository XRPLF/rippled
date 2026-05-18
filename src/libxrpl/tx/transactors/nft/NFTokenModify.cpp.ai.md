# `NFTokenModify.cpp` — NFT Metadata Modification Transactor

`NFTokenModify.cpp` implements the `NFTokenModify` transaction, which allows an authorized party to update the metadata URI attached to a mutable NFT on the XRP Ledger. It follows the standard three-phase transactor pattern—`preflight`, `preclaim`, `doApply`—and is one of the smallest transactors in the NFT suite, because nearly all heavy lifting is delegated to `nft::changeTokenURI` in `NFTokenHelpers.cpp`.

## Why This Transactor Exists

NFTs minted on the XRPL carry an immutable `NFTokenID` that encodes their flags, issuer, taxon, and serial number at the bit level (as defined in `protocol/nft.h`). Mutability of the token itself is opt-in: only tokens whose `flagMutable` bit (`0x0010`) was set at mint time can ever have their URI changed. This transaction exists precisely to serve that case — issuers who need to update off-chain metadata (artwork pointers, license files, version hashes) without burning and re-minting the token, at the cost of accepting the trust implications of mutable state.

## `preflight` — Stateless Field Validation

`preflight` runs before any ledger reads, so it only validates the raw transaction fields.

The first check prevents a logical contradiction: if the optional `sfOwner` field is present and equals `sfAccount`, the transaction is malformed. `sfOwner` is only meaningful when a third party (e.g., a marketplace wallet) currently holds the token; submitting it with your own account is redundant and signals a construction error.

The second check guards the optional `sfURI` field. If supplied, the URI must be non-empty and must not exceed `maxTokenURILength`. The design choice to make `sfURI` optional rather than required is deliberate: an issuer who wants to strip the URI entirely (reverting to a bare token with no metadata pointer) submits the transaction without the field. `changeTokenURI` treats `std::nullopt` as "remove the field," and `preflight` never rejects the absent case.

## `preclaim` — Stateful Authorization Checks

`preclaim` has ledger read access and performs three sequential checks against the live ledger state.

**Existence check.** `nft::findToken` scans the owner's `NFTokenPage` directory for the given `NFTokenID`. Failure returns `tecNO_ENTRY`. The owner is resolved here with the same `sfOwner`-or-`sfAccount` fallback used in `doApply`.

**Mutability check.** `nft::getFlags(nftokenID)` extracts the 16-bit flag field packed into the first two bytes of the 256-bit token ID (big-endian, as `nft.h` shows). If `flagMutable` is clear, the token was minted as permanently immutable and no modification is ever permitted — `tecNO_PERMISSION`.

**Issuer/minter authorization check.** The token ID also embeds the issuer's `AccountID` (bytes 4–24, via `nft::getIssuer`). If the transaction sender is not the issuer, the code reads the issuer's account root SLE and checks its optional `sfNFTokenMinter` field. This mirrors the delegation model used by `NFTokenMint`: an issuer can designate a single minter account that acts on their behalf. If the sender matches neither the issuer nor the designated minter, `tecNO_PERMISSION` is returned.

The `tecINTERNAL` path on the issuer account lookup (`// LCOV_EXCL_LINE`) guards against a theoretically impossible state: the issuer address is encoded in the token ID itself and must have existed at mint time, so its account root cannot legitimately be absent. The comment and exclusion from coverage reflect this.

## `doApply` — Ledger Mutation

With all validation passed, `doApply` is minimal:

```cpp
return nft::changeTokenURI(view(), owner, nftokenID, ctx_.tx[~sfURI]);
```

`nft::changeTokenURI` locates the `NFTokenPage` SLE for the owner, finds the specific `STObject` entry in the page's `sfNFTokens` array that matches the token ID, then either sets `sfURI` (if the optional slice is present) or calls `makeFieldAbsent` to remove the field entirely. The page SLE is then marked dirty via `view.update(page)`. If the page or token cannot be found at this stage, `tecINTERNAL` is returned — a redundant guard since `preclaim` already confirmed existence, but present to handle any hypothetical inconsistency introduced by conflicting transactions in the same ledger batch.

## Permission Model and the `sfOwner` Field

The ownership/authorization separation is a recurring pattern in NFT transactors. A token minted by issuer A and later transferred to wallet B is still controlled by A for modification purposes — B owns the token economically but has no authority to change its metadata. When A (or A's designated minter) wants to modify that token, they must include `sfOwner: B` in the transaction to tell the ledger where to find it. Omitting `sfOwner` means "the token is in my own wallet." This is why `preflight` rejects `sfOwner == sfAccount`: the correct way to modify a self-owned token is simply to leave `sfOwner` absent.

## Relationship to Sibling Files

- **`protocol/nft.h`**: Defines `flagMutable`, `getFlags()`, and `getIssuer()` as inline functions operating directly on the binary layout of the `uint256` token ID. `NFTokenModify` uses these to avoid deserializing the full token object just for a permission check.
- **`ledger/helpers/NFTokenHelpers.h`**: Declares `findToken` and `changeTokenURI`. The former is used in `preclaim` (read-only `ReadView`), while the latter requires a writable `ApplyView` and is called only in `doApply`.
- **`NFTokenMint`**: The minter delegation model checked in `preclaim` is the same pattern used during minting — `sfNFTokenMinter` on the issuer's account root grants one account the ability to act as proxy issuer for both creating and modifying tokens.