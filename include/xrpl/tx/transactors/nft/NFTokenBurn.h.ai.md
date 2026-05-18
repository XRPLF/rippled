# `NFTokenBurn.h` — NFT Burn Transactor Declaration

## Role in the System

`NFTokenBurn.h` declares the `NFTokenBurn` transactor, which handles the `NFTokenBurn` transaction type on the XRP Ledger. Its sole responsibility is to permanently destroy an NFT — removing it from the ledger state, cancelling any outstanding offers associated with it, and bookkeeping the issuer's burned-token count. It sits in the `xrpl/tx/transactors/nft/` directory alongside the other NFT-lifecycle transactors (`NFTokenMint`, `NFTokenCreateOffer`, `NFTokenAcceptOffer`, etc.) and is a direct subclass of `Transactor`.

## Class Design and Three-Phase Processing

Like every transactor in the XRPL framework, `NFTokenBurn` participates in a strict three-phase pipeline: `preflight` → `preclaim` → `doApply`. The base class `Transactor` drives this via its `invokePreflight<T>` template and `operator()`, using compile-time polymorphism through name hiding (not virtual dispatch) for the static phases.

`ConsequencesFactory` is set to `Normal`, meaning this transaction has standard fee and sequence-number consequences — it does not block later transactions from the same account (`Blocker`) and doesn't require custom consequence computation (`Custom`). This is appropriate because a burn has no unusual side-effects on the sender's transaction queue.

The constructor simply forwards `ApplyContext&` to the `Transactor` base, as is standard for the NFT transactor family.

## `preflight` — Intentionally Trivial

The `preflight` implementation returns `tesSUCCESS` unconditionally. This is deliberate: there are no transaction-field-level validity checks specific to an NFT burn that need to run stateless (before ledger access). Generic checks such as fee validation, sequence number format, and signature key well-formedness are handled by `preflight1` and `preflight2` inside `invokePreflight`, so `NFTokenBurn::preflight` has nothing left to do. Comparing this to `NFTokenMint`, which overrides `checkExtraFeatures` and `getFlagsMask` to enforce amendment gates and flag constraints, a burn carries no such concerns.

## `preclaim` — Authorization Enforcement

The substantive validation happens in `preclaim`, which has read-only access to the ledger. Two checks are performed:

**Token existence:** `nft::findToken` searches the owner's NFToken page for the specific `NFTokenID`. If absent, `tecNO_ENTRY` is returned — the fee is still consumed, but the transaction fails gracefully without modifying state.

**Permission logic:** The owner of a token can always burn it. If the transaction sender (`sfAccount`) differs from the token's current holder (`sfOwner` field, if present), the code checks whether the `flagBurnable` bit is set in the token's embedded flags. Without it, `tecNO_PERMISSION` is returned. Even with `flagBurnable` set, only the issuer — or an account designated as the issuer's `sfNFTokenMinter` — may destroy a token they do not own. This layered check reflects the real-world analogy: a token's creator can reserve the right to recall or destroy tokens they issued, but cannot unilaterally destroy tokens belonging to arbitrary parties without the `flagBurnable` flag being encoded at mint time (making the rule immutable post-issuance).

## `doApply` — State Mutation

`doApply` commits the burn in three steps:

1. **Token removal:** `nft::removeToken` strips the NFToken from its owner's NFTokenPage SLE. A post-condition assert guards against the case where the token disappeared between `preclaim` and `doApply` (which should be impossible under normal consensus, hence the comment "should never happen").

2. **Issuer accounting:** The issuer account's `sfBurnedNFTokens` counter is incremented. This field is optional (`~sfBurnedNFTokens`), so `value_or(0)` seeds the count if this is the first token burned by that issuer. This counter is informational ledger state that wallets and explorers can surface.

3. **Offer cleanup:** All sell and buy offers keyed to the now-destroyed `NFTokenID` are deleted, subject to a hard cap of `maxDeletableTokenOfferEntries` (500 total). Sell offers are prioritized over buy offers, since the sell-offer directory is expected to be smaller and its cleanup frees more critical ledger resources. Any remaining budget after sell-offer deletion is applied to buy offers. This bounded cleanup is a deliberate performance contract: a burn transaction cannot be weaponized to consume unbounded computation by accumulating thousands of offers against a token, and any offers beyond the cap simply remain in the ledger until other cleanup mechanisms handle them.

## Relationship to Other Files

The header depends only on `<xrpl/tx/Transactor.h>`, keeping the interface minimal. The implementation (`NFTokenBurn.cpp`) pulls in `NFTokenHelpers.h` for `nft::findToken`, `nft::removeToken`, `nft::removeTokenOffersWithLimit`, `nft::getFlags`, and `nft::getIssuer` — all ledger-level helpers that abstract the NFTokenPage data structure. Compared to sibling transactors, `NFTokenBurn` is notably lean: it does not override `checkExtraFeatures` (so the amendment gate is handled by the `Permission` registry lookup in `invokePreflight`) and does not override `getFlagsMask` (there are no burn-specific transaction flags).