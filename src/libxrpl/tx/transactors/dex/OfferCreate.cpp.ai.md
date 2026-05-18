# `OfferCreate.cpp` — DEX Offer Creation Transactor

## Role and Context

This file implements the `OfferCreate` transactor, the core engine behind XRPL's decentralized exchange. When an account submits an `OfferCreate` transaction it is trying to exchange one asset for another at a stated price. This code handles everything from stateless input validation through order-book crossing to placing any residual offer onto the ledger. It sits in the `dex/` transactor sub-directory alongside the AMM family of transactors and `OfferCancel`, and it inherits from `Transactor` — the CRTP-adjacent base that supplies `account_`, `ctx_`, `j_`, and `preFeeBalance_`.

## Three-Phase Execution Pipeline

The XRPL transaction engine drives all transactors through three phases, each a distinct static or virtual method.

**`preflight`** performs stateless, read-free validation. All checks here are cheap and can be done before the ledger view is locked. The checks enforce: `tfHybrid` implies `sfDomainID` must be present (else `temINVALID_FLAG`); `tfImmediateOrCancel` and `tfFillOrKill` are mutually exclusive; `sfExpiration`, if present, must be non-zero; `sfOfferSequence`, if present (for concurrent cancel), must be non-zero; both `sfTakerPays` and `sfTakerGets` must pass `isLegalNet`; neither can be zero or negative; they cannot both be XRP (no native-to-native offer); they cannot reference the same asset (no redundant IOU-for-IOU); and neither can use the reserved bad-currency sentinel. This exhaustive upfront validation avoids touching any state for clearly malformed inputs.

**`checkExtraFeatures`** is called by the base class to gate optional fields behind their governing feature flags: `sfDomainID` is rejected unless `featurePermissionedDEX` is enabled; either amount holding an `MPTIssue` type is rejected unless `featureMPTokensV2` is enabled. The dynamic flags mask returned by `getFlagsMask` similarly adds `tfHybrid` to the prohibited-flag set when `featurePermissionedDEX` is off — preventing hybrid offers before the feature is live.

**`preclaim`** reads ledger state to catch conditions that require an account or object lookup. It verifies: the submitting account exists; neither asset is globally frozen; the account has sufficient funds to partially cover `sfTakerGets` (with a special carve-out for MPT issuers whose `OutstandingAmount ≥ MaximumAmount`); the cancellation sequence, if present, is less than the account's current sequence (preventing cancellation of not-yet-issued offers); the transaction itself has not already expired; if `sfTakerPays` is non-native, `checkAcceptAsset` confirms the submitter is authorized to receive that asset; and if `sfDomainID` is present, the submitter must already be a member of that permissioned domain.

## `checkAcceptAsset` — Authorization and Freeze Gating

This static helper determines whether account `id` may legally hold what it would receive. The logic branches on asset type. For an `Issue`, if the issuer has `lsfRequireAuth` set, it reads the trust line and checks the appropriate authorization bit (using canonical `id > issuer` ordering to pick the correct `lsfLowAuth`/`lsfHighAuth` bit). It also checks for `lsfLowDeepFreeze | lsfHighDeepFreeze` — a newer deep-freeze mechanic where either side of a trust line can prohibit token movement. For `MPTIssue`, it calls `requireAuth` in `WeakAuth` mode, which intentionally skips requiring a pre-existing `MPToken` object because one will be lazily created if the account has the right authorization.

The `tapRETRY` flag governs whether failures return soft `ter` codes (retryable) or hard `tec` codes (fee-consuming). This distinction matters for validation-retry paths in the server.

## `doApply` and the Dual-Sandbox Pattern

`doApply` is the only virtual method — it materialises ledger changes. It creates two `Sandbox` objects wrapping the live ledger view: `sb` accumulates all changes including the crossed amounts and any new offer placed on books; `sbCancel` accumulates only fee payments and the removal of expired/unfunded offers encountered during crossing. The pair is threaded through `applyGuts` and then through `flowCross`. If `applyGuts` returns `result.second == true`, `sb` is committed; otherwise (for `tfFillOrKill` orders that weren't satisfied) `sbCancel` is committed instead, ensuring that stale offers found during the failed attempt are still cleaned up, while no trades and no new order are recorded.

## `applyGuts` — The Main State Machine

`applyGuts` orchestrates the full apply lifecycle:

1. **Cancel prior offer.** If `sfOfferSequence` is present, `offerDelete` removes the named offer from both sandboxes immediately, before any crossing.

2. **Expiry check.** The offer's own `sfExpiration` is re-checked against the current ledger close time. An already-expired offer returns `{tecEXPIRED, true}`, committing the `sb` that includes any cancellation work already done.

3. **Tick-size rounding.** Both the payer's and getter's issuers are checked for `sfTickSize`. The tightest tick size governs. For a sell offer, `saTakerPays` is rounded; for a buy offer, `saTakerGets` is rounded. Either rounding to zero short-circuits with success (order "rounded away"). This step runs before crossing so that the quality stored in order books is the rounded quality.

4. **Offer crossing via `flowCross`.** The amounts are inverted — the offer placer is treated as a taker to leverage the payment engine — and fed to `flowCross`. The `PaymentSandbox` child views created here are applied back into `sb` and `sbCancel` afterward.

5. **Post-cross remainder adjustment.** If the cross was partial, the unfilled amounts are recomputed. For sell mode, the unfilled input is reduced by `actualAmountIn` (net of gateway transfer rate), and output is derived from the preserved `Quality`. For buy mode, the unfilled output is reduced and input is scaled up to maintain quality. Either side going negative triggers a zero clamp with an assertion — the assert is deliberately kept alongside the clamp because this condition should be impossible, but a rounding edge case demands defensive handling.

6. **FillOrKill / ImmediateOrCancel short-circuits.** After crossing, if `tfFillOrKill` is set and the offer is unfilled, `{tecKILLED, false}` commits only `sbCancel`. For `tfImmediateOrCancel`, an unfilled result returns `{tecKILLED, false}`; a partially or fully filled result returns `{tesSUCCESS, true}`.

7. **Reserve check.** The account's pre-fee balance (`preFeeBalance_` from the base class) is compared against the reserve for `ownerCount + 1`. If insufficient, the transaction succeeds only if some crossing actually occurred (the `crossed` flag) — otherwise it returns `tecINSUF_RESERVE_OFFER`. This design allows an offer to cross even if the owner cannot afford to store any remainder.

8. **Book placement.** The offer object is written into its owner directory and order book. The book key is `keylet::quality(keylet::book(book), uRate)` where `uRate` is the original pre-crossing rate — intentionally preserving the submitted price for queue ordering even when partial crossing changed the remaining amounts. If the book did not exist before, `OrderBookDB` is notified.

## `flowCross` — Delegating to the Payment Engine

Rather than maintaining a separate matching loop, `OfferCreate` delegates crossing entirely to `flow()` from the payment paths engine. This unifies the quality-matching, transfer-rate accounting, and multi-hop XRP bridging logic with the code path used by `Payment` transactions.

Key details: if neither leg is XRP, `flowCross` injects an additional path through XRP as an intermediate, enabling two non-XRP assets to cross through a shared XRP order book. For `tfSell` mode, the deliver limit is set to `cMaxNative` or `cMaxValue/2` (IOU) or `maxMPTokenAmount/2` (MPT) — capped at half maximum to accommodate potential 200% gateway transfer rates. The gateway transfer rate is computed upfront and folded into `sendMax` so the payment engine's threshold comparison is accurate. Stale or expired offers surfaced during the crossing (`result.removableOffers`) are deleted from both sandboxes. A `try/catch` wraps the entire call; any uncaught exception logs and returns `tecINTERNAL` rather than propagating.

## Hybrid Offers — Dual Book Placement

A hybrid offer (`tfHybrid`) lives in both a permissioned domain order book and the open order book simultaneously. `applyHybrid` handles the second placement: it asserts `sfDomainID` is present, sets `lsfHybrid` on the offer SLE, computes an open-book `Book` (no domain ID), and calls `sb.dirAppend` with a `setBookDir` callback that deliberately passes `std::nullopt` for the domain so the open book's directory object has no `sfDomainID` field. The two book entries are linked by storing `sfBookDirectory` and `sfBookNode` of the open directory inside an `sfAdditionalBooks` array on the offer SLE. This allows the crossing engine to find the offer from either side of the market.

## `makeTxConsequences` — Fee Reserve Estimation

The custom `TxConsequences` factory declares the maximum possible XRP spend as the `sfTakerGets` amount when it is native, or zero otherwise. This tells the transaction queue the worst-case XRP impact of the offer, enabling safe parallel scheduling across queued transactions.