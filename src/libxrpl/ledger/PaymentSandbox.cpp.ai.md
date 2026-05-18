# PaymentSandbox.cpp

## Role in the System

`PaymentSandbox.cpp` implements the deferred credit accounting layer that makes multi-hop payments safe on the XRP Ledger. The core problem it solves is **circular liquidity exploitation**: in a multi-step payment path, if a credit to an account were immediately visible as usable balance, a later step in the same path could draw on that already-committed liquidity, violating the intended accounting invariants. `PaymentSandbox` prevents this by intercepting every credit during payment execution and recording it in a shadow table, then returning a "pre-credit" balance whenever a step queries an account's available funds.

The file is split between two closely coupled entities: `detail::DeferredCredits`, which owns the credit accounting data structures, and `PaymentSandbox`, which integrates those structures into the ledger view hierarchy by overriding hook methods.

## DeferredCredits: The Shadow Accounting Engine

`DeferredCredits` maintains three independent maps:

- `creditsIOU_` — keyed by `(AccountID, AccountID, Currency)` for IOU trust line credits.
- `creditsMPT_` — keyed by `MPTID` for Multi-Purpose Token credits.
- `ownerCounts_` — keyed by `AccountID`, tracking the peak owner count seen during the payment.

### IOU Canonical Key Design

`makeKeyIOU` always places the lexicographically-lower `AccountID` first, so the pair (A→B) and (B→A) share a single map entry. Within that entry, `ValueIOU` distinguishes credits from each direction via `lowAcctCredits` and `highAcctCredits` fields. This mirrors how the ledger itself stores IOU trust lines as bidirectional `RippleState` objects, where one side is arbitrarily the "low" account.

When `creditIOU` records a new credit between a sender and receiver it never seen before, it also snapshots `lowAcctOrigBalance` — the sender's balance before the credit occurred. Critically, **only the first credit for a given pair captures this snapshot**; subsequent credits for the same pair accumulate on top of `lowAcctCredits`/`highAcctCredits` but leave `lowAcctOrigBalance` untouched. This is the "post-switchover" algorithm.

### Numerical Stability: Post-Switchover Algorithm

The comment in `balanceHookIOU` explains the design rationale clearly. A naive implementation would take the post-credit balance `B+C` passed in and subtract the accumulated credit `C` to recover the pre-credit balance: `(B+C) - C`. When the credit `C` is large relative to the original balance `B`, this floating-point subtraction suffers cancellation error. The post-switchover approach stores `B` directly as `lowAcctOrigBalance` and returns it, avoiding the cancellation entirely.

`balanceHookIOU` walks the linked chain of sandboxes, accumulating `delta` (total debits observed) and `lastBal` (the original balance from the first record encountered). The returned value is `min(amount, lastBal - delta, minBal)`, where `minBal` is the minimum `lastBal` across all sandboxes in the chain. This triple minimum prevents the adjusted balance from exceeding any ancestor's snapshot of the pre-credit state. A special guard clears any computed negative XRP balance to zero — this can arise when a large XRP credit is recorded then debited within the same payment, producing a mathematically negative but not erroneous result.

### MPT Accounting

MPT (Multi-Purpose Token) tracking is structurally more complex because MPT relationships are not symmetric: there is a distinct issuer and one or more holders, with `OutstandingAmount` tracking total issued supply.

`IssuerValueMPT` stores:
- A `holders` sub-map recording per-holder debits and original balances.
- A `credit` field representing total credits issued from the issuer to holders.
- An `origBalance` capturing the issuer's original `OutstandingAmount`.
- A `selfDebit` field for the special case of an issuer selling MPT through their own offer.

The `selfDebit` case is necessary because the payment engine executes paths in **reverse** (credit first, then debit). If the issuer owns a sell offer, the credit step runs first and can temporarily overflow `OutstandingAmount` beyond `MaximumAmount`. The `issuerSelfDebitHookMPT` / `issuerSelfDebitMPT` pathway records how much the issuer has already self-debited, allowing `balanceHookSelfIssueMPT` to cap the issuer's available issuance to `origBalance - selfDebit`. `balanceHookMPT` applies analogous capping logic for individual holders or the issuer.

### Owner Count Tracking

During payment execution, trust lines may be created or destroyed, transiently changing an account's owner count and therefore its XRP reserve requirement. `DeferredCredits::ownerCount(setter)` always records `max(cur, next)` — the peak count between the current and target value. `ownerCountHook` walks the sandbox chain and returns the maximum recorded count, ensuring that reserve checks never undercount the peak obligation incurred mid-payment.

## PaymentSandbox: Hook Integration

`PaymentSandbox` inherits from `detail::ApplyViewBase` (itself extending `ApplyView` and `RawView`) and carries two members: a `DeferredCredits tab_` instance and a nullable `PaymentSandbox const* ps_` pointer to a parent sandbox.

The hook overrides form a thin delegation layer:

- `creditHookIOU` / `creditHookMPT` / `issuerSelfDebitHookMPT` — called by the payment engine whenever a credit flows; they forward directly into `tab_`.
- `balanceHookIOU` / `balanceHookMPT` / `balanceHookSelfIssueMPT` — called when a step queries available balance; they walk the `ps_` chain collecting adjustments from all ancestor sandboxes.
- `adjustOwnerCountHook` / `ownerCountHook` — owner count analogues of the above.

The base class `ApplyView` provides no-op default implementations of all hook methods, so non-payment code paths that operate through a plain `ApplyViewBase` are completely unaffected. Only a `PaymentSandbox` activates the deferred credit behavior.

## Nested Sandbox Chain and apply()

`PaymentSandbox` supports nesting: constructing one on top of another via the `explicit PaymentSandbox(PaymentSandbox const* base)` constructor sets `ps_` to the parent. Nested sandboxes allow individual path segments to be tentatively applied and rolled back independently. The `ps_` pointer is checked by the two `apply()` overloads:

- `apply(RawView& to)` — terminal apply to the underlying ledger state. Asserts `ps_ == nullptr` to confirm there is no unresolved parent.
- `apply(PaymentSandbox& to)` — merges into a parent sandbox. Asserts `ps_ == &to`, then calls both `items_.apply(to)` (to propagate ledger state changes) and `tab_.apply(to.tab_)` (to merge the deferred credit tables).

`DeferredCredits::apply` merges by accumulating credits additively into the target and preserving the original balance snapshots already recorded there — it never overwrites the first-seen `origBalance`, consistent with the post-switchover invariant.

## balanceChanges() and xrpDestroyed()

`balanceChanges()` is a diagnostic/accounting utility that computes net balance deltas across all modified ledger objects by delegating to `items_.visit()`. For each `ltACCOUNT_ROOT` and `ltRIPPLE_STATE` object seen, it records `(lowID, highID, currency) → delta` and also populates `(lowID, lowID, currency)` and `(highID, highID, currency)` entries to capture per-issuer totals. This allows callers to reconstruct the full currency flow of a completed payment. `xrpDestroyed()` simply forwards to `items_.dropsDestroyed()` to report XRP burned as fees during the transaction.