# `XChainBridge.h` — Cross-Chain Bridge Transaction Declarations

This header is the authoritative declaration point for all eight transaction types implementing XRPL's cross-chain bridge protocol. It defines the complete lifecycle of a cross-chain value transfer: establishing bridge infrastructure, initiating transfers on the source chain, relaying witness attestations, and finalizing fund claims on the destination chain. The file lives in the `bridge/` subdirectory under `transactors/`, grouping it with other feature-specific transactor declarations across the codebase.

## Architecture: The `Transactor` Pattern

Every class here inherits from `Transactor` and follows the three-phase execution pipeline mandatory for all XRPL transaction processing:

- **`preflight(PreflightContext const&)`** — Stateless validation run before any ledger access. Returns `NotTEC`, a type that encodes only non-TEC error codes. This is the cheapest gate and runs without ledger I/O.
- **`preclaim(PreclaimContext const&)`** — Read-only ledger state checks run after preflight passes. Returns `TER`. Can inspect account balances, existing objects, and signer lists without mutating state.
- **`doApply()`** — The state-mutating execution step, called only after both prior phases succeed.

The `Transactor` base class enforces this pipeline through its `invokePreflight<T>` template, which sequences amendment checks, `preflight1`/`preflight2` (the framework's own flag and signature checks), and then the derived class's `preflight()` via static name resolution — notably *not* virtual dispatch. Derived classes cannot skip framework checks by overriding `invokePreflight`.

## Bridge Infrastructure Transactions

`XChainCreateBridge` attaches a bridge definition to a "door account" — the custody point that holds locked assets on one side of the bridge. This is a one-time setup transaction. The door account then acts as the anchor for all subsequent cross-chain activity on that bridge. `BridgeModify` (aliased as `XChainModifyBridge`) handles post-creation parameter changes. Uniquely among the bridge transactors, it overrides `getFlagsMask()` — most transactors let the base class handle flag validation, but bridge modification supports flag-driven toggles (e.g., enabling or disabling features) that require a custom mask.

Both use `ConsequencesFactory{Normal}`, indicating their ledger impact is fully predictable at consequence-calculation time: they create or modify a single ledger object with no contingent side effects.

## The Normal Cross-Chain Transfer Sequence

A standard cross-chain transfer is a four-step protocol that spans two independent ledgers:

**Step 1 — `XChainCreateClaimID`**: The recipient creates a claim ID object on the *destination* chain first, reserving a monotonically-increasing sequence number. The source account that will commit funds must be specified here, binding authorization to a single account before any funds move. This ordering is the primary anti-replay mechanism: a claim ID is destroyed upon successful claim, making it single-use.

**Step 2 — `XChainCommit`**: The sender locks funds on the *source* chain, referencing the previously-created claim ID. This transactor uses `ConsequencesFactory{Custom}` with an explicit `makeTxConsequences()` static method. The custom factory exists because the amount being committed must be accurately reflected in consequence calculations — the committed funds are effectively removed from the sender's balance, a non-standard consequence that `Normal` cannot model.

**Step 3 — `XChainAddClaimAttestation`**: Off-chain witness servers submit cryptographic signatures to the destination chain, each attesting that the commit event occurred. Both attestation classes (`XChainAddClaimAttestation` and `XChainAddAccountCreateAttestation`) use `ConsequencesFactory{Blocker}`. The comment in the source is explicit: "Blocker since we cannot accurately calculate the consequences." Attestation submission may or may not push the running signature count past quorum, and crossing quorum triggers immediate fund movement. Because this outcome is not determinable at fee-calculation time, the engine conservatively marks the entire transaction as a blocker to prevent underestimated ordering constraints downstream.

**Step 4 — `XChainClaim`**: Once a quorum of attestations is collected, the recipient submits this transaction to move funds on the destination chain. On success, the `XChainClaimID` ledger object is destroyed. The key design decision here is the failure-recovery path: if `XChainClaim` fails for any reason, the claim ID *survives* and the transaction can be re-submitted with different parameters. This is also `ConsequencesFactory{Blocker}` for the same non-deterministic-impact reasoning. The distinction from the account-creation path below is important — here, recovery is always possible.

## Account Creation Flow and Its Operational Risks

`XChainCreateAccountCommit` and `XChainAddAccountCreateAttestation` address the bootstrapping problem: how does a user who has no account on the destination chain receive funds? The normal flow requires the recipient to create a claim ID first, but that requires an existing destination account.

The solution substitutes ordering-based replay prevention for the object-based mechanism: account creation commits are processed in strict source-chain sequence, enforced by the `createCount` field in `AttestationCreateAccount`. The constant `xbridgeMaxAccountCreateClaims = 128` caps the maximum queue depth for pending account-create claims — an important bound on both memory usage and processing latency.

This approach carries an explicitly documented hazard: if any attestation in the sequence is not delivered to the destination chain, *all subsequent account creations are permanently blocked*, with no recovery mechanism. The bridge's `MinAccountCreateAmount` field doubles as a feature gate — its absence disables `XChainCreateAccountCommit` entirely on a given bridge, allowing operators to opt out of this risk. The comment also restricts this path to XRP-to-XRP bridges only.

Critically, `XChainCreateAccountCommit` has no error recovery. If the destination-side claim fails, the committed XRP is permanently lost. This is a deliberate tradeoff: the ordering mechanism does not have a well-defined "undo" state, unlike the object-based claim ID whose survival enables retry. The comment advises treating this transaction solely as an account creation primitive, never as a general transfer path even when the destination account already exists.

## Relationship to Supporting Types

The file includes `XChainAttestations.h`, which provides `AttestationClaim` and `AttestationCreateAccount` — the cryptographic structures that witness servers produce and that `XChainAddClaimAttestation` and `XChainAddAccountCreateAttestation` validate and accumulate. The attestation base type carries a public key, a cryptographic signature, a `wasLockingChainSend` direction flag, and reward-distribution metadata. The `XChainAttestationsBase<T>` template enforces a hard cap of 256 attestations per claim to bound memory allocation.

The `using XChainModifyBridge = BridgeModify` and `using XChainAccountCreateCommit = XChainCreateAccountCommit` aliases exist for naming consistency with the broader XRPL transaction type registry, which uses `XChain`-prefixed names throughout. The underlying classes use shorter names; the aliases bridge the gap without code duplication.