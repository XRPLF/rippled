# `MPTInvariant.cpp` — MPT Issuance and Payment Invariant Checks

This file implements two invariant checker classes — `ValidMPTIssuance` and `ValidMPTPayment` — that form part of the XRPL ledger's post-apply safety net for Multi-Purpose Tokens (MPTs). Every successful transaction passes through the invariant checking framework before its changes are committed; if any checker's `finalize()` returns `false`, the transaction is rolled back regardless of how it completed. These classes enforce that MPT-related ledger state can only change in ways the protocol explicitly authorizes.

## The `visitEntry` / `finalize` Interface

Both classes conform to the standard invariant checker contract: `visitEntry()` is called once per modified ledger entry (`SLE`) with `before` and `after` snapshots, accumulating summary state in member variables. `finalize()` is then called once after all entries have been visited, receiving the completed transaction, its result code, the fee charged, and the read-only post-apply view. This two-phase structure exists because no single ledger entry carries enough context to validate the operation in isolation; consistency can only be judged globally once all changes are known.

## `ValidMPTIssuance`: Structural Constraints on Issuances and Tokens

`ValidMPTIssuance` tracks four unsigned integer counters (`mptIssuancesCreated_`, `mptIssuancesDeleted_`, `mptokensCreated_`, `mptokensDeleted_`) and one boolean flag (`mptCreatedByIssuer_`). In `visitEntry()`, it counts `ltMPTOKEN_ISSUANCE` and `ltMPTOKEN` SLE changes, and additionally sets `mptCreatedByIssuer_` when a newly-created `ltMPTOKEN` entry's `sfMPTokenIssuanceID` resolves to the same account as the token holder — something that should never happen because an issuer does not hold their own tokens.

`finalize()` dispatches on the transaction's *privilege mask* rather than its type. The `hasPrivilege()` helper (defined in `InvariantCheck.cpp` via an X-macro over `transactions.macro`) maps each `ttXXX` transaction type to a bitmask of `Privilege` enum values. This indirection is deliberate: it decouples invariant logic from the explosive enumeration of transaction types. Adding a new transaction type that creates MPT issuances only requires setting the `createMPTIssuance` privilege flag in the macro table; the invariant logic doesn't change.

The privilege dispatch follows this hierarchy:

- **`createMPTIssuance`**: Exactly one `ltMPTOKEN_ISSUANCE` must be created, none deleted.
- **`destroyMPTIssuance`**: Exactly one deleted, none created.
- **`mustAuthorizeMPT | mayAuthorizeMPT`**: No issuance changes allowed. Token changes are tightly constrained: when a holder submits (no `sfHolder` field in tx), exactly one MPToken must be created or deleted; when the issuer submits, none. AMM-specific transactions (`ttAMM_WITHDRAW`, `ttAMM_CLAWBACK`) are carved out to allow at most one MPToken creation and at most two deletions — reflecting that AMM pool dissolution can remove both token sides simultaneously.
- **`mayCreateMPT`**: No issuances and no deletions; creation is allowed for non-issuers. `ttAMM_CREATE` may create up to two MPTokens (one per asset side), and `ttCHECK_CASH` up to one for the receiver. This is the path that covers payment and offer-crossing flows where MPTokens are auto-created for recipients who don't yet hold the token.
- **`mayDeleteMPT`**: Only deletions, no creations; at most two for `ttAMM_DELETE`.

If none of these privilege branches match and the transaction result is successful (or `tecINCOMPLETE` with `featureMPTokensV2` enabled), any non-zero counter constitutes a violation and `finalize()` returns `false`.

### The Issuer-Token Detection and Graduated Enforcement

The `mptCreatedByIssuer_` flag catches a subtle invariant: an MPToken for the issuer's own issuance must never be created, because the issuer account implicitly "holds" unlimited amounts. The handling here uses the `assert(enforce)` pattern documented in `InvariantCheckPrivilege.h`: when the invariant fires, it logs a fatal message unconditionally, then checks whether the amendment `featureSingleAssetVault` or `featureLendingProtocol` is active. If so, `enforceCreatedByIssuer` is `true`, the `XRPL_ASSERT_PARTS` fires in debug builds, and `finalize()` returns `false`. If neither amendment is active, the assert still fires in debug/test builds (catching developer mistakes early), but the function continues rather than rolling back — a deliberate backward-compatibility concession for the older amendment environment.

## `ValidMPTPayment`: Accounting Conservation

`ValidMPTPayment` enforces the conservation invariant on `OutstandingAmount` for every MPT touched by a transaction:

```
OutstandingAmount_after == OutstandingAmount_before + Σ(MPTAmount_after − MPTAmount_before)
```

The `MPTData` struct, keyed on `MPTID` in a `hash_map`, tracks the before/after `sfOutstandingAmount` from `ltMPTOKEN_ISSUANCE` entries and accumulates a signed net delta (`mptAmount`) from all `ltMPTOKEN` entries. Each MPToken's contribution is `sfMPTAmount + sfLockedAmount`, since locked amounts remain part of the outstanding supply.

Because `sfMPTAmount` is a 64-bit quantity and `maxMPTokenAmount` is `0x7FFF'FFFF'FFFF'FFFF` (the maximum representable signed 63-bit value), overflow is a real concern. `visitEntry()` guards against it in two ways: it checks each individual token value against `maxMPTokenAmount`, and it checks whether the token's combined amount (`mptAmt + lockedAmt`) would overflow. If either condition fires, `overflow_` is set and all further processing is skipped for that transaction.

`finalize()` then performs a second-level overflow check on the signed delta arithmetic itself before comparing `outstanding[After]` to `outstanding[Before] + mptAmount`. This double-check is necessary because the accumulated delta is a signed 64-bit integer and can itself wrap. Overflow failures return `!enforce` — soft-failing (returning `true`) when `featureMPTokensV2` is not yet active, hard-failing when it is.

## Design Observations

The file is deliberately conservative about what it checks at the `visitEntry()` phase. The per-entry visitor accumulates only primitive counts and deltas; no conclusions are drawn. This is important for correctness: ledger entries may appear in any order within a transaction's change set, and committing to a judgment mid-stream would produce false positives.

The `tecINCOMPLETE` result code exemption in `ValidMPTIssuance::finalize()` reflects a protocol extension: with `featureMPTokensV2`, certain operations can succeed partially (returning `tecINCOMPLETE` rather than `tesSUCCESS`) while still making valid ledger changes that must pass invariant checks.

The privilege mask system pays dividends when reading the code: the `finalize()` logic never needs a switch on transaction type inside each privilege branch, keeping the combinatorial complexity under control even as the set of MPT-capable transactions grows.