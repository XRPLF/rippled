# `RippleCalc.h` — Payment Path Quality Engine

`RippleCalc` is the authoritative entry point for computing how funds move through the XRPL's trust-line and order-book network during a payment transaction. Its core responsibility is "quality" in the XRPL sense: given a requested output amount, determine how much input is actually required to deliver that output along a set of candidate paths — i.e., the effective exchange rate.

## Architecture and Role

The class sits at the boundary between transaction processing and the low-level path-flow engine. A Payment transaction handler calls the static `rippleCalculate()` factory, which orchestrates the full calculation without permanently modifying the ledger. The design deliberately separates concern:

- The **caller** owns the `PaymentSandbox` and is responsible for applying the results to the real ledger.
- `RippleCalc` spawns a **nested** `PaymentSandbox` (`flowSB`) wrapping the caller's view, feeds it into `flow()`, then calls `flowSB.apply(view)` only after a successful computation. This nesting means the outer sandbox can still be discarded entirely if the enclosing transaction fails, preserving atomicity.

The `flow()` function (declared in `Flow.h`, returning the same `RippleCalc::Output` type) does the heavy lifting — it walks paths, crosses order books, handles AMM liquidity, and aggregates actual in/out amounts. `rippleCalculate()` itself is a thin adapter that translates the `Input` flags into `flow()`'s parameter conventions and catches any exception from path computation, converting it to a `tecINTERNAL` result so the transaction is stored rather than silently dropped.

## `Input` Flags

The `Input` struct bundles four boolean knobs that alter calculation behavior:

- **`partialPaymentAllowed`**: If the paths cannot deliver the full requested amount, allow delivery of a lesser amount rather than failing the payment outright.
- **`defaultPathsAllowed`**: Include the implicit direct path (sender → receiver) in addition to any explicitly specified paths. Defaulting to `true` matches normal payment semantics.
- **`limitQuality`**: When `true` and `saMaxAmountReq` is positive, the engine computes a minimum acceptable `Quality` threshold (`Amounts(saMaxAmountReq, saDstAmountReq)`) and rejects any liquidity below that rate. This prevents accepting worse exchange rates than the sender specified.
- **`isLedgerOpen`**: Distinguishes open-ledger (pre-consensus) processing from closed-ledger validation, which can affect rounding and fee logic downstream.

A null `pInputs` pointer is a valid call — the implementation treats it as the conservative default (no partial payment, default paths enabled, no quality limit).

## `Output` — Results and Side Effects

`Output` captures two categories of information: the computed amounts and the set of stale offers discovered during traversal.

`actualAmountIn` and `actualAmountOut` are the true amounts consumed and delivered — these may differ from the requested amounts when a partial payment is allowed or when rounding applies. The `calculationResult_` field is deliberately private with controlled access through `result()` / `setResult()`, preventing callers from accidentally stamping a success code over an internally-set error.

`removableOffers` is a `boost::container::flat_set<uint256>` holding offer IDs that were found to be expired or unfunded during path traversal. When a payment **succeeds**, those offers are deleted from the ledger as a side effect. When a payment **fails**, the ledger is not modified, but this set is returned so that offer-crossing logic (which operates differently) can still clean up the stale state. The flat_set provides ordered, compact storage suitable for deterministic iteration.

## `permanentlyUnfundedOffers_`

The public member `permanentlyUnfundedOffers_` on the `RippleCalc` class itself (not on `Output`) tracks offers that must be removed regardless of payment outcome — offers that are structurally unfunded rather than merely temporarily so. The comment stresses that removal must happen in a **deterministic order**, which is why an ordered container is used rather than an unordered set. This invariant is required for consensus: every validator must clean up the same offers in the same sequence to produce identical ledger hashes.

## Domain-Scoped Payments and Service Registry

The `domainID` optional parameter and `ServiceRegistry& registry` are extensions beyond the base XRPL payment model. They allow payment routing to be scoped to a named domain — a permissioned sub-network of trust lines and liquidity — rather than the global trust-line graph. The `registry` supplies cross-cutting services like the journal logger (`registry.getJournal("Flow")`), decoupling `RippleCalc` from global singletons and making the computation testable in isolation.

## Design Choices Worth Noting

`rippleCalculate()` is `static` and returns by value, making `RippleCalc` objects lightweight — the class holds only a `view` reference and the `permanentlyUnfundedOffers_` set. This avoids heap allocation for the common case and keeps the call site simple: no object lifetime to manage, no factory indirection. The exception catch around `flow()` returning `tecINTERNAL` is a pragmatic safety net; `flow()` should not throw in normal operation, but the ledger cannot tolerate an unhandled exception escaping transaction processing.