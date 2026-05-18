# `EscrowCreate.h` — Escrow Creation Transactor Declaration

`EscrowCreate` is the transactor class responsible for processing the ledger transaction that locks XRP or tokens into an escrow object. It lives within the `escrow` subdirectory alongside its two counterparts — `EscrowCancel` and `EscrowFinish` — which together form the complete escrow lifecycle. The header itself is thin: a class declaration that inherits from `Transactor` and advertises the four entry points the ledger framework calls at different stages of transaction processing.

## Role in the Transactor Framework

Every transaction type in the XRPL is represented by a subclass of `Transactor`. The framework drives these subclasses through a strict three-phase pipeline:

1. **`preflight`** — stateless validation using only the transaction fields and active rules.
2. **`preclaim`** — read-only ledger inspection after a fee has been provisionally claimed.
3. **`doApply`** — the mutating phase that writes ledger state changes.

`EscrowCreate` participates in all three phases and additionally overrides the consequences computation with `ConsequencesFactory{Custom}`. This is the critical architectural difference from `EscrowCancel` and `EscrowFinish`, both of which use `ConsequencesFactory{Normal}`. The `Custom` factory tells the framework to call `makeTxConsequences()` rather than rely on a generic cost model. This is necessary because the potential XRP impact of an `EscrowCreate` scales with the `sfAmount` field: for XRP escrows the consequences include the locked principal, whereas for token escrows (IOU or MPT) the XRP impact is `beast::zero` since no XRP leaves the account. The framework uses this information to reason about fee-claiming eligibility before any ledger state is read.

## The `preflight` Phase

`preflight` validates the transaction without touching the ledger. For the amount field it branches on asset type:

- **XRP**: must be strictly positive.
- **IOU trustline amounts** (`Issue`): requires the `featureTokenEscrow` amendment and rejects native or non-positive values, as well as the bad-currency sentinel.
- **MPT amounts** (`MPTIssue`): additionally requires `featureMPTokensV1`.

Beyond amounts, three timing invariants are enforced. At least one of `sfCancelAfter` or `sfFinishAfter` must be present. When both are given, the cancel time must be strictly after the finish time, preventing a logically inverted escrow. Finally, if no `sfFinishAfter` is provided, a crypto-condition (`sfCondition`) must exist — this prevents the pathological case of an escrow that can be finished immediately with no meaningful unlock mechanism. If a condition is present its wire encoding is deserialized and rejected as `temMALFORMED` if parsing fails.

## The `preclaim` Phase

`preclaim` adds ledger-aware checks that require a `ReadView`. It first confirms the destination account exists and is not a pseudo-account. Pseudo-accounts are explicitly blocked — this check is unconditional rather than amendment-gated because the writes that create pseudo-account discriminator fields are themselves amendment-gated, so the behaviour is always self-consistent.

For non-XRP amounts, the check is dispatched to template specializations via `std::visit` over the `Asset` variant:

- **IOU escrow** (`escrowCreatePreclaimHelper<Issue>`): verifies the issuer has `lsfAllowTrustLineLocking` set, that both source and destination accounts have a trust line, that neither is frozen, that both are authorized if the issuer requires auth, and that the sender has sufficient spendable balance. An additional precision-loss check (`canAdd`) guards against IOU arithmetic edge cases.
- **MPT escrow** (`escrowCreatePreclaimHelper<MPTIssue>`): verifies the issuance exists, that `lsfMPTCanEscrow` is set on the issuance, that the sender holds an `MPToken` object, and that neither side is frozen or lock-restricted. The transferability check (`canTransfer`) is also applied.

## The `doApply` Phase

`doApply` is where the escrow ledger object (`SLE`) is created and balances are moved. It begins by re-checking whether the `sfCancelAfter` and `sfFinishAfter` values have already passed relative to the ledger's `parentCloseTime`. This is not redundant: time advances between preflight and apply, and an escrow that expires between these two phases must be rejected here rather than creating an immediately-expired object.

The function then checks XRP reserve requirements. The sender's account must hold enough XRP to cover the incremented owner-count reserve plus, for XRP escrows, the locked principal itself. This single-pass reserve check is intentional — token escrows debit the token balance through a separate helper, not the XRP balance.

The `SLE` is constructed from the transaction fields and inserted into the ledger. The `fixIncludeKeyletFields` amendment gates whether `sfSequence` is copied into the escrow object (needed for `keylet` derivation by off-ledger clients).

For token escrows, the transfer rate is snapshotted at creation time and stored in `sfTransferRate`. This is architecturally important: it freezes the fee that will apply at `EscrowFinish` time, preventing the issuer from changing the rate between escrow creation and execution.

After inserting the SLE, `doApply` updates three owner directories:

- Always adds the escrow to the sender's `ownerDir`.
- Adds it to the destination's `ownerDir` unless this is a self-escrow.
- For IOU escrows only, adds it to the issuer's `ownerDir` so the issuer can enumerate all locked balances. MPT escrows skip this because the MPT issuance object already tracks its locked balance directly.

Finally, the sender's balance is debited — XRP directly from `sfBalance`, tokens via `escrowLockApplyHelper` which for IOU performs a `directSendNoFee` to the issuer and for MPT calls `lockEscrowMPT` to atomically update the locked field on the `MPToken` object.

## Relationship to Sibling Transactors

The class interface mirrors `EscrowCancel` and `EscrowFinish` closely, but neither sibling defines `makeTxConsequences`. `EscrowFinish` additionally overrides `checkExtraFeatures`, `preflightSigValidated`, and `calculateBaseFee` — the latter to charge an elevated fee proportional to the size of the fulfillment blob when completing a crypto-conditional escrow. `EscrowCreate` has no analogous fee scaling: its cost is driven entirely by the XRP amount reserved, which is why the `Custom` factory and its `makeTxConsequences` override exist.