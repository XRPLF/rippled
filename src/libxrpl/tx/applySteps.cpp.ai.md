# `src/libxrpl/tx/applySteps.cpp`

## Role in the System

This file is the orchestration hub for the XRPL transaction processing pipeline. Every transaction submitted to the ledger passes through the four entry points defined here — `preflight`, `preclaim`, `calculateBaseFee`, and `doApply` — in that order. The file does not contain any transaction-specific business logic itself; instead, it provides the type-dispatch machinery that routes each transaction to the correct concrete `Transactor` subclass and assembles the results into the structured `PreflightResult`, `PreclaimResult`, and `ApplyResult` objects that higher layers consume.

## The X-Macro Dispatch Engine

The central mechanism is `with_txn_type()`, a function template that converts a runtime `TxType` enum value into a compile-time template parameter:

```cpp
template <class F>
auto with_txn_type(Rules const& rules, TxType txnType, F&& f)
```

Inside, it `#include`s `transactions.macro` a second time with `TRANSACTION` redefined to emit one `case` label per known transaction type. Each case calls `f.template operator()<NamedTransactor>()`, passing the concrete transactor class as a template argument. This X-macro technique generates a flat switch statement at compile time, covering every transaction type declared in `transactions.macro` without any virtual dispatch overhead and without requiring `applySteps.cpp` to directly `#include` any transactor headers (the comment at line 14 explicitly forbids it). The first `#include` of the macro near the top of the file is done with `TRANSACTION` defined as a no-op so that only the transactor headers referenced via `TRANSACTION_INCLUDE` guards in the macro get pulled in for include-only purposes.

If `txnType` matches no case, `with_txn_type` throws the internal `UnknownTxnType` sentinel exception. All four public entry points catch this; the catch blocks are marked `LCOV_EXCL` because they represent conditions that should be unreachable in production — a transaction with an unknown type would have failed long before reaching here.

## Numeric Precision Guards

Before the switch dispatch, `with_txn_type` sets up thread-local RAII guards that control arithmetic precision for the entire processing step:

- When `featureSingleAssetVault` or `featureLendingProtocol` is enabled, a `CurrentTransactionRulesGuard` installs the current `Rules` into a thread-local slot (making them accessible without passing them everywhere), and a `NumberSO` guard configures floating-point-style number arithmetic based on whether `fixUniversalNumber` is active.
- Without those features, a `NumberMantissaScaleGuard` forces the legacy small-mantissa behavior to preserve historical correctness.

The comment here is self-critical: ideally these guards would have been applied to every processing phase from the start, but they were historically placed only in `doApply` (via `Transactor::operator()`). They were added to `with_txn_type` only once the new vault/lending features made it necessary for read-only phases like `preflight` and `preclaim` to also see the correct numeric rules.

## The Three-Phase Pipeline

**Preflight** (`invoke_preflight` / public `preflight`) validates a transaction purely from its static content — no ledger state required. It calls `Transactor::invokePreflight<T>` and, on success, computes `TxConsequences` via the `consequences_helper` template family. Three overloads of `consequences_helper` are selected at compile time by C++20 `requires` clauses on `T::ConsequencesFactory`:
- `Normal` — constructs a standard `TxConsequences` from the raw `STTx`.
- `Blocker` — marks the transaction as a blocker (e.g., `SetRegularKey`), signaling to the transaction queue that this transaction affects the ability of subsequent transactions to claim fees.
- `Custom` — delegates to `T::makeTxConsequences(ctx)` for transaction-specific logic.

There are two overloads of the public `preflight` function: one for ordinary transactions and one for transactions that are part of a batch (carrying a `parentBatchId`). Both produce a `PreflightResult` whose members are all `const`, by design, making it very difficult to construct a plausible-looking result without actually running the check.

**Preclaim** (`invoke_preclaim` / public `preclaim`) validates against ledger state. Before calling `T::preclaim(ctx)`, `invoke_preclaim` runs a hardcoded sequence of static-method checks via name hiding (compile-time polymorphism without virtual functions): `checkSeqProxy`, `checkPriorTxAndLastLedger`, `checkPermission`, `checkSign`, and then `checkFee`. The code comments enforce a critical security invariant: every check up to and including `checkSign` **must** return `NotTEC` (not a `tec` code). A `tec` result would cause a fee to be charged even before the signature is verified, which could enable theft or fund destruction. Only after `checkSign` succeeds can `checkFee` return a full `TER`.

The public `preclaim` also handles a race condition: if the `Rules` recorded in `preflightResult` no longer match the rules on the provided view (the ledger advanced between preflight and preclaim), it silently re-runs `preflight` with the new rules before constructing the `PreclaimContext`. The resulting `PreclaimResult` sets `likelyToClaimFee` based on whether the result is a `tes` success or a hard-fail `tec` (i.e., not a soft retry).

**doApply** constructs an `ApplyContext` and calls `invoke_apply`, which dispatches through `with_txn_type` to instantiate `T p(ctx)` and call `p()`. It first guards against a caller logic error by checking that the ledger sequence of the `preclaimResult`'s view matches the target `view`. If `likelyToClaimFee` is false, the transaction is returned early without applying.

## TxConsequences Constructors

The `TxConsequences` constructors (implemented here, declared in the header) build a compact summary of what the transaction "costs" the account's queue position. The base constructor extracts the fee from `sfFee` only if it is native XRP and non-negative — a defensive guard against malformed or exotic fee fields. Derived constructors layer on `potentialSpend` (for transactions like Payment that may consume more XRP than just the fee), a `blocker` flag, or a custom `sequencesConsumed` count (for multi-sequence-consuming operations). This structure feeds the `TxQ` logic that decides which transactions from an account can be tentatively accepted.

## Key Design Decisions

The prohibition on including transactor headers directly in this file is architectural: it keeps compile-time dependencies minimal and forces all type-specific behavior through the macro-generated dispatch. The compile-time polymorphism pattern (static methods with name hiding rather than virtual functions) is used throughout the `Transactor` hierarchy specifically because it allows the dispatch to be resolved at compile time via template instantiation, with the switch table as the only runtime cost.