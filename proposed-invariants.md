# Proposed additional invariants for XLS-65 / XLS-66 / XLS-103

Scope-limited working list. The invariants below are those that fit
into these code sites and nowhere else:

- `finalizeLoanSet` (including its `checkLoanCreation` /
  `checkLoanFunding` helpers) in
  `src/libxrpl/tx/invariants/VaultInvariant.cpp`;
- `finalizeLoanManage` in the same file;
- `finalizeLoanPay` in the same file;
- `NoModifiedUnmodifiableFields` in
  `src/libxrpl/tx/invariants/InvariantCheck.cpp`, for immutability
  checks re-homed from `LoanInvariant.cpp`.

Items that would have lived in `ValidVault::finalize` (the universal
branch), `ValidLoan`, `ValidLoanBroker`, `ValidPseudoAccounts`, or a
new invariant class have been removed from this document. Anything
two-step / `LoanAccept`-driven is out of scope: `ttLOAN_ACCEPT` is
not wired and the one-step `LoanSet` funding path is what these
functions have to police.

**Closed-ended vault (XLS-103) invariants are unconditionally in
scope**, wherever they naturally live. That includes checks in
`ValidLoan::finalize` (e.g. the closed-ended maturity bound) and
any other invariant class that touches vault-kind / phase state.
The scope rules above do not exclude these.

Each entry: **[spec ref]** short name — statement — where it lives —
gap type (**new** / **extend** / **hole**).

### Verified framework facts

- `ValidVault::finalize` only runs its per-type logic for
  transactions carrying `MustModifyVault` or `MayModifyVault`. From
  `transactions.macro`: `ttLOAN_SET` and `ttLOAN_PAY` have
  `MustModifyVault`; `ttLOAN_MANAGE` has `MayModifyVault` (a flagless
  `LoanManage` is a legal no-op).
- `ttLOAN_ACCEPT` genuinely does not exist in `transactions.macro`
  (numbering jumps 82 → 84). Any invariant that would depend on it
  is stripped rather than parked.
- Most checks in the three `finalizeLoan*` functions are gated
  behind `fixCleanup3_4_0`
  (`finalizeLoanSet` line 675, `finalizeLoanManage` line 684,
  `finalizeLoanPay` line 965), so new checks in those functions
  inherit the gate for free.
- `ValidVault::finalize` returns early on `!isTesSuccess(ret)`
  (line 1192), so every check reachable from these three functions
  is a class-2 (transaction post-condition) check — that guard
  already handles pass-2 (`ProtocolOnly`) correctness for us.

## B. `LossUnrealized` bookkeeping (XLS-65 §3.1.7.1, XLS-66 §3.10)

**Important**: the exposure amount is accounting-basis dependent — see
`loanVaultExposure(vaultSle, loanSle)` in `LendingHelpers.cpp:280`. Under
**accrual** it is `TotalValueOutstanding - ManagementFeeOutstanding`
(exactly `ValidVault::Loan::claim()`, line 94); under **cash-basis** it is
`PrincipalOutstanding` alone. Any check below must branch on
`getVaultVersion(vault) == VaultVersion::CashBasis`, which the invariant
snapshot does not currently record. `ValidVault::Vault` would need a
`version` (or `cashBasis`) field, and `Loan` would need an
`exposure(bool cashBasis)` accessor alongside `claim()`. This is the main
implementation cost of group B.

10. **[66 §3.10.5 impair]** `tfLoanImpair` bookkeeping — `LossUnrealized`
    must grow by exactly the impaired loan’s vault exposure, snapshot
    *before* the transaction (`LoanManage.cpp:290`, `impairLoan`). Currently
    `finalizeLoanManage` (line 466) only asserts that `assetsAvailable` and
    `assetsTotal` are unchanged; it never reads `LossUnrealized` at all in
    that function. `ValidVault::finalizeLoanManage`. **hole**.

11. **[66 §3.10.5 unimpair]** `tfLoanUnimpair` bookkeeping — symmetrical:
    `LossUnrealized` decreases by the loan’s pre-transaction exposure
    (`LoanManage.cpp:339`, `unimpairLoan`).
    `ValidVault::finalizeLoanManage`. **hole**.

12. **[66 §3.10.5 default]** `tfLoanDefault` bookkeeping — if and only if
    the defaulted loan had `lsfLoanImpaired` set *before* the transaction,
    `LossUnrealized` decreases by `totalDefaultAmount` (the pre-default
    exposure); otherwise it is unchanged (`LoanManage.cpp:224–236`).
    `ValidVault::finalizeLoanManage`. **hole**.

12a. **[66 §3.11.5]** `ttLOAN_PAY` on an impaired loan — `LoanPay::doApply`
    calls `LoanManage::unimpairLoan` before applying the payment
    (`LoanPay.cpp:394`), so a payment on an impaired loan legitimately moves
    `LossUnrealized` down by the pre-payment exposure. `finalizeLoanPay`
    never inspects `LossUnrealized`; the only guard is the universal
    "must not change loss unrealized" check at line 902, which explicitly
    exempts `ttLOAN_PAY`. So on this path `LossUnrealized` may currently
    move by **any** amount undetected. `ValidVault::finalizeLoanPay`.
    **hole** — this one was missed entirely in the earlier draft and belongs
    with 10–12 at the top of the priority list.

## E. Broker↔vault balance identities (XLS-66 §3.1.10, §3.11)

22. **[66 §3.1.10]** Broker debt equals the vault’s exposure to all its
    loans — `DebtTotal == Σ loanVaultExposure(loan)` over all live loans of
    that broker. Because loans are touched one-at-a-time by lending txns,
    this reduces to a delta check on the single modified loan. **Caveat
    found on re-check**: the snapshots live in `ValidVault`
    (`beforeLoan_`/`afterLoan_`), whereas `DebtTotal` lives in
    `ValidLoanBroker`, which does *not* snapshot loans at all — its
    `visitEntry` only collects `ltLOAN_BROKER`, `ltACCOUNT_ROOT`,
    `ltRIPPLE_STATE` and `ltMPTOKEN`. So this is either a loan-snapshot
    addition to `ValidLoanBroker::visitEntry`, or the check moves into
    `ValidVault` (which already reads the broker via `view.read` at
    `VaultInvariant.cpp:590`). The latter is cheaper. **new**, high value.

23. **[66 §3.11.5 items 6/7]** `LoanPay` splits correctly across broker and
    vault — under accrual, `Δ AssetsTotal == valueChange` and
    `Δ DebtTotal == (principalPaid + interestPaid) - valueChange`; under
    cash-basis, `Δ AssetsTotal == interestPaid` and
    `Δ DebtTotal == principalPaid` (`LendingHelpers.cpp:202–208`,
    `231–235`). The invariant can’t see the individual `LoanPaymentParts`,
    but it *can* assert the basis-appropriate identity
    `Δ DebtTotal == -(exposureBefore - exposureAfter)`, which is the
    observable consequence. This pairs with, and is independent of, the
    existing vault-side "assetsTotal delta == cash + claim delta" check at
    `VaultInvariant.cpp:649`. But see item 30 — that existing check may
    itself be basis-unaware. **new**.

25. **[66 §3.10.5 default]** Broker cover flow on default — for
    `ttLOAN_MANAGE(tfLoanDefault)`, `Δ CoverAvailable == -DefaultCovered`
    and this must equal the increase in `Vault.AssetsAvailable` (the broker
    pseudo-account’s outflow equals the vault pseudo-account’s inflow, no
    manufacturing). Placed in `ValidVault::finalizeLoanManage` because
    `ValidLoanBroker` does not snapshot the vault; `ValidVault` already
    carries `beforeBroker_` / `afterBroker_`. **new**.

## F. Closed-ended vault (XLS-103)

Closed-ended vault invariants are unconditionally in scope, wherever
they naturally live. The Investment-phase gate itself is already in
`checkLoanCreation` (`VaultInvariant.cpp:464–471`) and is
`NoPhase`-tolerant, so open-ended vaults fall through to the funding
checks. The closed-ended maturity bound is already in
`ValidLoan::finalize` (`LoanInvariant.cpp:56–83`). The `LoanAccept`
phase-gate item that used to live here has been stripped, along with
everything else two-step.

No known outstanding invariants in this section — this is a listing
of what is in scope from XLS-103, kept explicit so future closed-ended
work has a home. Add items here as they arise.

## H. Accounting-basis awareness (pre-requisite, not a spec invariant)

30. **[66 §3.11.5, cash-basis]** ✓ Landed. `ValidVault::Vault` now
    snapshots `sfLEVersion` (`VaultInvariant.cpp:68–72`), and
    `Loan::claim(version)` / `Loan::exposure(version)`
    (`VaultInvariant.cpp:96–114`) select the accrual vs. cash-basis
    formula. All identities that were basis-blind on the earlier draft
    (items 10, 11, 12, 12a, 22, 23, 38, and M1) now route through
    these accessors and read the correct claim under either basis.

## I. Requested by reviewers on PR 7732

Collected from the review threads on
[PR 7732](https://github.com/XRPLF/rippled/pull/7732) (`Tapanito`,
`gregtatcam`, `depthfirst-app[bot]`, `xrplf-ai-reviewer[bot]`) and
cross-checked against the current working tree, so each item states whether
it is still outstanding.

### Already implemented (no action)

- **Deleted-loan guard hoisted out of the loop** — `Tapanito` asked for the
  `txType != ttLOAN_DELETE && !deletedLoans_.empty()` form; that is now
  `LoanInvariant.cpp:168`, with the fully-paid-off check over
  `deletedLoans_` (the *before* snapshot, as requested) at line 178.
- **`ttLOAN_SET` creates exactly one loan** — the `beforeLoan_.empty()` half
  of the check is in place at `VaultInvariant.cpp:382`.
- **`ttLOAN_PAY` amount split cap** — `Tapanito`’s
  `vaultPseudoDelta + loanBrokerPseudoDelta + loanBrokerOwnerDelta <=
  tx[sfAmount]` is implemented at `VaultInvariant.cpp:588–625`, including
  the broker-owner-is-borrower degradation note.
- **`ttLOAN_MANAGE` no-flag case** — the flagless `LoanManage` no-op is
  rejected by the trailing `else` at `VaultInvariant.cpp:508`.

### Still outstanding

32. **`ttLOAN_MANAGE` must modify exactly one loan** (`depthfirst-app`).
    `finalizeLoanPay` has this guard (`VaultInvariant.cpp:635`);
    `finalizeLoanManage` does not, so a spurious loan creation, a
    multi-loan touch or a zero-loan manage passes. Add the same
    `afterLoan_.size() != 1 || beforeLoan_.size() != 1 ||
    afterLoan_[0].key != beforeLoan_[0].key` guard.
    `ValidVault::finalizeLoanManage`. **hole**.

33. **`LossUnrealized` directional checks** (`depthfirst-app`, two
    threads). Compute `lossDelta` once in `finalizeLoanManage` and require:
    `tfLoanImpair` ⇒ `lossDelta >= 0`; `tfLoanUnimpair` ⇒ `lossDelta <= 0`;
    `tfLoanDefault` ⇒ `lossDelta <= 0`. This is the rounding-safe,
    sign-only weakening of items 10–12 — worth landing first, since it
    needs no basis awareness (item 30) and no magnitude derivation.
    `ValidVault::finalizeLoanManage`. **hole**.

34. **`ttLOAN_PAY` claim may only shrink** (`depthfirst-app`).
    `claimDelta > 0` is always a violation: a payment pays the loan down,
    so the vault’s claim (`TotalValueOutstanding −
    ManagementFeeOutstanding`) can only fall. A cheap sign check next to
    the existing equality at `VaultInvariant.cpp:649`, and unlike that
    equality it is rounding-safe. `ValidVault::finalizeLoanPay`.
    **new**.

35. **Rounding fix for the `ttLOAN_PAY` conservation identity**
    (`depthfirst-app`). `VaultInvariant.cpp:649` rounds three deltas
    independently and compares with exact equality, so
    `round(a) + round(b)` may differ from `round(a + b)` by one `minScale`
    ULP and a legitimate payment can fail. Harmless while
    `minScale <= loanScale`, but a false positive once
    `minScale = scale(assetsTotal) > loanScale`. Round the residual once
    instead:
    `residual = roundToAsset(vaultAsset, ΔassetsTotal − ΔassetsAvailable −
    Δclaim, minScale); residual != kZero ⇒ fail`. Exactly `round(0)` when
    conservation holds, at any scale. `ValidVault::finalizeLoanPay`.
    **bug fix**, and it must be applied together with item 30 (the same
    line is also basis-blind).

36. **`ttLOAN_PAY` vault accounting completeness** (`Tapanito`).
    `finalizeLoanPay` bails out early when `afterLoan_` is empty
    (line 588) — but an empty `afterLoan_` is itself an invariant
    violation for a payment. Move the exactly-one-loan block (currently
    line 635) to the top of the function, which both makes the violation
    explicit and removes the need for the `!afterLoan_.empty()` guard.
    `ValidVault::finalizeLoanPay`. **restructure**.

37. **`ttLOAN_SET` broker and participant deltas** (`Tapanito`). Three
    additions to `finalizeLoanSet`'s `checkLoanFunding`:
    `Δ LoanBroker.DebtTotal == tx[sfPrincipalRequested]`; the borrower
    received `tx[sfPrincipalRequested] − loan.originationFee`; and the
    broker owner received `loan.originationFee`. Extract as helpers on
    `ValidVault` so the funding checks stay callable independently of
    the loan-creation cardinality/phase-gate checks that already live in
    `checkLoanCreation`. `ValidVault::finalizeLoanSet::checkLoanFunding`.
    **new**, medium cost.

38. **`ttLOAN_SET` vault accounting fields** (`Tapanito`).
    `finalizeLoanSet` currently only checks the pseudo-account balance
    against `PrincipalRequested`; it says nothing about the accounting
    fields. Add `Δ AssetsAvailable == -tx[sfPrincipalRequested]` and
    `Δ AssetsTotal == 0` (accrual books interest, so under accrual this is
    `Δ AssetsTotal == originationFee`-adjusted — resolve against item 30
    before writing). Partly subsumed by item 31 if that lands first.
    `ValidVault::finalizeLoanSet`. **hole**.

39. **`tfLoanDefault` moves accounting and balance in step** (`Tapanito`).
    On default, `finalizeLoanManage` only bounds the *signs* of
    `assetAvailableDelta` and `assetTotalDelta` (lines 486–507). The
    reviewer wants the vault accounting fields and the vault
    pseudo-account balance tied together, i.e. the same identity as
    item 31 plus `Δ AssetsAvailable == DefaultCovered` from the broker.
    Overlaps items 25 and 31. `ValidVault::finalizeLoanManage`. **hole**.

## J. Generalised from the PR 7732 review (not directly requested)

The reviewer comments cluster into three recurring shapes: *deduplicate a
per-type check into one object-level check*, *prefer a rounding-safe residual
or sign test over an exact comparison of independently rounded terms*, and
*hoist a cardinality/existence precondition to the top so later code can rely
on it*. Applying those shapes beyond the exact lines commented on:

41. **The residual-rounding defect is confined to one line — but the correct
    pattern is already in the file.** I checked every exact-equality delta
    comparison in `VaultInvariant.cpp` (lines 976, 993, 1222, 1270, 1279,
    1472, 1482, 1527, 1538). All of them compare **two** terms rounded at a
    shared `minScale`, and `a == b` unrounded implies
    `round(a) == round(b)`, so they are safe. Line 649 is the **only**
    three-term comparison, which is why it is the only one that can drift by
    a ULP. Notably `finalizeLoanPay` already does the right thing 40 lines
    earlier: lines 609–615 sum three *raw* deltas and round **once**. So
    item 35 is not a new technique, it is making line 649 consistent with
    line 615. That makes it a small, low-risk fix and strengthens the case
    for doing it first. **scope-limiting finding**.

42. **`finalizeLoanManage` tolerates a missing vault balance delta where the
    other two functions refuse to.** ✓ Landed. `finalizeLoanManage` now
    hard-fails on `tfLoanDefault` when `deltaAssets` returns `nullopt`,
    while the tolerant `value_or(zero)` path is preserved for
    impair/unimpair which legitimately move no funds
    (`VaultInvariant.cpp:713–719`).

47. **State the loan-cardinality precondition once, for all three loan
    transactions.** Items 32 and 36 are the same request at two call sites:
    `finalizeLoanPay` has the exactly-one-loan guard but buried at line 635,
    and `finalizeLoanManage` lacks it entirely. Since all three lending
    transactions touch exactly one loan (`ttLOAN_SET` creates one,
    `ttLOAN_MANAGE`/`ttLOAN_PAY` modify one), hoist a single shared
    precondition helper and let each `finalizeLoanX` assume it. That
    collapses items 32 and 36 into one change and removes the
    `!afterLoan_.empty()` guard the reviewer objected to. **Caveat**: this
    assumes lending transactions never batch loans — true today, but it is
    the assumption to revisit if that changes. **refactor**.

## K. Found by reading the implementation (second pass)

Items below came from reading the parts of `VaultInvariant.cpp` not covered
by the review threads — chiefly `finalizeLoanSet`'s control flow. Item 48
from the earlier draft (the `finalizeLoanSet` `NoPhase`-early-return merge
regression) has been fixed in the current tree and is now recorded under
"Dropped".

53. **`computeVaultMinScale` reads `beforeVault_[0]` without the guard its
    callers rely on.** Pre-`fixCleanup3_2_0` the function dereferences
    `beforeVault_[0]`. Every current caller runs after the
    `beforeVault_.empty() && txnType != ttVAULT_CREATE` gate in
    `ValidVault::finalize`, and `ttVAULT_CREATE` never calls it — so it is
    safe today. But the three `finalizeLoanX` helpers call it having only
    `XRPL_ASSERT`ed non-emptiness, which compiles out in release. If item
    37 extracts funding checks into helpers callable from another
    transactor, this becomes reachable. Add a defensive early return, or
    document the precondition on the declaration. **latent**, cheap to
    harden.

## L. Immutability re-homing (`LoanInvariant` → `InvariantCheck`)

Vault immutability was moved out of `ValidVault::finalize` and into
`NoModifiedUnmodifiableFields` under `fixCleanup3_4_0`
(`InvariantCheck.cpp:1234–1240`). The same treatment applies to the
flag-immutability checks previously in `ValidLoan` — the framework's
`reset(fee)` restores flag bits along with everything else on pass 2,
so these are naturally class-1 once co-located with the other
non-modifiable-field checks. Both items are now landed.

L1. **`lsfLoanOverpayment` set-once** — ✓ Landed. Relocated to
    `NoModifiedUnmodifiableFields`, sharing the `ltLOAN` per-type
    block with the value-field immutability checks
    (`InvariantCheck.cpp:1215–1241`). Kept ungated to match its
    prior coverage in `ValidLoan`; the outer `featureLendingProtocol`
    guard is the natural gate (no `ltLOAN` entries exist without it).

L2. **`lsfLoanDefault` never cleared** — ✓ Landed. Relocated to the
    same `ltLOAN` block as L1. Gated on `featureLendingProtocolV1_1`
    to preserve prior activation. The check is a one-way transition
    (before-set → after-clear is a violation; before-clear → after-set
    is legal), so it lives as a small inline comparison rather than
    routing through `kFieldChanged`.

## M. Cash-basis accounting (XRPL-Standards PR 582)

The `LendingProtocolV1_1` amendment (XRPL-Standards PR 582, amending
XLS-65 §3.1.2.2 / §3.1.7.4 and XLS-66 §3.8, §3.10, §3.11) makes
`Vault.AssetsTotal` and `LoanBroker.DebtTotal` principal-only for
vaults with `LEVersion == 1`, and gates the switch off the new
`sfLEVersion` field on the `Vault` ledger entry. Most of the
invariant work this implies is already in the current tree — the
basis-aware `Loan::claim(version)` / `Loan::exposure(version)`
accessors from item 30 are used by the funding, `LossUnrealized`,
and `DebtTotal` delta identities in all three `finalizeLoan*`
functions, so the amendment does not by itself require a raft of
new invariants. One in-scope hole remains:

M1. **`Δ AssetsTotal - Δ AssetsAvailable - Δ claim(version) == 0` on
    `tfLoanDefault`.** ✓ Landed. Once-rounded residual added to
    `finalizeLoanManage`'s default branch, mirroring the identities
    in `finalizeLoanPay` and `checkLoanFunding`
    (`VaultInvariant.cpp:815–843`). Basis-aware via `claim(version)`.
    Under cash-basis this reduces to
    `Δ AssetsTotal == DefaultCovered - Loan.PrincipalOutstanding`
    (matching XLS-66 §3.10.5.1); under accrual to
    `Δ AssetsTotal == DefaultCovered - (TotalValueOutstanding -
    ManagementFeeOutstanding)` (matching §3.10.5).

Everything else PR 582 introduces is either already implemented or
out of scope for this document:

- `Loan::claim(version)` / `exposure(version)` basis-aware accessors
  and `Vault::version` snapshot: implemented under item 30
  (`VaultInvariant.cpp:96–114, 68–72`).
- Impair / unimpair / default `LossUnrealized` magnitude uses
  `beforeLoan.exposure(version)`: already basis-aware
  (`VaultInvariant.cpp:762–780, 822–839, 1099–1115`).
- `LoanSet` and `LoanPay` `Δ DebtTotal == Δ exposure(version)`:
  already basis-aware (`VaultInvariant.cpp:562–581, 1152–1177`).
- `LoanSet` and `LoanPay` `Δ AssetsTotal - Δ AssetsAvailable -
  Δ claim(version) == 0`: already basis-aware
  (`VaultInvariant.cpp:636–649, 1130–1143`).
- Impair failure-condition bound (XLS-66 §3.10.4.3): universal
  `LossUnrealized <= AssetsTotal - AssetsAvailable` check
  (`VaultInvariant.cpp:1467`).
- `sfLEVersion` immutability: `NoModifiedUnmodifiableFields`
  (`InvariantCheck.cpp:1232`).
- `LEVersion` domain (`0` or `1`) and `LEVersion == 1` at creation:
  universal / `ttVAULT_CREATE` checks, out of scope here.
- `valueChange` removal under cash-basis: an implementation-side
  simplification with no invariant surface.

---

## Classification: state validity vs. transaction post-conditions

`Transactor::operator()` runs invariants twice
(`Transactor.cpp:1640–1659`):

1. `InvariantScope::Full` against the transaction's tentative
   outcome. Failure → `tecINVARIANT_FAILED`.
2. `reset(fee)` discards the transaction's effects, leaving only the
   fee deduction; then `InvariantScope::ProtocolOnly` re-runs. Failure
   here → `tefINVARIANT_FAILED`.

Two classes of check follow:

- **Class 1 — state validity.** Must hold of any ledger state,
  including the post-reset fee-claim-only state.
- **Class 2 — transaction post-conditions.** Assert what a specific
  transaction type did; meaningless once effects are rolled back.

`ValidVault` gates the entire finalize on `isTesSuccess(ret)`
(`VaultInvariant.cpp:1192`), so every check reachable from
`finalizeLoanSet` / `finalizeLoanManage` / `finalizeLoanPay` is
class-2 by construction and correctly skipped on pass 2. No new
gating is required for anything landing in those three functions.

`NoModifiedUnmodifiableFields` is class-1: on reset the ledger
restores the original field values, so its invariants hold trivially
on pass 2. That is why re-homing the flag-immutability checks (Section
L above) into it is safe and does not need an `isTesSuccess` guard.

---

## Recommended priority

Highest value / lowest cost, in this order. Numbering is inherited
from the earlier draft to keep cross-references stable.

All in-scope items are landed. Recorded here for provenance:

- **30, 35, 10, 11, 12, 12a, 33, 34, 47, 37, 38, 39, 22, 23, 25, 53,
  46**: `VaultInvariant.cpp` — basis-aware `claim(version)` /
  `exposure(version)`, `finalizeLoanPay` once-rounded conservation,
  `finalizeLoanManage` impair/unimpair/default sign + magnitude,
  broker↔vault identities, `exactlyOneLoan` hoist, funding-check
  extraction into `checkLoanCreation` / `checkLoanFunding`,
  `computeVaultMinScale` precondition guard.
- **42, M1**: `VaultInvariant.cpp` `finalizeLoanManage` — real
  vault-balance delta required on `tfLoanDefault`, plus the
  `Δ AssetsTotal - Δ AssetsAvailable - Δ claim(version) == 0`
  once-rounded residual on default.
- **L1, L2**: `InvariantCheck.cpp` `NoModifiedUnmodifiableFields` —
  `lsfLoanOverpayment` set-once and `lsfLoanDefault` never-cleared
  relocated from `LoanInvariant.cpp`.
- **40**: `InvariantCheck.cpp` `NoModifiedUnmodifiableFields` — vault
  `sfAsset` / `sfAccount` / `sfShareMPTID` immutability re-homed
  from `ValidVault::finalize` under `fixCleanup3_4_0`.

Closed-ended vault work has no outstanding proposed items — the
Investment-phase gate and maturity bound are both in the tree. Future
XLS-103 items belong in Section F wherever they naturally live.

## Dropped from an earlier draft

Out of scope under the new scope rules:

- All `ValidVault::finalize` universal-branch items (former Section
  A, items 1–9): `AssetsMaximum`, `Scale`, share-issuance
  static/flag invariants, non-transferable-share ban, exchange-rate
  monotonicity, deposit/withdraw share-ratio, `VaultDelete`
  residuals.
- All `ValidLoanBroker` items (former Section C, items 13–17):
  `DebtTotal <= DebtMaximum`, `DebtTotal == 0 ⇒ OwnerCount == 0`,
  rate bounds, broker↔vault directory linkage, `OwnerCount` delta.
- All `ValidLoan` items (former Section D, items 18–21) and item
  12b.
- `ValidLoanBroker` fee-routing item 24.
- `ValidPseudoAccounts` / cross-object structural items 28, 29.
- Universal `AssetsAvailable ↔ pseudo-account` item 31 and universal
  share-conservation item 44 (both `ValidVault::finalize`).
- Vault non-mutable-field helper item 40 — already re-homed to
  `NoModifiedUnmodifiableFields` (`InvariantCheck.cpp:1234–1240`).
- `ValidLoanBroker` refactor item 45 and its dependent broker-
  deletion items 54–56 (former Section L).
- `ttVAULT_CREATE` / `ttVAULT_SET` implementation-reading items
  49–52.

Stripped as two-step / `LoanAccept`-driven:

- Former item 27 (XLS-103 §8.4 `LoanAccept` phase gate).
- Former item 46 (phase-parameterise `finalizeLoanX` helpers for
  the `LoanSet` / `LoanAccept` split).
- The `LoanAccept` motivation on item 37; the underlying invariants
  themselves remain in scope for `finalizeLoanSet`.
- Item 48 (merge regression on the `finalizeLoanSet` phase gate) —
  already fixed in the current tree by making `NoPhase` fall through
  (`VaultInvariant.cpp:466`).

Already covered in the current tree:

- LossUnrealized ≤ AssetsTotal − AssetsAvailable and ≥ 0.
- Loan "interest due ≥ 0" (`LoanInvariant.cpp:241`).
- Loan `PaymentRemaining == 0 ⇔ balances zero`
  (`LoanInvariant.cpp:87, 98`).
- Broker `DebtTotal >= 0`, `CoverAvailable >= 0`,
  `CoverAvailable == pseudo-account balance`, `LoanSequence`
  monotonic (all in `ValidLoanBroker::finalize`).
- Broker and Loan constant-field immutability, plus vault
  `sfAsset` / `sfAccount` / `sfShareMPTID` / `sfVaultKind` /
  `sfSubscriptionDate` / `sfRedemptionDate` / `sfLEVersion`
  immutability, in `NoModifiedUnmodifiableFields`
  (`InvariantCheck.cpp:1195–1240`). `sfLEVersion` is the field
  introduced by XRPL-Standards PR 582 that pins the accounting
  basis to a vault for its lifetime; together with its
  protocol-only writability (never a transaction field), that
  gives us "accounting basis is fixed at creation" as a class-1
  invariant with no further work.
- `VaultDeposit` phase = `Subscription` / `NoPhase`;
  `VaultWithdraw` not in `Investment`.
- Closed-ended `LoanSet` phase = `Investment` (in
  `checkLoanCreation`).
- Vault balance vs. `AssetsAvailable` add-up, and
  deposit/withdraw/clawback balance vs. `AssetsTotal` add-up
  (universal check in `ValidVault::finalize`).
- Shares outstanding only changes on deposit/withdraw/clawback.
