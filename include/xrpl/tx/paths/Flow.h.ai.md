# `include/xrpl/tx/paths/Flow.h`

## Role in the System

`Flow.h` declares the single public entry point for executing payments on the XRP Ledger: the `flow()` function. It sits at the boundary between the transaction-processing layer (which decides *whether* to attempt a payment) and the path-engine layer (which decides *how* to route it). Every Payment transaction and offer-crossing operation in rippled ultimately resolves its actual movement of funds through this function.

The file itself is minimal — a forward declaration of `FlowDebugInfo` and one function signature — but it is the linchpin that unifies path-finding, strand execution, AMM liquidity, and sandboxed ledger mutation into a single callable interface.

## The `flow()` Function

```cpp
path::RippleCalc::Output
flow(
    PaymentSandbox& view,
    STAmount const& deliver,
    AccountID const& src,
    AccountID const& dst,
    STPathSet const& paths,
    bool defaultPaths,
    bool partialPayment,
    bool ownerPaysTransferFee,
    OfferCrossing offerCrossing,
    std::optional<Quality> const& limitQuality,
    std::optional<STAmount> const& sendMax,
    std::optional<uint256> const& domainID,
    beast::Journal j,
    path::detail::FlowDebugInfo* flowDebugInfo = nullptr);
```

The return type, `path::RippleCalc::Output`, carries three things: the actual amount consumed from the source (`actualAmountIn`), the actual amount delivered to the destination (`actualAmountOut`), a set of `removableOffers` (unfunded or expired offers discovered during the search), and a `TER` result code. When the result is `tesSUCCESS`, the sandbox has been updated in-place; when it fails, the ledger is untouched.

## Implementation Architecture

The implementation in `Flow.cpp` proceeds in three phases:

**1. Asset type resolution.** The source asset is inferred: if `sendMax` is given, its asset is used; otherwise, for IOU deliver amounts the source asset adopts the sender's account as issuer (implementing the "any issuer from src" semantic), and for MPT the delivery asset is used directly. XRP is a special case throughout.

**2. Strand construction.** The `paths` (an `STPathSet`, the raw path hints from the transaction) are translated into `Strand` objects via `toStrands()`. A `Strand` is a `vector<unique_ptr<Step>>`, where each `Step` is one of several concrete types: `DirectStepI` (IOU-to-IOU between accounts), `BookStepII`/`BookStepIX`/`BookStepXI` (order book crossings between IOU/XRP), `XRPEndpointStep`, `MPTEndpointStep`, and newer MPT-flavored book steps. If `toStrands()` fails, the error is returned immediately with no ledger changes.

**3. Type-dispatched execution.** Because XRP, IOU, and MPT amounts are distinct C++ types, the function uses `std::visit` over the source and destination asset types to instantiate the correct `flow<TIn_, TOut_>()` template from `StrandFlow.h`. This avoids branching inside the hot execution loop and lets the compiler optimize each amount-type combination separately. The inner template runs the actual payment: it iterates over all strands, uses a reverse pass (`Step::rev()`) to determine required input for desired output, then a forward pass (`Step::fwd()`) to commit the execution and handle rounding differences. Only if the overall result is `tesSUCCESS` does `finishFlow()` call `sandbox->apply(sb)` to propagate the speculative changes into the caller's sandbox.

## Parameter Design Decisions

**`PaymentSandbox& view`** — the function receives a mutable reference to a speculative ledger view. All offer consumption, balance changes, and trust line modifications are staged here. The caller owns the sandbox and can choose to commit or discard the result. This is the XRPL's standard mechanism for atomic, all-or-nothing transaction application.

**`OfferCrossing offerCrossing`** — the `OfferCrossing` enum (`no`, `yes`, `sell`) distinguishes three operational modes. Regular payments (`no`) and offer crossing (`yes`/`sell`) share the same strand machinery but differ in fee attribution, quality constraints, and which offers are eligible. Passing this through to every step avoids duplicating the entire engine for offer-crossing.

**`ownerPaysTransferFee`** — normally the sender of a payment pays IOU transfer fees; when crossing offers, fees are charged to the offer owner. This parameter propagates down to `StrandContext` so individual `BookStep` instances can apply the correct fee model without knowing their call context.

**`limitQuality`** — a minimum exchange rate expressed as a `Quality` (output/input ratio). During offer crossing, if a `BookStep` finds that the best available offer quality falls below this threshold it stops consuming liquidity. This enforces the taker's price constraint without requiring the step to enumerate all offers.

**`sendMax`** — an optional upper bound on the sender's spend, independent of the delivery amount. Its presence also drives source-asset inference: if absent, the source asset must be derived from the destination currency and the sender's account.

**`domainID`** — a newer optional parameter supporting domain-scoped order books, where certain AMM or offer book lookups are restricted to a particular domain. It threads all the way down into `StrandContext` and individual `BookStep` constructors.

**`flowDebugInfo`** — a nullable pointer defaulting to `nullptr` in production. When non-null (during testing or diagnostic runs), the inner flow template populates this structure with per-strand, per-step execution traces. The null default means the debug path has zero overhead in normal operation.

## Relationship to `RippleCalc`

`RippleCalc::rippleCalculate()` is the older, pre-Flow path engine still used for certain legacy code paths. `flow()` is the newer replacement and shares the same `Output` type as its return value to remain compatible with the rest of the transaction-processing infrastructure. Both functions operate on a `PaymentSandbox` and both produce actual in/out amounts with a `TER` result, but `flow()` supports MPT, AMM liquidity (via `AMMContext`), and the full step-abstraction model.

## Invariants and Error Handling

If `toStrands()` returns a non-success `TER`, `flow()` constructs a default `Output` with that error and returns without touching the sandbox. Within `finishFlow()`, the sandbox is applied only on success — failure leaves the sandbox pristine. Unfunded and expired offers discovered during a failed payment are collected in `removableOffers` so the transaction processor can still clean them from the ledger even though the payment itself did not go through, maintaining ledger hygiene without complicating the payment semantics.