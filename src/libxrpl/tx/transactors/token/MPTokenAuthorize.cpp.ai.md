# `MPTokenAuthorize.cpp` — MPT Holder Opt-In and Issuer Allowlist Transactor

This file implements the `MPTokenAuthorize` transactor, which handles all authorization state transitions for Multi-Purpose Tokens (MPTs) on the XRP Ledger. It covers four distinct operations in a single transaction type: a holder opting in to receive a token (creating an `MPToken` SLE), a holder opting out and deleting their `MPToken` SLE, an issuer granting explicit allowlist access to a holder, and an issuer revoking that access. The presence or absence of the optional `sfHolder` field in the transaction is the sole signal that determines which of these two roles the submitter is playing.

## The Dual-Role Architecture

The most architecturally significant choice in this file is that a single transaction type serves both holders and issuers. The `sfHolder` field acts as the pivot: when absent, `ctx.tx[~sfHolder]` evaluates to a falsy optional and the submitter is treated as the holder acting on their own behalf. When present, the submitter must be the token's issuer and is managing the allowlist for the named account. This avoids introducing two separate transaction types that would share almost all their validation logic, but it does mean `preclaim` branches on this early and the two paths share virtually no code below that branch point.

The `preflight` guard is minimal but essential: it rejects the case where `sfAccount` equals `~sfHolder`. This blocks an issuer from mistakenly naming themselves as the holder they want to authorize, which would be undefined behavior in the preclaim logic that assumes account and holderID are always distinct.

## `preclaim`: Diverging Validation Paths

### Holder Path (no `sfHolder`)

When a holder is the submitter, the first thing `preclaim` checks is whether the transaction carries the `tfMPTUnauthorize` flag, because there is an important ordering constraint: a holder may delete their `MPToken` object even after the parent `MPTokenIssuance` has been destroyed. This edge case arises when all balances reach zero before the issuer destroys the issuance, and then the outstanding zero-balance `MPToken` objects need to be cleaned up afterwards. By checking the unauthorize flag before attempting to read the issuance, the code avoids a spurious `tecOBJECT_NOT_FOUND` in that cleanup scenario.

Deleting a holder's `MPToken` requires two separate zero-balance checks — `sfMPTAmount` must be zero and the optional `~sfLockedAmount` must also be zero (or absent). The double check guards against tokens that have a zero net balance but still carry locked amounts from active escrows or vault operations. Returning `tecHAS_OBLIGATIONS` from both is consistent, but note the asymmetry: if the `MPToken` doesn't exist at all during an unauthorize attempt, the error is `tecOBJECT_NOT_FOUND`, not `tecHAS_OBLIGATIONS`. There is also a `featureSingleAssetVault`-gated check against the `lsfMPTLocked` flag on the `MPToken` SLE itself; a holder cannot remove an MPT that is currently locked by a vault.

When the holder wants to create their `MPToken` (opt in), the issuance must exist, the submitter cannot be the issuer themselves, and the `MPToken` must not already exist (guards against `tecDUPLICATE`).

### Issuer Path (with `sfHolder`)

When `sfHolder` is present, the submitter is asserting they are the issuer. `preclaim` validates this in sequence: the named holder account must exist on the ledger, the `MPTokenIssuance` must exist, and the submitter's account must actually match the `sfIssuer` field on the issuance. If the issuance doesn't have `lsfMPTRequireAuth` set, issuer-side authorization is meaningless and `tecNO_AUTH` is returned — the issuer can only manage an allowlist if the token type was configured at creation to require one.

A subtle guard prevents pseudo-accounts (vault pseudo-accounts and loan-broker pseudo-accounts, identified by the presence of `sfVaultID` or `sfLoanBrokerID` on the account root) from being unauthorized by an issuer. Pseudo-accounts are implicitly always authorized because they exist as protocol-controlled entities, not user-controlled accounts. The comment explicitly notes that no amendment gate is needed here because such accounts can only exist if the `featureSingleAssetVault` amendment is enabled anyway.

## `doApply`: Thin Delegation to `authorizeMPToken`

`doApply()` is a single delegating call to `authorizeMPToken()` in `MPTokenHelpers.cpp`, passing through the transaction flags, the `preFeeBalance_` (the pre-fee XRP balance captured by the base `Transactor` class before the fee is consumed), and the optional `sfHolder`. All actual ledger mutations happen in that helper, which is intentionally shared: `addEmptyHolding()` and `removeEmptyHolding()` in the same helpers file both call `authorizeMPToken()` to reuse the create and delete paths.

What `authorizeMPToken` does for each operation:
- **Holder create**: Computes the reserve requirement (with a two-item exemption that mirrors trust line behavior — the first two owned items do not increase the reserve), creates the `MPToken` SLE with zero amounts and cleared flags, links it into the holder's owner directory via `dirLink`, and increments the owner count.
- **Holder delete**: Removes the `MPToken` from the owner directory with `dirRemove`, decrements the owner count, and erases the SLE from the ledger view.
- **Issuer authorize**: Reads the holder's `MPToken` SLE and sets the `lsfMPTAuthorized` flag.
- **Issuer unauthorize**: Reads the holder's `MPToken` SLE and clears the `lsfMPTAuthorized` flag.

By the time `doApply` runs, all invariants have been established in `preclaim`, so `authorizeMPToken` treats any precondition failures (missing SLE after passing preclaim) as `tecINTERNAL` or `UNREACHABLE`, annotated with `LCOV_EXCL_LINE` since they cannot be reached through valid transaction processing.

## Relationship to the Broader MPT System

`MPTokenAuthorize` sits alongside `MPTokenIssuanceCreate`, `MPTokenIssuanceDestroy`, `MPTokenIssuanceSet`, and `Clawback` in the token transactor directory. Together they manage the full lifecycle of MPTs. The authorization state managed here — the `lsfMPTAuthorized` flag and the very existence of the `MPToken` SLE — is later consulted by `requireAuth()` in `MPTokenHelpers` during payment processing to ensure holders have opted in and, when required, have been explicitly allowlisted by the issuer before any transfer can occur.