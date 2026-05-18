# `NFTokenModify.h` — NFT Modification Transactor

## Role and Purpose

`NFTokenModify.h` declares the `NFTokenModify` transactor, which handles the `NFTokenModify` ledger transaction — a purpose-built operation for updating the URI metadata of an existing, mutable NFT on the XRP Ledger. It sits alongside the other NFT transactors (`NFTokenMint`, `NFTokenBurn`, `NFTokenCreateOffer`, `NFTokenCancelOffer`, `NFTokenAcceptOffer`) in the `include/xrpl/tx/transactors/nft/` module, and follows the identical structural pattern used across all XRPL transaction types.

## Class Design and Inheritance

`NFTokenModify` inherits from `Transactor`, the abstract base class for all transaction processors in the XRPL codebase. `Transactor` enforces a three-phase processing pipeline:

1. **`preflight`** — stateless, context-free validation run before even consulting the ledger.
2. **`preclaim`** — read-only ledger checks to determine whether the transaction can claim a fee.
3. **`doApply`** — the mutating phase that writes final state changes to the ledger.

This pattern is enforced structurally: `preflight` and `preclaim` are `static` methods (they operate purely on their context argument, not on any instance state), while `doApply` is a virtual override that gains access to the mutable `ApplyView` through the inherited `ctx_` member. The constructor simply delegates to `Transactor(ctx)`, which is the norm for this family.

## `ConsequencesFactory{Normal}`

The `static constexpr ConsequencesFactoryType ConsequencesFactory{Normal}` declaration tells the fee-claiming machinery that this transaction behaves in the standard way: it always claims its fee, and it neither blocks the account's sequence number queue (`Blocker`) nor computes custom consequences (`Custom`). For a URI modification that touches only the NFT page, this is exactly right.

## What the Three Phases Do

**`preflight`** performs two pure-data checks without touching ledger state:

- If an `sfOwner` field is present and equals `sfAccount` (the transaction sender), the transaction is malformed. This guards against a degenerate form where a user redundantly specifies themselves as the owner; the field is only meaningful when a delegated minter is modifying an NFT they minted on behalf of the actual owner.
- If `sfURI` is present, it must be non-empty and no longer than `maxTokenURILength` (256 bytes as defined in `Protocol.h`). A present-but-empty URI would be ambiguous — is the caller trying to clear the URI or submitting a bug? The design forbids it. Omitting `sfURI` entirely remains the valid signal for a no-URI-change call.

**`preclaim`** reads ledger state but does not modify it:

- It resolves the effective owner: `sfOwner` if present, otherwise `sfAccount`.
- `nft::findToken` confirms the NFT exists in that owner's token directory; absent NFTs return `tecNO_ENTRY`.
- The NFT's `flagMutable` bit (bit 4 of the packed flags within the 256-bit token ID) is checked. This is critical: NFT immutability is an at-mint, permanent choice encoded directly into the token ID itself. If the flag is not set, `tecNO_PERMISSION` is returned immediately — no modification is possible.
- If the caller is not the original issuer (extracted from the token ID via `nft::getIssuer`), `preclaim` checks whether the `sfNFTokenMinter` field on the issuer's account SLE points to the caller. Only a designated minter may act as a proxy modifier. Any other account gets `tecNO_PERMISSION`.

**`doApply`** is the state-mutation step. It resolves the owner (same `sfOwner`-or-`sfAccount` logic as `preclaim`) and delegates all ledger work to `nft::changeTokenURI(view(), owner, nftokenID, ctx_.tx[~sfURI])`, which navigates NFToken pages and writes the updated URI in place.

## Relationship to NFT Protocol Helpers

The implementation imports `<xrpl/ledger/helpers/NFTokenHelpers.h>` and `<xrpl/protocol/nft.h>`. The helper `nft::findToken` and `nft::changeTokenURI` encapsulate the complex NFToken page navigation — NFTs are packed into shared ledger objects (`NFTokenPage` SLEs) sorted by token ID, and mutating them requires locating the right page and entry. By confining that logic to `NFTokenHelpers`, `NFTokenModify` stays focused on authorization and field validation, not data structure traversal.

The `nft::getFlags` and `nft::getIssuer` functions decode fields directly from the 256-bit token ID, which encodes the issuer account ID, taxon, sequence number, and flags at fixed offsets. Permissions are therefore enforced without needing any separate on-ledger permission object — the token ID itself is the source of truth for mutability and issuer identity.

## Absence of `checkExtraFeatures` and `getFlagsMask`

Unlike `NFTokenMint`, which overrides both `checkExtraFeatures` (to gate on an amendment) and `getFlagsMask` (to declare supported transaction flags), `NFTokenModify` omits both. It inherits the base `Transactor` defaults: `checkExtraFeatures` returns `true` unconditionally, and `getFlagsMask` returns `tfUniversalMask`. This is appropriate for a transaction that defines no custom per-invocation flags and whose amendment gating (if any) is handled by the global permission registry checked in `invokePreflight` before the per-transactor hooks are called.