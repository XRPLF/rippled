# `include/xrpl/ledger/Sandbox.h`

`Sandbox` is the standard discardable staging layer used by XRPL transaction processors to attempt ledger mutations without committing them permanently. It represents the simplest complete implementation of `detail::ApplyViewBase`, delegating every read and write operation to the base view through a buffered change table, then offering a single commit-or-discard decision point via `apply()`.

## Role in the View Hierarchy

The XRPL ledger access model is built around a layered view hierarchy. `ReadView` provides read-only access. `ApplyView` adds the ability to peek at mutable `SLE` objects and issue insert/update/erase operations. `RawView` adds lower-level unconditional mutations. `detail::ApplyViewBase` fuses all three via multiple inheritance and stores all changes in a protected `ApplyStateTable items_` member that acts as a write buffer.

`Sandbox` adds nothing to `ApplyViewBase` except two constructors and the `apply()` method. This minimalism is intentional — the full complexity lives in `ApplyStateTable`, and `Sandbox` simply exposes the "commit to a target" operation to callers.

## Buffering and the Commit Model

All modifications made through a `Sandbox` are accumulated in `items_` as tagged `(Action, SLE)` pairs, where `Action` is one of `cache`, `erase`, `insert`, or `modify`. The underlying `ReadView` base is never touched during this accumulation. When `apply(RawView& to)` is called, `ApplyStateTable::apply()` replays every buffered action against the target `RawView`, atomically promoting the tentative changes into it.

This pattern appears throughout the transaction processing layer. In `AMMCreate::doApply()`, for example, a `Sandbox` is constructed over the transactor's current `ApplyView`, all ledger mutations for AMM pool creation are applied through the sandbox, and only if the operation reports success is `sb.apply(ctx_.rawView())` called. On failure, the sandbox is simply destroyed, leaving the ledger unchanged — no rollback needed.

## Constructor Design

Two constructors cover the two call patterns found in practice:

```cpp
Sandbox(ReadView const* base, ApplyFlags flags);
Sandbox(ApplyView const* base);
```

The first is the general form: any read-only view plus explicit flags. The second convenience form is used when stacking a `Sandbox` on top of another `ApplyView` (including another `Sandbox`); it inherits flags from the parent, preserving properties like `tapNO_CHECK_SIGN` or `tapDRY_RUN` across layers without the caller having to re-specify them.

Copy construction and both assignment operators are deleted to prevent accidental duplication of the change buffer — only move construction is permitted. This enforces a clear ownership model: a `Sandbox` is created, used, and either committed or discarded, never shared.

## Relationship to Sibling Classes

`Sandbox` has two closely related siblings. `ApplyViewImpl` is the heavier variant used at the outermost transaction application boundary: its `apply()` takes an `OpenView` along with the `STTx` and `TER`, constructs full `TxMeta` metadata, and threads ownership links — capabilities `Sandbox` intentionally omits because inner operations don't need metadata. `PaymentSandbox` extends `Sandbox`'s semantics with `DeferredCredits` tracking, overriding `balanceHook` and `creditHook` so that credits from one step of a payment path cannot be double-counted as available liquidity in a subsequent step. `Sandbox` itself carries none of this payment-specific logic and is the right choice wherever a transactor simply needs a safe scratchpad.

## Key Invariant

Because `Sandbox` inherits the flags of its base view (the comment "The sandbox inherits the flags of the base" in the class definition is precise), code that queries `flags()` on a `Sandbox` will see exactly the same flags as the parent view. This prevents the sandbox from accidentally changing the execution context — e.g., dry-run semantics propagate correctly through nested sandboxes without any additional plumbing.