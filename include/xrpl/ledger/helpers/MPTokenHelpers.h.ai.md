# `include/xrpl/ledger/helpers/MPTokenHelpers.h`

This header declares the MPT-specific layer of the XRPL token helper system. It sits directly below the generic `TokenHelpers.h`, which provides `Asset`-polymorphic dispatchers covering both IOU trust-line tokens and MPTs. The functions here implement the MPT side of that contract and also expose several operations that have no IOU equivalent: escrow mechanics, DEX permission checks, overflow arithmetic, and the two-phase authorization protocol that is central to MPT's design.

`MPTIssue` — the type passed to almost every function — wraps a 192-bit `MPTID` that encodes the issuing account's sequence number and account ID, so the issuer identity can always be recovered from the token identifier without a separate ledger lookup.

## Freeze Checking

Three freeze predicates are exported at increasing levels of granularity. `isGlobalFrozen` reads the `MPTokenIssuance` SLE and tests `lsfMPTLocked`. `isIndividualFrozen` reads the per-holder `MPToken` SLE and tests the same flag on that object. `isFrozen` ORs both, and then additionally calls an internal `isVaultPseudoAccountFrozen` helper that descends into any vault the holder's account belongs to. The `depth` parameter on `isFrozen` and `isAnyFrozen` guards against infinite recursion when a vault holds MPT shares backed by another vault — a configuration the protocol currently forbids, but the code defends against anyway up to `maxAssetCheckDepth`.

## Transfer Rate

`transferRate(ReadView const&, MPTID const&)` reads `sfTransferFee` from the issuance SLE and converts it to the same `Rate` type used by IOU trust lines. The encoding: XRPL rates use a 1 000 000 000 base (parity = no fee), and the MPT `sfTransferFee` field is in units of 0.001%, so the formula `1,000,000,000 + 10,000 × fee` maps 0→parity, 50,000→2,000,000,000 (100% surcharge, i.e. 50% transfer fee on the gross). If the field is absent, `parityRate` is returned.

## Holding Lifecycle

`canAddHolding` performs a dry-run guard: it returns `tecOBJECT_NOT_FOUND` if the issuance does not exist, and `tecNO_AUTH` if `lsfMPTCanTransfer` is not set, which is the flag that controls whether anyone other than the issuer may hold the token.

`addEmptyHolding` allocates a new `MPToken` SLE for the holder. For the issuer's own account it short-circuits to `tesSUCCESS` — issuers do not hold `MPToken` objects. For everyone else it delegates to `authorizeMPToken` (the creation path). The required XRP reserve follows the same rule as trust lines: the first two owner-directory entries are exempt, after which `accountReserve(ownerCount + 1)` is enforced against `priorBalance`.

`removeEmptyHolding` requires the `MPToken`'s `sfMPTAmount` to be zero (and `sfLockedAmount` to be zero once `fixSecurity3_1_3` is enabled). It then calls `authorizeMPToken` with `tfMPTUnauthorize` to erase the object and remove it from the owner directory.

## Authorization: `requireAuth` and `enforceMPTokenAuthorization`

The split between these two functions mirrors XRPL's transaction pipeline division into `preclaim` (read-only feasibility check) and `doApply` (state-writing execution).

`requireAuth` is the read-only check. It returns `tesSUCCESS` immediately for the issuer. For other accounts it supports two authorization modes. In the classic mode (`lsfMPTRequireAuth` set, no `sfDomainID`), the holder must have an `MPToken` SLE with `lsfMPTAuthorized` set by the issuer. In the domain-based mode (`sfDomainID` present), authorization comes from the holder's on-chain credentials being verified against that domain via `credentials::validDomain`. When the `featureSingleAssetVault` amendment is active, vault and loan-broker pseudo-accounts are always implicitly authorized, and the check recurses into the vault's underlying asset — descending through `depth + 1` each time.

`enforceMPTokenAuthorization` runs during `doApply`. Its key responsibility beyond re-checking is *creating the `MPToken` SLE on-the-fly* when a domain-authorized holder does not yet have one — the caller provides `priorBalance` precisely so this lazy allocation can enforce the XRP reserve. It also handles the case where credentials have expired between `preclaim` and `doApply` by running `verifyValidDomain` (which deletes expired credentials as a side effect) and returning `tecEXPIRED` when appropriate.

## Escrow Mechanics

`lockEscrowMPT` moves a token amount from live to escrowed state. It decrements `sfMPTAmount` on the sender's `MPToken` and increments `sfLockedAmount` on both that object and the `MPTokenIssuance`. Critically, `sfOutstandingAmount` on the issuance does not change — escrowed tokens remain outstanding because the recipient has not yet received them. Underflow and overflow are checked defensively at each step.

`unlockEscrowMPT` completes the escrow. It receives both `grossAmount` (what was locked) and `netAmount` (what the recipient gets after fees). The locked counters on the sender's `MPToken` and the issuance are decremented by `grossAmount`. The receiver's balance is credited separately by the payment machinery. If `grossAmount > netAmount` (a fee was charged), the difference is subtracted from `sfOutstandingAmount` on the issuance — the fee is effectively burned.

## Overflow Arithmetic

`isMPTOverflow` encodes a deliberate two-threshold strategy. For direct sends that bypass the payment engine (`AllowMPTOverflow::No`), it enforces the strict rule `OutstandingAmount + sendAmount ≤ MaximumAmount`. For sends through the payment engine (`AllowMPTOverflow::Yes`), the effective ceiling is raised to `UINT64_MAX`. This relaxation exists because offer-crossing paths can temporarily create an issuing step (outbound from the issuer) before a matching redemption step collapses the overflow in the same transaction. The comment in the header documents this explicitly.

`availableMPTAmount` is a thin wrapper that computes `MaximumAmount − OutstandingAmount` from the SLE; the comment acknowledges that `OutstandingAmount` can transiently exceed `MaximumAmount`, making the result signed and potentially negative — callers must handle that.

## DEX Integration

`canTransfer` allows a transfer only if `lsfMPTCanTransfer` is set, or if at least one party is the issuer. `canTrade` guards DEX use by checking `lsfMPTCanTrade` on the issuance. `checkMPTTxAllowed` is the comprehensive gating function called by DEX and payment transaction types; it verifies that the issuance exists, is not globally locked, has the `lsfMPTCanTrade` flag, and (for non-issuer accounts) has `lsfMPTCanTransfer` set and the account's own `MPToken` is not individually locked.

`issuerFundsToSelfIssue` and `issuerSelfDebitHookMPT` handle the tracking needed when an issuer owns an MPT sell offer on the DEX. During an issuing step the issuer's "available" balance is determined by how much more they can issue (i.e., `availableMPTAmount`), and the debit hook records how much has been sold in that step so the payment engine can accurately balance the final settlement.