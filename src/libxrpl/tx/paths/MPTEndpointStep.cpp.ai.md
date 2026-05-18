# `MPTEndpointStep.cpp` — MPT Endpoint Step for Payment Paths

## Role in the System

This file implements the payment-path step that handles the source or destination account when the asset in motion is a Multi-Party Token (MPT). In the XRPL payment engine, every path through the ledger is decomposed into a chain of `Step` objects. `MPTEndpointStep` is the MPT counterpart to `XRPEndpointStep` and `DirectStepI`, and it represents the edge of a strand where MPT balances are actually moved between an issuer and a holder (or between two holders in a direct payment). It is the last concrete piece needed to make MPT a first-class citizen in cross-currency and direct-payment flows, including DEX offer crossing.

## Class Hierarchy and CRTP Design

The file defines a single CRTP base class `MPTEndpointStep<TDerived>` that inherits from `StepImp<MPTAmount, MPTAmount, MPTEndpointStep<TDerived>>`. Two concrete subclasses are defined in the same translation unit: `MPTEndpointPaymentStep` and `MPTEndpointOfferCrossingStep`. The CRTP pattern — rather than a virtual dispatch on the payment-vs-crossing distinction — is deliberate: it mirrors the identical structure used by `DirectStepI` for IOU steps, and it allows the hot-path methods `revImp`, `fwdImp`, and `qualitiesSrcIssues` to call back into the concrete type's `maxPaymentFlow`, `checkCreateMPT`, and `verifyPrevStepDebtDirection` without any virtual-function overhead. The distinction is resolved once at construction time by `make_MPTEndpointStep`, which dispatches on `ctx.offerCrossing`.

## Construction and Invariant Enforcement

The private constructor captures the `StrandContext` at strand-build time and pre-computes a handful of flags that would otherwise require repeated lookups. Notably, `isDirectBetweenHolders_` is set when the strand delivers this MPT issue, neither strand endpoint is the issuer, and the step is either the first in the strand or its predecessor is not a book step. This flag is later used in `check()` to apply holder-specific frozen/transfer rules. The constructor ends with an `XRPL_ASSERT` that one of `src_` or `dst_` must equal `mptIssue_.getIssuer()` — an invariant that rules out two non-issuer accounts ever appearing directly adjacent in an MPT step, which would be structurally incoherent for a token defined by a single issuer account.

## Two-Phase Validation in `check()`

The base-class `check()` performs structural validation common to both payments and offer crossing: it rejects zero/equal accounts, verifies the source account exists on the ledger, enforces that MPT can only appear as the first or last step in a strand (never a middle hop), checks frozen status for the relevant account depending on position, and detects path loops using `seenBookOuts` and `seenDirectAssets`. After all structural checks pass, it delegates to `static_cast<TDerived const*>(this)->check(ctx, sleSrc)` for context-specific rules.

`MPTEndpointPaymentStep::check()` then applies the full MPT authorization, frozen, and `canTransfer`/`canTrade` rules. For direct holder-to-holder payments it checks `canTransfer` between the active holder and the strand's final destination. For cross-token paths through the DEX it calls `canTrade` instead. It also short-circuits the path with `tecPATH_DRY` if there is no available source balance and no previous step that could create balance first. `MPTEndpointOfferCrossingStep::check()` unconditionally returns `tesSUCCESS` — offer crossing imposes no up-front MPT auth/frozen checks because the book step that precedes it always issues and those checks are deferred to `checkCreateMPT`.

## Reverse and Forward Passes

The payment engine uses a two-pass approach: first a reverse pass (`revImp`) to determine how much input is needed to produce a desired output, then a forward pass (`fwdImp`) to actually execute the flow. Both passes start by calling `maxPaymentFlow()` to determine the maximum transferable amount and the `DebtDirection` (whether the source is issuing or redeeming w.r.t. the issuer). They then call `qualities()` to retrieve `srcQOut` (the effective transfer-rate quality applied to the source's output). Since MPT lacks per-trustline quality fields, `dstQIn` is always `QUALITY_ONE`; only `srcQOut` can differ from unity, and only when the issuer is sourcing tokens and the previous step was redeeming (triggering the MPT transfer rate).

In `revImp`, if the requested output exceeds `maxSrcToDst`, the step becomes the *limiting node* and delivers `maxSrcToDst` instead. In either case the computed `(in, srcToDst, out, srcDebtDir)` tuple is written to `cache_`. The actual ledger mutation is performed by `directSendNoFee()` using `PaymentSandbox`, which journals the transfer without committing until the full path succeeds.

## Cache and Forward-Pass Rounding Guard

Because rounding can cause the forward pass to compute slightly more liquidity than the reverse pass determined was available, `setCacheLimiting()` caps forward-pass results against the cached reverse-pass values. The cap has a deliberate tolerance policy: a difference of exactly one unit is silently corrected; a difference larger than one unit but within 1% is clamped to the cached value; a difference larger than 1% logs a warning at `warn` level and accepts the forward-pass value, allowing the anomaly to be investigated without blocking the transaction. This three-tier approach prevents obscure rounding edge cases from being silently swallowed while also avoiding false panics on normal integer rounding.

`validFwd()` provides a post-execution consistency check: it saves the cache, re-executes `fwdImp` with the same input, and then verifies that the new cache values are "near" (within `checkNear`) the saved values. A `FlowException` during that re-execution is treated as a validation failure. This check is the payment engine's defense against strands that somehow behave differently on repeated execution.

## Offer Crossing: `checkCreateMPT`

The `MPTEndpointOfferCrossingStep::checkCreateMPT()` method handles a detail specific to offer crossing: the offer owner may not have an `MPToken` ledger object for the purchased asset, since the purchase happens atomically during crossing. If the step is `isLast_` (i.e., it represents TakerPays in offer-crossing terms), the method calls `xrpl::checkCreateMPT()` to create the object if necessary. The comment in the code explicitly notes that the reserve check is intentionally waived here because the offer never goes on the books when it crosses — `CreateOffer::applyGuts()` handles reserve separately. The payment variant, `MPTEndpointPaymentStep::checkCreateMPT()`, is a static no-op returning `tesSUCCESS`.

## Factory and Test Utility

`make_MPTEndpointStep()` is the external entry point. It instantiates either `MPTEndpointPaymentStep` or `MPTEndpointOfferCrossingStep` based on `ctx.offerCrossing`, immediately calls `check()`, and returns a `{TER, std::unique_ptr<Step>}` pair. If `check()` fails, a null pointer is returned. The `test::mptEndpointStepEqual()` function in the nested `test` namespace provides structural equality testing by downcasting through the `MPTEndpointPaymentStep` template specialization — only the payment variant is matched, which is sufficient since tests construct payment steps when verifying strand composition.