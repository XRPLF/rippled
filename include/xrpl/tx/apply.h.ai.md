# `include/xrpl/tx/apply.h` — Transaction Validation and Application API

This header defines the top-level public interface for applying transactions to the XRP Ledger. It sits at the boundary between the consensus machinery (which decides *when* to process transactions) and the lower-level transaction pipeline (which decides *how* to process them). Everything in this file either gates a transaction before touching the ledger or orchestrates the full application sequence as a single call.

## The Three-Stage Pipeline

Transaction processing in the XRPL is deliberately split into three ordered stages, defined in the companion header `applySteps.h`:

1. **`preflight`** — stateless validation against ledger rules (signature format, field constraints, fee structure). The result can be cached and reused safely across threads because it requires no ledger state.
2. **`preclaim`** — stateful validation against the current `OpenView` (account existence, sufficient balance, sequence correctness). Must run on the same thread as the view.
3. **`doApply`** — the actual mutation of ledger state. Must run on the same thread as `preclaim`, with the same view.

The `apply()` function declared here composes all three into a single call for callers that don't need fine-grained control. Internally the implementation template-dispatches on a preflight callable:

```cpp
return doApply(preclaim(preflightChecks(), registry, view), registry, view);
```

This structure means the hot path avoids unnecessary copies: the `PreflightResult` and `PreclaimResult` structs hold const references (not copies) of the original `STTx` and `ReadView`, and their copy-assignment operators are deleted to prevent accidental reuse with a stale view.

## Validity Caching via HashRouter

The `checkValidity()` function does not maintain its own cache. Instead, it piggybacks on the `HashRouter` — a network-layer routing table that already tracks transaction hashes for P2P broadcast deduplication. Four private flags (`PRIVATE1`–`PRIVATE4`) are reserved in `HashRouterFlags` and aliased internally as `SF_SIGBAD`, `SF_SIGGOOD`, `SF_LOCALBAD`, and `SF_LOCALGOOD`. On the first call for a given transaction ID, `checkValidity` performs `tx.checkSign(rules)` and `passesLocalChecks(tx, reason)`, then records the outcome as flags. Subsequent calls hit the cached flags directly.

This design avoids a separate validity cache data structure and leverages the natural TTL already built into the `HashRouter`'s aged map. The tradeoff is coupling the tx-application layer to a network-layer component, justified by the fact that transactions that pass validity checks are exactly the ones that get routed to peers.

The `Validity` enum encodes a strict three-level hierarchy — `SigBad < SigGoodOnly < Valid` — matching the real dependency between checks: local checks are only worth running if the signature is good.

`forceValidity()` uses a deliberate fallthrough switch to enforce monotonicity: setting `Valid` also sets `SigGoodOnly`'s flag, because you cannot claim local checks pass without first affirming the signature is good. The comment "can only raise the validity to a more valid state" is enforced structurally, not just by convention. An attempt to call it with `SigBad` is a no-op (the flag is never set), and the function is marked with a `@warning` because forcibly declaring a transaction valid bypasses real verification — it exists to allow the transaction queue to mark locally-constructed transactions that were never signed by a remote peer.

## Exception Safety and Fee Guarantee

The `apply()` function guarantees it does not throw. For open ledgers, any exception inside a `Transactor` is caught and returned as `tefEXCEPTION`. For closed ledgers, the design goes further: even if the full application fails, the `Transactor` attempts to charge the fee and returns `tecFAILED_PROCESSING`. If even the fee-charge path throws, that exception is also caught and returned as `tefEXCEPTION`. This "best-effort fee deduction" is a network health requirement — a validator that silently drops a transaction without consuming a fee would allow fee-free spam vectors during consensus.

## Batch Transaction Handling

The implementation in `apply.cpp` exposes an `applyBatchTransactions()` static helper (not declared in the header) that handles the `ttBATCH` transaction type. When `applyTransaction()` sees a successfully applied `ttBATCH` outer transaction, it creates a whole-batch `OpenView` and applies each inner transaction through its own per-transaction `OpenView`. Only if an inner transaction's result is `tes` or `tec` (fee-claiming) are its view changes promoted to the batch view, and only if the batch as a whole succeeds do those changes promote into the main view.

Batch mode flags (`tfAllOrNothing`, `tfUntilFailure`, `tfOnlyOne`) are read from the outer transaction's flags field and control short-circuit behavior during inner transaction iteration.

## `applyTransaction()` — Retry Semantics for the Queue

`applyTransaction()` wraps `apply()` with the retry/fail/success classification that the transaction queue needs. `tefFailure`, `temMalformed`, and `telLocal` errors map to `Fail` (no point retrying in this ledger); everything else that wasn't applied maps to `Retry`. This decoding is what allows the transaction queue to make intelligent decisions about eviction versus holding.

The `retryAssured` parameter controls whether `tapRETRY` is added to the flags. With `tapRETRY` set, `tec` results are treated as soft failures rather than hard fee-claims, which affects whether `preclaim` reports `likelyToClaimFee` — a signal used upstream to decide whether a transaction is safe to relay without applying it to the open ledger first.