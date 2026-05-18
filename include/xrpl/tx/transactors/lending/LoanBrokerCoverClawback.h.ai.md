# `LoanBrokerCoverClawback.h` — Transactor Header for Lending Protocol Cover Clawback

## Role and Context

This header defines the `LoanBrokerCoverClawback` transactor, which handles transaction type `ttLOAN_BROKER_COVER_CLAWBACK` (type 78) in XRPL's lending protocol. It is one of roughly ten transactors under `include/xrpl/tx/transactors/lending/` that together implement the full lifecycle of the XLS-66 lending system, alongside sibling types such as `LoanBrokerCoverDeposit`, `LoanBrokerCoverWithdraw`, `LoanBrokerSet`, and `LoanPay`.

In the lending protocol, a loan broker maintains a pool of "cover" funds — assets deposited as collateral to absorb potential losses on loans it brokers. These cover funds are held in a pseudo-account controlled by the broker's on-ledger object and tracked in `sfCoverAvailable`. This transaction gives the **issuer of the vault's underlying asset** the power to forcibly reclaim (claw back) a portion of those cover funds. This is analogous to how XRPL's existing clawback feature lets token issuers reclaim IOUs from ordinary accounts, but applied specifically to the broker's cover reserve.

## Class Structure

`LoanBrokerCoverClawback` inherits from `Transactor` and conforms to the standard three-phase validation + apply pattern enforced across the entire transactor framework:

- **`checkExtraFeatures`** (static, called from `invokePreflight`) — delegates entirely to `checkLendingProtocolDependencies`, which verifies that the `featureLendingProtocol` amendment and all its required dependencies are enabled. This is the canonical mechanism for amendment-gating a transactor without embedding the feature check inside `preflight` itself.

- **`preflight`** (static, stateless field-level validation) — validates that at least one of `sfLoanBrokerID` or `sfAmount` is present (both absent is invalid); that if a broker ID is given it is nonzero; and that any `sfAmount` is non-XRP, non-negative, and legally formed. A subtle rule: if no `sfLoanBrokerID` is given, the amount must be an IOU (not MPT), because the broker identity will be inferred from the IOU's issuer field.

- **`preclaim`** (static, ledger-read-only validation) — resolves the broker identity if not directly specified, checks that the submitting account is the actual issuer of the vault asset, verifies clawback permission flags (`lsfAllowTrustLineClawback` for IOUs, `lsfMPTCanClawback` for MPTs), and calculates the effective claw amount against the broker's minimum required cover.

- **`doApply`** (virtual, ledger mutation) — decrements `sfCoverAvailable` on the broker SLE and transfers the claw amount from the broker pseudo-account to the issuer via `accountSend` with `WaiveTransferFee::Yes`.

`ConsequencesFactory` is set to `Normal`, indicating the standard account-sequence-based ordering semantics with no blocking of other transaction types.

## Non-Obvious Design Decisions

**Broker identity resolution without an explicit ID.** The transaction makes `sfLoanBrokerID` optional. When omitted, the implementation infers the broker from the IOU issuer field in `sfAmount`. Because trust lines are bidirectional, an IOU amount carries both currency and issuer. If the issuer field names the broker's pseudo-account (which carries `sfLoanBrokerID` on its account SLE), the code extracts the broker ID from there. This convenience path only works for IOUs — MPTs have no bidirectional issuer ambiguity, so they require an explicit `sfLoanBrokerID`. The `preflight` enforces this restriction, and `determineBrokerID` in the `.cpp` implements the lookup.

**Amount semantics: zero means "take everything above minimum cover."** If `sfAmount` is zero or absent, `determineClawAmount` computes `sfCoverAvailable − (sfDebtTotal × sfCoverRateMinimum)` and claws back the entire surplus. This allows the issuer to efficiently drain excess cover without knowing the exact balance. Non-zero amounts are still capped at the same maximum, so the transaction can never drive the broker below its contractual minimum cover ratio.

**Template-specialized `preclaim` helpers for IOU vs MPT.** Rather than branching with `if/else`, the code uses `std::visit` with a templated helper `preclaimHelper<T>` specialised for `Issue` and `MPTIssue`. For IOUs it checks `lsfAllowTrustLineClawback` and the absence of `lsfNoFreeze`; for MPTs it reads the `MPTokenIssuance` SLE and checks `lsfMPTCanClawback`. This pattern avoids code duplication while keeping the asset-type-specific logic isolated.

**Balance sanity check in `preclaim`.** The code explicitly calls `accountHolds` against the broker pseudo-account and compares the result with the computed claw amount before permitting the transaction to proceed. The comment acknowledges that this value should always match `sfCoverAvailable`, treating any mismatch as an internal error. This defensive check catches ledger state corruption before `doApply` runs, when the cost of failure is lower.

**`doApply` re-runs `determineBrokerID` and `determineClawAmount`.** Because `doApply` works on a mutable ledger view that could theoretically diverge from what `preclaim` saw (in speculative apply), both helper functions are called again with `tecINTERNAL` as the fallback on failure. The real state should be identical, so these branches are marked `LCOV_EXCL_LINE` — they exist purely as a defensive layer against unexpected ledger divergence.

## Relationship to Sibling Files

Within the lending directory, `LoanBrokerCoverDeposit` and `LoanBrokerCoverWithdraw` are the natural counterparts: they add to and voluntarily remove from `sfCoverAvailable`, respectively. The clawback variant is privileged — only the asset issuer can trigger it — while deposit and withdraw are available to the broker operator. `LendingHelpers.h` provides the shared math infrastructure (amortization schedules, interest rates, rounding) used by the heavier transactors like `LoanPay` and `LoanManage`; the clawback transactor does not require that machinery since it performs no amortization calculations, only a simple cover-floor arithmetic.