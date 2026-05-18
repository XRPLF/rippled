# PermissionedDomainSet.cpp

## Role and Purpose

`PermissionedDomainSet` is the transactor responsible for both creating new Permissioned Domain objects and modifying existing ones on the XRP Ledger. A Permissioned Domain is a named access-control construct that specifies a list of accepted credential types — each identified by an issuer account and a `CredentialType` blob — that define which credentials grant access to the domain. This file implements the full lifecycle of the "set" operation: feature gating, stateless field validation, stateful ledger consistency checks, and final ledger mutation.

## Transaction Duality: Create vs. Modify

The most architecturally significant aspect of this transactor is that a single transaction type handles both creation and update. The discriminator is the optional `sfDomainID` field. When it is absent, `doApply()` creates a new domain object; when it is present, the existing domain is fetched and its `sfAcceptedCredentials` array is replaced wholesale. There is no partial update — callers must always submit the complete desired credential list.

This design choice avoids a separate `PermissionedDomainUpdate` transaction type at the cost of slightly more complex validation, since `sfDomainID` is optional and its presence or absence must be handled consistently across `preflight`, `preclaim`, and `doApply`.

## Validation Pipeline

`checkExtraFeatures` acts as an early gate, returning false unless the `featureCredentials` amendment is active. The XRPL transactor framework calls this before anything else, preventing the transaction from even reaching preflight on networks that haven't enabled the feature.

`preflight` is purely stateless and validates the transaction's fields in isolation. It delegates the bulk of credential array validation to `credentials::checkArray`, which enforces that the array is non-empty, does not exceed `maxPermissionedDomainCredentialsArraySize` (10 entries), each entry has a valid non-empty `sfCredentialType` within the allowed length, and there are no duplicate `(issuer, credentialType)` pairs (detected via a hash-set on `sha512Half(issuer, credentialType)`). The only additional check `preflight` adds is rejecting a `sfDomainID` of `beast::zero` — a structurally valid but semantically nonsensical identifier.

`preclaim` performs all ledger-state-dependent checks. It verifies each credential issuer account actually exists in the current ledger view (returning `tecNO_ISSUER` if not), and when `sfDomainID` is present, it confirms both that the domain object exists (`tecNO_ENTRY`) and that the submitting account owns it (`tecNO_PERMISSION`). The account existence check for `sfAccount` itself returns `tefINTERNAL` — a code indicating an impossible condition that should have been caught upstream, hence the `LCOV_EXCL_LINE` annotations.

## Applying the Transaction

`doApply()` begins by sorting the submitted credentials through `credentials::makeSorted`, which returns a `std::set<std::pair<AccountID, Slice>>`. Using an ordered set provides canonical, deterministic ordering for the `sfAcceptedCredentials` array stored in the ledger, making the ledger entry independent of submission order. The sorted pairs are then serialized into an `STArray` of `sfCredential` inner objects.

For **modifications**, the flow is minimal: peek the existing `SLE`, replace its `sfAcceptedCredentials` field in-place, and call `view().update()`. No reserve check is needed because the object already exists and is already counted against the owner's reserve.

For **creation**, the flow is richer. A reserve check ensures the account's XRP balance covers `fees().accountReserve(ownerCount + 1)` before any state is mutated, returning `tecINSUFFICIENT_RESERVE` if not. The domain's `Keylet` is derived from `keylet::permissionedDomain(account, sequence)` — using the transaction's `sfSequence` as the unique disambiguator, which ensures each domain gets a globally unique, collision-resistant identifier tied to the submitter and their transaction sequence. The new `SLE` is populated with the owner account, sequence number, and sorted credentials, inserted into the account's owner directory via `view().dirInsert()`, and finally `adjustOwnerCount()` increments the owner's object count to reflect the new reserve obligation.

## Error Handling and Invariants

The `tefINTERNAL` and `tecDIR_FULL` branches are all marked `LCOV_EXCL_LINE` because they represent conditions the framework guarantees won't occur under normal operation (the account was verified to exist in `preclaim`, and owner directories are extremely rarely truly full). This is a deliberate defensiveness pattern: keep the invariant checks in `doApply` to catch any future code path that bypasses `preclaim`, but don't burden test coverage requirements with untriggerable branches.

The separation between `tecNO_ISSUER` (checked at claim time with ledger access) and the `credentials::checkArray` structural checks (done at preflight without ledger access) follows the general XRPL transactor principle: fail cheap and stateless first, then fail with full ledger context.

## Relationship to Sibling Files

`PermissionedDomainDelete.cpp` in the same directory is the counterpart transactor. It handles the teardown: removes the domain from the owner directory, calls `adjustOwnerCount(..., -1, ...)` to release the reserve slot, and erases the `SLE`. The asymmetry worth noting is that `PermissionedDomainSet` is responsible for both creation and update, while delete is isolated in its own transactor — a typical XRPL convention where "set" covers the full write lifecycle and "delete" is a separate, explicit act.