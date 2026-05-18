# `EscrowCreate.cpp` — Escrow Creation Transactor

## Role in the System

`EscrowCreate.cpp` implements the ledger transaction that locks funds in a conditional escrow object. It is one of three escrow transactors (`EscrowCreate`, `EscrowFinish`, `EscrowCancel`) and is responsible for the full three-phase lifecycle of creating an escrow: stateless validation (`preflight`), state-dependent validation (`preclaim`), and ledger mutation (`doApply`). The file originally handled only XRP escrows; it has since been extended under the `featureTokenEscrow` amendment to support IOU trustline amounts and MPT (Multi-Purpose Token) amounts, each with distinct locking semantics.

## Asset Type Dispatch Pattern

The file uses explicit template specializations for `Issue` (IOU/trustline) and `MPTIssue` (MPT) types rather than if/else branching. Three helper families follow this pattern: `escrowCreatePreflightHelper<T>`, `escrowCreatePreclaimHelper<T>`, and `escrowLockApplyHelper<T>`. Each is declared as a template and then specialized for each asset type. The caller dispatches via `std::visit` over the `STAmount::asset().value()` variant, so the compiler selects the correct specialization at the visit site. This avoids virtual dispatch overhead and keeps each asset type's validation logic self-contained without mixing them in a shared branch tree.

## Preflight: Stateless Validation

`EscrowCreate::preflight` validates the transaction without touching the ledger. For XRP amounts, it simply requires a positive value. For non-XRP amounts, it first checks that the `featureTokenEscrow` amendment is enabled — without it the field is rejected outright — then dispatches to the appropriate template specialization.

The `Issue` specialization (`escrowCreatePreflightHelper<Issue>`) rejects native amounts (the `amount.native()` guard prevents XRP slipping through), zero or negative values, and the bad-currency sentinel. The `MPTIssue` specialization additionally requires `featureMPTokensV1` to be active and enforces the `maxMPTokenAmount` ceiling on the raw MPT balance.

Three temporal invariants are enforced regardless of asset type:

- At least one of `sfCancelAfter` or `sfFinishAfter` must be present — an escrow with neither has no defined lifecycle.
- When both are present, `sfCancelAfter` must be strictly after `sfFinishAfter`, preventing a logically backwards escrow that could never be finished.
- When `sfFinishAfter` is absent, a crypto-condition (`sfCondition`) must be present. Without this guard, an escrow with only a cancel time could be finished immediately upon creation, which is likely a user mistake.

When `sfCondition` is present, its bytes are deserialized and validated with `Condition::deserialize`, returning `temMALFORMED` on any parse error. This early rejection avoids persisting an escrow whose condition can never be evaluated.

## Preclaim: State-Dependent Validation

`EscrowCreate::preclaim` reads ledger state to validate against it. The first check — independent of asset type — is that the destination account exists and is not a pseudo-account. The pseudo-account guard is explicitly noted as not needing its own amendment gate, because the conditions that would make a pseudo-account a valid destination are themselves gated.

For IOU escrows, `escrowCreatePreclaimHelper<Issue>` enforces several constraints:

- The issuer must not be the same as the sender. An issuer escrowing their own tokens makes no sense economically and would create a bookkeeping anomaly.
- The issuer must have the `lsfAllowTrustLineLocking` flag set. Token escrow is opt-in for issuers.
- The sender must hold a trust line to the issuer, and the trust line balance polarity must match the address ordering convention (`balance > 0` implies `issuer > account`, `balance < 0` implies `issuer < account`). This mirrors the XRP Ledger's internal trust line representation.
- Both the sender and destination must be authorized if the issuer requires authorization.
- Neither account can be frozen.
- The sender's spendable balance (computed with `accountHolds` using `fhIGNORE_FREEZE` to get the actual balance) must cover the requested amount.
- A `canAdd` precision check guards against precision loss that could occur when the amount is added back during finish.

For MPT escrows, `escrowCreatePreclaimHelper<MPTIssue>` follows the same structure but with MPT-specific rules: the `MPTokenIssuance` object must exist and carry the `lsfMPTCanEscrow` flag, the sender must hold an `MPToken` object for this issuance, and freeze violations return `tecLOCKED` rather than `tecFROZEN` — a deliberate distinction in error codes between IOU and MPT freeze states.

## `doApply`: Ledger Mutation

`doApply` begins by re-checking both `sfCancelAfter` and `sfFinishAfter` against the current `parentCloseTime`. This guard is not redundant: a transaction that passed preflight may arrive in a ledger close where the expiry has already passed, and it must be rejected rather than creating an immediately-expired escrow.

Reserve and balance checks come next. The sender's XRP balance must cover the reserve for one additional ledger object, and for XRP amounts must also cover the escrowed value itself.

The escrow SLE is constructed with fields copied directly from the transaction. Two conditional fields are notable:

- Under `fixIncludeKeyletFields`, the `sfSequence` field is written to the escrow SLE. This allows other transactions (notably `EscrowFinish` and `EscrowCancel`) to reconstruct the escrow's keylet directly from the SLE without needing external input, and is part of a broader cross-ledger object navigation improvement.
- Under `featureTokenEscrow` for non-XRP amounts, the transfer rate is captured at creation time into `sfTransferRate` if it differs from parity. Snapshotting the rate at creation is deliberate: IOU transfer rates can change after the escrow is created, but the sender committed to the escrowed amount under the rate in effect at that moment. The finish operation later reads this stored rate to calculate the correct delivery amount.

### Owner Directory Tracking

The escrow keylet is inserted into the sender's owner directory unconditionally, and into the destination's owner directory if the sender and destination differ. For IOU escrows (not MPT), the keylet is also inserted into the issuer's owner directory. The comment explains the asymmetry: locked IOU funds are moved to the issuer during creation (via `directSendNoFee`), so the issuer holds a liability that needs tracking. MPT escrows do not require this because the `MPTokenIssuance` object directly tracks the total locked supply in its own fields, making an additional directory entry unnecessary.

### Locking the Funds

XRP is locked by subtracting directly from `sfBalance` on the sender's account root. IOU tokens are locked by calling `directSendNoFee` to transfer the amount from the sender back to the issuer — conceptually, the tokens are retired from circulation and will be re-issued when the escrow finishes. MPT tokens are locked via `lockEscrowMPT`, which moves the balance into a dedicated locked-amount field on the sender's `MPToken` object rather than transferring ownership, which is why no issuer directory entry is needed.

Finally, `adjustOwnerCount` increments the sender's owner count by one, raising the XRP reserve requirement, and the modified account root SLE is committed with `ctx_.view().update(sle)`.