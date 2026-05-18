# `RippleCalc.cpp` — Payment Path Calculation Entry Point

## Role in the System

`RippleCalc.cpp` is a thin but architecturally significant adapter that sits between the XRPL transaction engine and the lower-level `flow()` payment algorithm. It implements the single static method `RippleCalc::rippleCalculate()`, which is the canonical public entry point for executing a payment across a set of paths. The actual multi-path traversal, liquidity aggregation, and quality optimization all live in `Flow.cpp`; this file's job is to translate high-level transaction parameters into the form `flow()` expects, manage sandbox isolation, and ensure the ledger is never left in a partially-mutated state when something goes wrong.

## The Double-Sandbox Pattern

The most architecturally important design decision in this file is the creation of a *nested* `PaymentSandbox`:

```cpp
PaymentSandbox flowSB(&view);
```

The caller already supplies a `PaymentSandbox& view`, which itself is a copy-on-write overlay over the real ledger state. Rather than letting `flow()` write directly into the caller's view, `rippleCalculate()` wraps it in a second sandbox (`flowSB`) and passes that to `flow()`. Only after `flow()` returns does the code call `flowSB.apply(view)`, promoting any changes to the caller's view.

The reason is exception safety: if `flow()` throws, `flowSB` is destroyed with its mutations intact but unapplied, leaving `view` completely unmodified. Without this intermediate layer, any partial state that `flow()` had already written before throwing would corrupt the caller's sandbox. The `apply()` call sits unconditionally after the try-catch block; `flow()` itself applies its own internal sandbox to `flowSB` only on success (see `finishFlow()` in `Flow.cpp`), so on failure `flowSB` holds no significant mutations and the apply is effectively a no-op.

## Parameter Preprocessing via Lambdas

Before delegating to `flow()`, the function derives three computed values inline:

**`defaultPaths`** and **`partialPayment`** are straightforward null-guard extractions from the optional `pInputs` pointer. When `pInputs` is `nullptr`, the function applies sensible defaults: default paths are enabled, partial payments are not.

**`limitQuality`** is only non-null if the caller has set `pInputs->limitQuality` *and* `saMaxAmountReq > beast::zero`. When both conditions hold, a `Quality` object is constructed from the ratio `Amounts(saMaxAmountReq, saDstAmountReq)` — this ratio represents the maximum acceptable exchange rate (input per unit of output). Paths offering worse quality than this threshold will be skipped by the flow engine.

**`sendMax`** encodes a subtle semantic: when `saMaxAmountReq` is negative (the sentinel value `-1` meaning "no limit"), or when the source and destination assets differ, or when the issuer of the source amount is not the sending account, then `sendMax` is set to the raw `saMaxAmountReq`. In the remaining case — sending the same IOU that the destination receives, with the sender as issuer — `sendMax` is `std::nullopt`. This signals to `flow()` that no separate spending cap needs to be enforced beyond what the delivery target already implies.

## Exception Handling and `tecINTERNAL`

```cpp
catch (std::exception& e)
{
    JLOG(j.error()) << "Exception from flow: " << e.what();
    path::RippleCalc::Output exceptResult;
    exceptResult.setResult(tecINTERNAL);
    return exceptResult;
}
```

Returning `tecINTERNAL` rather than rethrowing is a deliberate choice rooted in how XRPL ledger result codes work. `tec`-class codes cause the transaction to be included in the ledger and the fee to be charged. If instead the function threw or returned a `tem`/`ter`/`tef` code, the transaction might not be stored, creating a discrepancy between nodes that handled the exception and those that didn't. Converting any unexpected exception to `tecINTERNAL` provides a safe, deterministic fallback that every validator will agree on.

## Relationship to `flow()` and `RippleCalc::Output`

The `Output` struct (defined in `RippleCalc.h`) carries the actual amounts moved (`actualAmountIn`, `actualAmountOut`), the final `TER` result code, and a set of `removableOffers` — unfunded or expired offers discovered during path traversal that could not be cleaned up because the payment failed. `rippleCalculate()` returns this struct unmodified from whatever `flow()` produced, but logs the key fields at debug level before returning, which aids in tracing payment behavior in logs.

The `hardcoded false` passed to `flow()` for `ownerPaysTransferFee` and `OfferCrossing::no` for the crossing mode indicate that `rippleCalculate()` is always invoked in the pure-payment context (not offer crossing). Offer crossing has its own dedicated path through `flow()` with different semantics, called directly without going through `RippleCalc`.