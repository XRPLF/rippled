# `src/libxrpl/tx/paths/Flow.cpp`

## Role in the System

`Flow.cpp` is the public entry point for XRPL's payment execution engine. It bridges the high-level transaction processor — which speaks in terms of `STAmount`, `AccountID`, and `STPathSet` — into the fully type-parameterized, template-driven machinery in `StrandFlow.h`. The file is intentionally small: its sole job is to resolve runtime asset types into compile-time template parameters and hand off to the inner engine. All the iterative liquidity-seeking, offer consumption, and sandbox management lives elsewhere.

## The Two Functions

### `finishFlow` (file-local template)

`finishFlow` is a generic cleanup helper that translates a `FlowResult<TIn, TOut>` into the public `path::RippleCalc::Output` type. The critical decision it encodes is the sandbox commit policy: if the inner execution succeeded (`isTesSuccess(f.ter)`), the speculative `PaymentSandbox` is applied to the caller's sandbox, making the ledger mutations permanent in the transaction scope. On failure, the sandbox is simply abandoned — the changes evaporate — but any `removableOffers` discovered during the attempt are still passed back to the caller. This matters for offer crossing, where expired or unfunded offers discovered during a failed payment still need to be cleaned from the ledger. The amounts `in` and `out` are also materialized back to `STAmount` via `toSTAmount()` regardless of success or failure.

### `flow` (the public function)

The public `flow()` function is declared in `Flow.h` and is called by `RippleCalc::rippleCalculate()`. It orchestrates the full payment lifecycle through four well-defined phases:

**Phase 1 — Source asset resolution.** The source asset cannot always be inferred from the delivery asset alone. If `sendMax` is present, the source currency is whatever the sender is spending, which may differ from the delivery currency (cross-currency payment). If `sendMax` is absent, the source asset is derived from `deliver`: XRP stays XRP, IOU becomes an `Issue` with `src` as the issuer (defaulting to the sender's own trust line), and MPT assets pass through unchanged. This logic handles the asymmetry that XRP has no issuer, while IOU issuance must be anchored to a specific account.

**Phase 2 — Strand construction.** `toStrands()` converts the user-supplied `STPathSet` (and the optional default path) into a `std::vector<Strand>`, where each `Strand` is a `std::vector<std::unique_ptr<Step>>`. Steps are polymorphic objects representing one hop in the payment path — account-to-account rippling (`DirectStepI`), order book crossings (`BookStepII`, `BookStepIX`, `BookStepXI`), or XRP/MPT endpoints. If strand construction fails — invalid path, missing trust lines, etc. — `flow` returns immediately with the error TER and zero amounts. No computation is wasted.

**Phase 3 — AMM context initialization.** An `AMMContext` is constructed here and will be passed all the way down to `AMMLiquidity` during order book execution. The context is initialized to `multiPath=false`, then updated with `ammContext.setMultiPath(strands.size() > 1)` once the strand count is known. This matters because the AMM quality function optimization (which can solve for the exact output amount that achieves a target quality) is only valid for single-path payments; multi-path payments disable it. The `AMMContext` also caps total AMM iterations at 30 (`MaxIterations`) to prevent unbounded computation, since AMM offers are not counted in the standard offer counter.

**Phase 4 — Template dispatch via `std::visit`.** The inner `flow<TIn_, TOut_>` function in `StrandFlow.h` is a template parameterized on amount types (`XRPAmount`, `IOUAmount`, `MPTAmount`). This design avoids virtual dispatch and tagged-union overhead on every step of the inner execution loop. However, the public API operates on runtime `STAmount`/`Asset` values. The bridge is `std::visit` on the variant returned by `srcAsset.getAmountType()` and `dstAsset.getAmountType()`. The lambda extracts `TIn_` and `TOut_` from the variant's active type, then calls the typed overload. After execution, `finishFlow` converts the typed result back to the `RippleCalc::Output` struct.

## Design Rationale

The file-level separation of concerns is intentional: `Flow.cpp` handles the type-erased public contract and one-time setup; `StrandFlow.h` owns the iterative algorithm. This means the payment loop in `StrandFlow.h` can be fully optimized by the compiler for each combination of `(TIn, TOut)` — six combinations for the three asset types — without any runtime branching inside the hot loop.

The `sendMax` optionality is handled carefully. It must be converted to the same numeric type as `TInAmt` before being passed to the inner engine, and the conversion is guarded against negative values (a zero or negative `sendMax` leaves `remainingIn` as `nullopt`, effectively unconstrained). The `std::optional<uint256> domainID` parameter threads through to `toStrands` to support permissioned payment domains, where liquidity is restricted to offers within a specific domain.

## Relationship to `StrandFlow.h`

`StrandFlow.h` contains the two templated overloads of `flow` that `Flow.cpp` calls indirectly:

- A **per-strand** `flow<TIn, TOut>(baseView, strand, maxIn, out, j)` that executes a single payment strand using reverse-then-forward passes: first walking backward through the strand to find the required input, detecting the limiting step, then walking forward from the limiter to finalize amounts.
- A **multi-strand** `flow<TIn, TOut>(baseView, strands, outReq, ...)` that manages `ActiveStrands` — a priority queue sorted by theoretical quality upper bound — iterating until `remainingOut` is satisfied or all strands are exhausted. This outer loop caps at 1000 iterations and 1500 offers considered to bound worst-case execution time.

`Flow.cpp` is the façade that makes this template machinery accessible through a single, unambiguous non-template function signature.