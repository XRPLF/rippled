# `MPTokenAuthorize.h` — Multi-Purpose Token Authorization Transactor

This header declares the `MPTokenAuthorize` transactor and its companion argument struct, together forming the entry point for the `MPTokenAuthorize` transaction (type `ttMPTOKEN_AUTHORIZE`, opcode 57) on the XRP Ledger. The transaction is gated behind the `featureMPTokensV1` amendment and handles the bidirectional authorization handshake that governs who may hold a given Multi-Purpose Token (MPT) issuance.

## Why This Transactor Exists

MPTs introduce a more structured ownership model than XRPL's older trust lines. An issuance marked with `lsfMPTRequireAuth` demands explicit issuer approval before a holder can receive tokens — a pattern used for regulated or permissioned assets. At the same time, any account that wants to hold an MPT (regardless of `RequireAuth`) must first opt in by creating an `MPToken` ledger object. The `MPTokenAuthorize` transaction serves both sides of this two-step handshake through a single transaction type, with the optional `sfHolder` field acting as the discriminator between the holder role and the issuer role.

## `MPTAuthorizeArgs`

`MPTAuthorizeArgs` is a plain aggregation struct that bundles the inputs required for the core authorization logic implemented in `authorizeMPToken()` (declared in `MPTokenHelpers.h`). It holds:

- `priorBalance` — the submitting account's XRP balance before fees, used to check reserve requirements when creating a new `MPToken` object.
- `mptIssuanceID` — the 192-bit identifier of the target MPT issuance.
- `account` — the account that submitted the transaction.
- `flags` — transaction flags, most importantly `tfMPTUnauthorize`.
- `holderID` — present only when the issuer is acting on behalf of a specific holder.

This struct exists so that `authorizeMPToken()` can be called from contexts other than the transactor itself — for example, vault creation code in `VaultCreate.cpp` and `VaultDeposit.cpp` reuse the same helper to implicitly authorize vault pseudo-accounts without going through a full transaction. Separating the arguments into a struct keeps the helper callable with named fields rather than a positional argument list.

## `MPTokenAuthorize` Class

`MPTokenAuthorize` inherits from `Transactor` and implements the standard three-phase XRPL transaction lifecycle: `preflight` → `preclaim` → `doApply`.

**`getFlagsMask()`** returns `tfMPTokenAuthorizeMask`, which tells the base framework's `preflight1()` which flag bits are valid for this transaction type. This is a static method resolved at compile time through name hiding (not virtual dispatch) — the `invokePreflight<T>()` template in `Transactor.h` calls `T::getFlagsMask()` directly.

**`preflight()`** is intentionally minimal — it only rejects the degenerate case where `sfAccount == sfHolder` (an account authorizing itself is nonsensical and likely a client bug). All amendment feature checks are handled upstream by `invokePreflight<T>()` via `Permission::getInstance().getTxFeature()`, so `preflight()` itself does not need to touch amendment state.

**`preclaim()`** is where the meaningful validation happens against ledger state (after signature verification but before fee consumption). It branches on whether `sfHolder` is present:

- **No `sfHolder` — holder path**: The submitting account is acting on its own behalf. If `tfMPTUnauthorize` is set, it attempts to delete the existing `MPToken` object; this is blocked if `sfMPTAmount` or `sfLockedAmount` is non-zero (`tecHAS_OBLIGATIONS`), and also blocked when `featureSingleAssetVault` is active and the token carries the `lsfMPTLocked` flag. Without `tfMPTUnauthorize`, it verifies the issuance exists, confirms the submitter is not the issuer (issuers cannot hold their own MPT), and rejects if an `MPToken` object already exists (`tecDUPLICATE`).

- **With `sfHolder` — issuer path**: The submitting account is acting as the issuer to allowlist or revoke allowlist for the named holder. It confirms the submitter really is the issuer (`tecNO_PERMISSION` otherwise), verifies the issuance has `lsfMPTRequireAuth` set (otherwise `tecNO_AUTH`, since granting auth on a non-auth issuance is meaningless), and requires that the holder has already created their `MPToken` entry — the ledger enforces a holder-first, issuer-second handshake. A special guard prevents authorizing pseudo-accounts (vaults and loan brokers, identified via `isPseudoAccount()`), because those are implicitly always authorized by the protocol.

**`doApply()`** is a thin delegation layer that calls `authorizeMPToken()` from `MPTokenHelpers`, passing `preFeeBalance_` (the pre-fee XRP balance stored by the base class) along with the transaction fields. The actual ledger mutations — creating or deleting the `MPToken` SLE, adjusting directory entries, and toggling the `lsfMPTAuthorized` flag — live entirely in that helper, making them reusable across multiple code paths.

## Design Notes

`ConsequencesFactory` is set to `Normal`, meaning a transaction that fails during `preclaim` or `doApply` still consumes its sequence number and fee — consistent with the standard XRPL policy for transactions that reach the network in a well-formed state.

The auto-generated protocol layer (`xrpl::transactions::MPTokenAuthorize` in `protocol_autogen/`) marks this transaction as `Delegation::delegable`, allowing a credentialed delegate to submit it on behalf of the owner account. The required delegation privilege is `mustAuthorizeMPT`.

The deliberate asymmetry in `preclaim()` — where the same transaction type, differentiated only by the presence of `sfHolder`, handles fundamentally different operations — avoids the overhead of two separate transaction types for what is logically one lifecycle event: establishing that a given account may hold a given MPT issuance. The two-step protocol (holder opts in first, issuer approves second) mirrors the trust line model's bidirectional consent while keeping the ledger object footprint predictable.