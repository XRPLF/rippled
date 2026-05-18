# `FreezeInvariant.cpp` — Enforcing Frozen Asset Transfer Restrictions

## Role in the System

`FreezeInvariant.cpp` implements `TransfersNotFrozen`, one of the post-transaction invariant checkers that form the XRPL's last line of defense against consensus-breaking ledger mutations. After every transaction is applied — successful or not — the invariant framework streams every modified ledger entry through `visitEntry()` and then calls `finalize()` to render a pass/fail verdict. A `false` return from `finalize()` causes the entire transaction to be rolled back.

This particular invariant answers one question: *did this transaction move token balances across a frozen trust line?* Freeze rules on XRPL are non-trivial to evaluate in isolation, which is why the checker accumulates state across the full set of affected trust lines before making any judgement.

## Why Two-Phase Collection Is Necessary

The comment in `visitEntry()` makes the key insight explicit: *"A trust line freeze state alone doesn't determine if a transfer is frozen."* A payment might touch multiple trust lines — the sender's, the receiver's, intermediate offer lines. Whether a particular balance change is legally frozen depends on directionality and which side of the line set the freeze flag. More importantly, whether a transfer is to or from the issuer determines whether freeze restrictions apply at all.

This forces a collect-then-validate architecture. During `visitEntry()`, the checker accepts `ltRIPPLE_STATE` (trust line) entries and `ltACCOUNT_ROOT` entries. Account roots are silently catalogued in `possibleIssuers_` — a local cache so `finalize()` can resolve issuers without hitting the ledger view for every account that was already touched by the transaction. Trust line changes are decomposed into `BalanceChange` records and grouped into `balanceChanges_`, a `std::map<Issue, IssuerChanges>` keyed by currency and issuer.

The `IssuerChanges` struct separates balance decreases (`senders`) from increases (`receivers`). This split is what enables the critical issuer-transfer exemption: if `changes.receivers` is empty, all the outflow is going back to the issuer. If `changes.senders` is empty, the issuer is distributing tokens. Either direction is unconditionally allowed — freeze restrictions only apply to peer-to-peer transfers that bypass the issuer.

## Trust Line Orientation and Balance Sign

XRPL trust lines have a canonical orientation determined by account ID comparison. The "low" side has the numerically lower account ID. The `sfBalance` field is recorded from the low side's perspective, so a positive balance means the low account holds tokens. `recordBalanceChanges()` exploits this: it computes the balance delta, records it once for the high account as issuer (using the delta sign directly) and once for the low account as issuer (negating the sign), because the same raw balance movement looks opposite from each issuer's perspective.

The regular freeze flags follow the same orientation: `lsfHighFreeze` means the *high* side froze the *low* side's outbound transfers, and vice versa. In `validateFrozenState()`, the `high` boolean indicates whether the issuer under scrutiny sits on the high side of this particular trust line, which determines which freeze flag to read:

```cpp
bool const freeze =
    change.balanceChangeSign < 0 && change.line->isFlag(high ? lsfLowFreeze : lsfHighFreeze);
```

Note that regular freeze is directional — it only applies when the frozen party is the *sender* (`balanceChangeSign < 0`). Deep freeze (`lsfLowDeepFreeze` / `lsfHighDeepFreeze`) is unconditional: it blocks all movement regardless of who initiated the transfer.

## The Three Freeze Tiers

`validateFrozenState()` aggregates three independent freeze conditions into a single `frozen` boolean:

1. **Global freeze** (`lsfGlobalFreeze` on the issuer's account root): the issuer has halted all transfers of their currency on the entire network.
2. **Regular freeze** (`lsfLowFreeze` / `lsfHighFreeze`): the issuer has frozen one specific counterparty's outbound transfers. Direction-sensitive.
3. **Deep freeze** (`lsfLowDeepFreeze` / `lsfHighDeepFreeze`): the issuer has bilaterally frozen a specific trust line, blocking both inbound and outbound movement regardless of directionality.

Any of the three being true is sufficient to block the transfer.

## AMMClawback Exception

The only transaction type that carries the `overrideFreeze` privilege (as declared in `transactions.macro`) is `AMMClawback`. When `hasPrivilege(tx, overrideFreeze)` returns true, the invariant relaxes freeze enforcement — but not unconditionally. AMM-pool trust lines (`lsfAMMNode`) can be clawed back through regular and deep freezes, because AMM clawback is specifically designed to recover tokens from frozen AMM positions. However, a global freeze cannot be overridden, and regular (non-AMM) trust lines cannot be clawed back even with the privilege. The condition:

```cpp
if ((!isAMMLine || globalFreeze) && hasPrivilege(tx, overrideFreeze))
```

reads as: "if this is an AMM line and not globally frozen, AMMClawback is permitted to proceed." The logic is inverted for readability — if the condition is *not* met, we fall through to the fatal log and failure return.

## Amendment-Gated Enforcement and the `enforce` Pattern

A notable piece of defensive engineering lives in `finalize()`. The `enforce` variable is set to `view.rules().enabled(featureDeepFreeze)`, tying hard enforcement to the DeepFreeze amendment. The detailed comment in the source explains the rationale: the invariant runs its detection logic regardless of amendment status so that operators monitoring fatal-level logs get early warning of exploits even before the amendment is live. If a freeze bypass were discovered, node operators would see fatal log output; the XRPL community could then expedite amendment activation or deploy a hotfix amendment, and only the single `enforce =` line would need to change.

This pattern also interacts with `XRPL_ASSERT(enforce, ...)`. As documented in `InvariantCheckPrivilege.h`, `assert(enforce)` is intentionally counterintuitive: the assert fires when `enforce` is false (amendment disabled) *and* an invariant violation is detected. In debug builds this crashes the process, catching developer mistakes in tests that exercise the invariant without enabling the amendment. In release builds, if `enforce` is false, the invariant logs the violation but returns `true` (allowing the transaction through), acting as a monitoring-only probe.

## Boundary Cases in Balance Calculation

`calculateBalanceChange()` handles two subtle edge cases for dynamically created and deleted trust lines. When a trust line is created mid-transaction (by a Payment crossing offers, for example), `before` is null and the pre-existing balance is treated as zero — so the full post-transaction balance counts as the change. When `isDelete` is true, the post-transaction balance is also treated as zero, ensuring that deletion of a frozen trust line is still caught as a balance movement rather than silently exempted as "nothing remains." Both cases prevent a loophole where creating or deleting a trust line could bypass freeze checks by making the balance appear unchanged.

## Relationship to Surrounding Files

`FreezeInvariant.cpp` is one of roughly ten invariant implementations in `src/libxrpl/tx/invariants/`. The class is registered in the `InvariantChecks` tuple in `InvariantCheck.h` alongside checkers for XRP totals, account creation, NFTs, AMM pools, and vaults. All checkers share the same two-method contract (`visitEntry` / `finalize`) and the same privilege system via `InvariantCheckPrivilege.h`. The privilege system centralizes transaction-type-to-capability mapping in a single X-macro expansion over `transactions.macro`, so adding a new transaction type that needs freeze-override capability requires only a single change to that macro file — the invariant checker code never needs to be updated.