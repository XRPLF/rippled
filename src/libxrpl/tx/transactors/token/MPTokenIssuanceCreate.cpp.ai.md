# `MPTokenIssuanceCreate.cpp`

## Role in the System

This file implements the `MPTokenIssuanceCreate` transactor — the entry point for creating a new Multi-Purpose Token (MPT) issuance on the XRP Ledger. An MPT issuance is the on-ledger configuration object that defines a fungible token's parameters: its supply cap, transfer fee, authorization policy, permissioned-domain membership, and optional metadata. Every MPT in circulation traces back to an issuance record created by this transactor.

The file fits within the standard XRPL transactor architecture: `MPTokenIssuanceCreate` inherits from `Transactor` and implements the three hooks the framework calls during transaction processing — `checkExtraFeatures`, `preflight`, and `doApply`. A fourth method, the static `create()` factory, is architecturally notable because it decouples ledger-entry construction from the transaction context, allowing it to be reused by other transactors.

## Validation in Two Stages

XRPL validation is split between stateless preflight (no ledger access, called potentially multiple times) and stateful apply (has write access to the view). `MPTokenIssuanceCreate` applies an additional pre-preflight hook, `checkExtraFeatures`, that gates certain fields on the presence of protocol amendments:

- `sfDomainID` requires both `featurePermissionedDomains` **and** `featureSingleAssetVault` to be active.
- `sfMutableFlags` requires `featureDynamicMPT`.

Returning `false` from `checkExtraFeatures` prevents the transaction from proceeding at all; this is the correct place for amendment gating because it runs before the main flag-mask check.

`getFlagsMask()` returns `tfMPTokenIssuanceCreateMask`, which the framework passes to `preflight1()` to reject any unknown transaction flags before the transactor's own `preflight` is reached.

`preflight` then enforces field-level business rules:

1. **`sfMutableFlags`**: If present, the value must be nonzero and must not have bits set outside `tmfMPTokenIssuanceCreateMutableMask`. The check is inverted (`(*mutableFlags & mask) != 0u` means bits outside the mask are set), which is a common defensive pattern in this codebase to catch callers who set reserved bits.

2. **`sfTransferFee`**: Capped at `maxTransferFee` (50,000 basis points = 50%). A non-zero fee is only meaningful when the token is transferable, so a non-zero fee without `tfMPTCanTransfer` returns `temMALFORMED` — the combination is incoherent by protocol design.

3. **`sfDomainID`**: Must not be `beast::zero` (an all-zeros sentinel) and mandates `tfMPTRequireAuth`. The rationale is that a domain-scoped issuance restricts which holders are eligible, which only makes sense when the issuer controls authorization.

4. **`sfMPTokenMetadata`**: Non-empty, bounded to `maxMPTokenMetadataLength` (1024 bytes).

5. **`sfMaximumAmount`**: Must be positive and within `maxMPTokenAmount` (0x7FFF_FFFF_FFFF_FFFF — a signed 63-bit maximum). This ceiling exists to ensure MPT amounts fit within the XRPL `Number` type's representable range.

## The `create()` Factory

The static `create(ApplyView&, beast::Journal, MPTCreateArgs const&)` method is the unit of reuse. Its return type, `Expected<MPTID, TER>`, follows the value-or-error pattern used across newer XRPL code: on success it carries the freshly minted `MPTID`; on failure it wraps a `TER` in `Unexpected`. This avoids out-parameters and makes it impossible for a caller to use the ID without checking for an error first.

`MPTCreateArgs` bundles all optional fields into a single aggregate. `priorBalance` is `std::optional<XRPAmount>` — when present, the reserve check is performed; when `std::nullopt`, the check is skipped. This opt-out is intentional: `VaultCreate` calls `create()` on a pseudo-account that is freshly created within the same transaction and has no meaningful pre-fee balance, so it passes `std::nullopt` to bypass the reserve gate.

Inside `create()`:

1. The issuer's account SLE is peeked (write-access) for the owner-count update. A missing account returns `tecINTERNAL`, which is marked `LCOV_EXCL_LINE` because a valid transaction reaching apply phase always has its account in the ledger.

2. If `priorBalance` is set, the reserve for `ownerCount + 1` is checked before any ledger mutation, returning `tecINSUFFICIENT_RESERVE` early.

3. The `MPTID` is computed via `makeMptID(args.sequence, args.account)` — a deterministic 192-bit identifier derived from the issuer's account ID and the transaction sequence number. Because sequence numbers are monotonically increasing per account, collisions are impossible under normal ledger operation.

4. The new `SLE` is built under `keylet::mptIssuance(mptId)`. Flags are stored as `args.flags & ~tfUniversal`, stripping the universal transaction flags that are not meaningful as ledger-entry flags.

5. `sfOutstandingAmount` is initialized to 0 — the canonical starting point for a newly created issuance.

6. All optional fields (`sfMaximumAmount`, `sfAssetScale`, `sfTransferFee`, `sfMPTokenMetadata`, `sfDomainID`, `sfMutableFlags`) are written only when present in `args`, keeping the SLE sparse.

7. The issuance is inserted into the owner's directory (`keylet::ownerDir`). A full directory returns `tecDIR_FULL`, also excluded from coverage as a theoretical edge case.

8. `adjustOwnerCount` increments the account's `sfOwnerCount` by 1, which both raises the reserve threshold and protects the issuance from being garbage-collected until explicitly destroyed.

## Reuse by `VaultCreate`

`VaultCreate` calls `MPTokenIssuanceCreate::create()` directly to mint the share token that vault depositors receive. It issues the MPT from a newly-created pseudo-account at sequence 1, with `priorBalance = std::nullopt` (no reserve check), and maps vault-level flags (`tfVaultShareNonTransferable`, `tfVaultPrivate`) to the corresponding MPT flags. This reuse is the primary reason `create()` is a free static method rather than implemented inline in `doApply`.

## Symmetry with `MPTokenIssuanceDestroy`

`MPTokenIssuanceDestroy` is the mirror image: it validates that no outstanding or locked balance exists, removes the SLE from the owner directory, erases it, and decrements `sfOwnerCount`. The `sfOutstandingAmount` initialized to 0 in `create()` is exactly the condition `MPTokenIssuanceDestroy::preclaim` checks — the ledger enforces that a token with circulating supply cannot be destroyed.