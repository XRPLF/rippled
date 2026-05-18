# `PaymentSandbox.h` — Liquidity-Isolation Layer for XRPL Payments

## Role in the System

The XRPL payment engine processes complex multi-hop paths where value flows through chains of trust lines, order books, and AMM pools. Without a special guard, a credit arriving at an intermediate account mid-path could be immediately visible to later steps in the same path — a form of "double-counting" liquidity that would allow the ledger to produce phantom value. `PaymentSandbox` exists to prevent this: it is a speculative ledger view that tracks every credit made during payment execution and hides those credits from balance queries until the entire transaction commits.

`PaymentSandbox` fits into the ledger view hierarchy alongside `Sandbox`. Both inherit from `detail::ApplyViewBase`, which itself implements the `ApplyView` + `RawView` dual interface. While a plain `Sandbox` is a straightforward journal of proposed ledger mutations, `PaymentSandbox` adds the deferred-credit bookkeeping layer via the hook protocol defined in `ApplyView`.

## The Core Problem: Circular Liquidity

Consider a path `A → B → A`. When the engine credits `A` in a later step, that credit must not appear as usable funds when computing whether `A` can send in the earlier step. The same applies to any path that touches an account more than once, or to multiple concurrent paths that share intermediate accounts. The fix is not to block credits — mutations must still propagate for the accounting to be correct — but to prevent freshly-arriving credits from being seen when computing the available balance for outgoing transfers.

## `DeferredCredits` — The Bookkeeping Ledger

The inner class `detail::DeferredCredits` maintains two separate tables: one for IOU trust lines (`creditsIOU_`) keyed by a canonical `(lowAccount, highAccount, currency)` triple, and one for MPT issuances (`creditsMPT_`) keyed by `MPTID`. Every time a credit is applied through `PaymentSandbox`, it is recorded in these tables along with the *pre-credit balance* at the point the first credit was recorded. Subsequent credits for the same pair only accumulate amounts; they do not overwrite the saved original balance.

For IOU, each record stores separate credit accumulations for the "low" and "high" accounts (canonically ordered by `AccountID`) so that the `adjustmentsIOU()` query can correctly orient the debits and credits regardless of the direction of the query.

For MPT, the structure is necessarily different because MPT does not have bidirectional trust lines. `IssuerValueMPT` tracks per-holder debit amounts plus an aggregate credit to the issuer's outstanding amount. A special `selfDebit` field handles the case where the issuer itself owns a sell offer — the payment engine runs in reverse, so crediting an MPT holder first can temporarily push `OutstandingAmount` above `MaximumAmount`. The `selfDebit` field captures how much the issuer has "sold" via offers so that `balanceHookSelfIssueMPT` can correctly limit what additional issuance is possible.

Owner count tracking has its own entry in `DeferredCredits`. The `ownerCount` setter stores the *maximum* of the current and next counts. The comment explains the rationale: since payments only ever decrease owner counts, the highest remembered count is the conservative bound. `ownerCountHook` then returns the max across all sandboxes in the chain, preventing a transient low count from bypassing reserve checks mid-payment.

## The Hook Protocol

`ApplyView` declares virtual no-ops for `creditHookIOU`, `creditHookMPT`, `issuerSelfDebitHookMPT`, `adjustOwnerCountHook`, and `ownerCountHook`. Higher-level ledger mutation helpers (in `View.h`) call these hooks at every credit and owner-count transition. Ordinary `Sandbox` ignores them via the base class defaults. `PaymentSandbox` overrides all of them, delegating directly into its private `DeferredCredits tab_` instance. This design keeps the hook plumbing orthogonal to the general view machinery — no transaction type other than payments needs to pay for the overhead.

## Balance Adjustment Logic

`balanceHookIOU()` traverses the sandbox chain (via the `ps_` parent pointer) and accumulates total debits from all ancestor sandboxes. The implementation comments explain a deliberate numerical-stability choice: rather than computing `(B+C) - C` (subtracting credits from an already-credited balance), it stores the original balance `B` and subtracts debits. When `B` and `C` differ by many orders of magnitude, floating-point arithmetic causes `(B+C) - C ≠ B`. The adjusted amount is `min(amount, lastBalance - debits, minBalance)` — the three-way minimum ensures correctness in edge cases where the deferred table might overestimate by a small rounding error. A special-case clamps negative XRP results to zero; a large credit followed by the same debit can legitimately produce a negative computed value that is not actually an error.

`balanceHookMPT()` and `balanceHookSelfIssueMPT()` follow the same principle for holders and issuers respectively, but work in raw `int64_t` arithmetic since MPT amounts are not signed `STAmount` values.

## Stacking and Apply

`PaymentSandbox` can be constructed on top of another `PaymentSandbox` using the explicit pointer constructors. The parent pointer `ps_` forms a singly-linked chain. This nesting is used inside the pathfinding engine, which runs each candidate strand in a disposable child sandbox and only commits to the parent on success. The comment in the header is emphatic: **if you are constructing on top of a `PaymentSandbox`, you must use the pointer constructors** — ordinary view-to-view construction would bypass the deferred-credit propagation and break invariants.

`apply(RawView& to)` is the terminal commit: it asserts `ps_ == nullptr` (the sandbox has no parent) and flushes the state journal to the raw ledger. `apply(PaymentSandbox& to)` asserts that `&to == ps_` (you can only apply to your direct parent) and propagates both the state journal and the deferred credits via `tab_.apply(to.tab_)`. In `DeferredCredits::apply()`, original balances are never overwritten — only credit accumulators are merged and owner-count maximums are taken.

## `balanceChanges()` and `xrpDestroyed()`

These are observational APIs used after payment execution. `balanceChanges()` iterates the state journal via `items_.visit()`, diffing every modified `ltACCOUNT_ROOT` and `ltRIPPLE_STATE` ledger entry against the pre-payment view to compute net balance deltas per `(low, high, currency)` triple. It also records per-issuer totals using the diagonal key `(account, account, currency)`. `xrpDestroyed()` delegates to `items_.dropsDestroyed()` to report fees burned during the payment, distinct from transferred XRP.