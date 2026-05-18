# XChainBridge.cpp

## Role in the System

This file is the monolithic implementation hub for all cross-chain bridge transaction types on the XRP Ledger. It provides the `preflight`, `preclaim`, and `doApply` methods for eight transactor classes — `XChainCreateBridge`, `BridgeModify`, `XChainClaim`, `XChainCommit`, `XChainCreateClaimID`, `XChainAddClaimAttestation`, `XChainAddAccountCreateAttestation`, and `XChainCreateAccountCommit` — plus a suite of internal helper functions that implement the shared quorum, attestation, and fund-transfer mechanics those transactors all rely on.

The bridge protocol connects two independent ledgers (a *locking chain* and an *issuing chain*) without an exchange rate. Committing an asset into the locking chain's door account generates an equivalent wrapped asset on the issuing chain, and vice versa. The door account is a regular XRPL multi-sig account controlled by witness servers. A file-level block comment explains this "box" mental model and is worth reading before diving into the code.

---

## The Seven-Transaction Lifecycle

A normal cross-chain transfer follows a defined sequence:

1. **`XChainCreateClaimID`** — The recipient on the destination chain reserves a monotonically increasing claim ID. This must happen *before* the source-side commit, locking in the source account identity and the reward the submitter is willing to pay witness servers.

2. **`XChainCommit`** — The sender on the source chain locks funds into the door account via `transferHelper`, and deliberately supports fee-dipping (spending down to the reserve) via `TransferHelperSubmittingAccountInfo`.

3. **`XChainAddClaimAttestation`** — Each witness server submits a signed attestation that the commit event occurred. When a quorum's worth of weight is reached *and* the attestation includes a destination, funds are settled automatically.

4. **`XChainClaim`** (optional fallback) — If no destination was embedded in the commit, or if automatic settlement failed, the claim ID owner can explicitly trigger settlement.

For account bootstrap, the protocol substitutes `XChainCreateAccountCommit` (locks both the creation amount and the witness reward) and `XChainAddAccountCreateAttestation` (witness attestations enforcing strict commit order).

---

## Internal Helper Architecture

### `transferHelper`

The central payment primitive handling both XRP (direct balance manipulation) and IOU (via the `flow` engine). Key behaviors: self-transfers short-circuit immediately; deposit authorization is enforced unless the destination is the claim owner bypassing their own auth; XRP account creation is permitted only when the amount meets the base reserve; and the `submittingAccountInfo` parameter supports fee-dipping for commit transactions.

### `finalizeClaimHelper`

Orchestrates two-step settlement using a nested `PaymentSandbox` so the main transfer can be rolled back if reward distribution fails. The `FinalizeClaimHelperResult` struct reports three independent TER outcomes with a priority-ordered `ter()` accessor. The `OnTransferFail` enum is critical: regular claims use `keepClaim` (failed transfer preserves the claim ID for retry), while account-create claims use `removeClaim` (failed transfer still destroys the claim ID to unblock subsequent ordered creates).

### `claimHelper` and `onNewAttestations`

`claimHelper` is a template shared between regular and account-create quorum checks. Before counting weight, it strips attestations whose signer's key is no longer valid (master key disabled, regular key rotated). `onNewAttestations` adds or replaces an attestation from a signer, then immediately checks quorum. The `CheckDst::check` vs `CheckDst::ignore` flag distinguishes automatic settlement (destination must match the attested value) from user-triggered `XChainClaim` (any matching quorum suffices).

### `applyClaimAttestations` and `applyCreateAccountAttestations`

Both use a lambda scope to read and modify the claim ID SLE, then explicitly drop all references before calling `finalizeClaimHelper` — because the payment sandbox infrastructure requires no live SLE references from a parent view overlap with a child sandbox's mutations. The code comments this pattern as "ugly — admittedly." `applyCreateAccountAttestations` enforces the strict ordering invariant (`createCount == claimCount + 1`) and advances the counter past failed claims to prevent one stalled create from blocking all later ones.

---

## Validation Design

`XChainCreateBridge::preflight` enforces structural invariants: distinct door accounts (replay prevention), matching XRP/IOU types on both sides, and critically — for XRP bridges the issuing door must be the genesis root account; for IOU bridges it must be the currency issuer. This guarantees the issuing side can never exhaust its supply of wrapped tokens.

`checkAttestationPublicKey` handles three cases: non-existent account (public key must derive to the signer account via `calcAccountID`), existing account using master key (master must not be disabled), and existing account using regular key (regular key field must match). This runs at both `preclaim` time and again inside `onNewAttestations` — explicitly redundant as a defensive guard against future refactoring.

---

## Concurrency and State Management

All state mutations go through `PaymentSandbox`, which buffers changes and applies atomically only on explicit `.apply()`. If any phase encounters `tecINTERNAL` or `tefBAD_LEDGER`, the entire sandbox is discarded. The `readOrpeekBridge` helper resolves bridge ambiguity by trying the locking-chain keylet first, then the issuing-chain keylet. `XChainClaim` and the attestation transactors are marked `ConsequencesFactory{Blocker}` — because they may trigger fund movements of indeterminate size, the transaction queue cannot pre-compute their consequences.