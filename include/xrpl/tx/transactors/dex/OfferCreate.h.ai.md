# `OfferCreate.h` — DEX Offer Creation Transactor

## Role in the System

`OfferCreate` is the `Transactor` subclass responsible for processing `OfferCreate` transactions on the XRPL decentralized exchange. It sits in the `include/xrpl/tx/transactors/dex/` module alongside AMM transactors and `OfferCancel`, representing the core order-book primitive of the ledger. Its job is deceptively complex: it must validate an offer's fields, optionally cancel an existing offer, attempt to immediately cross against resting orders via the payment engine, and — if any unfilled amount remains — write a new offer ledger entry into the appropriate order book directory.

## Transactor Lifecycle Hooks

The class follows the standard three-phase pipeline mandated by the `Transactor` base class:

**`checkExtraFeatures`** acts as the amendment gate before any field validation. Rather than sprinkling `rules.enabled(featureX)` checks throughout `preflight`, all feature guards live here. It rejects the transaction outright (`temDISABLED`) if `sfDomainID` is present without `featurePermissionedDEX` enabled, or if either amount holds an `MPTIssue` without `featureMPTokensV2`. This keeps `preflight` focused on structural correctness.

**`getFlagsMask`** returns the bitmask of acceptable transaction flags, and does so dynamically. The `tfOfferCreateMask` is defined assuming PermissionedDEX is active. If it is not, `tfHybrid` is OR-ed into the mask — which signals to the base class infrastructure that this flag is *not* permitted, rejecting it in `preflight0` before `preflight` is even called.

**`preflight`** validates the structural integrity of the transaction fields: mutual exclusivity of `tfImmediateOrCancel` and `tfFillOrKill`, non-zero expiration if present, non-zero and non-redundant amounts (no XRP-for-XRP, no same-asset IOUs, no negative amounts), and matching issuer/native consistency. It returns `NotTEC` error codes, meaning failures here cause the transaction to be rejected without charging a fee.

**`preclaim`** runs against a read-only ledger view and checks runtime conditions that can only be evaluated once you can inspect account state: whether involved assets are globally frozen, whether the submitter has sufficient funds, whether a provided cancel sequence is valid relative to the account's current sequence, whether the offer has already expired, whether the submitter is authorized to receive the `TakerPays` asset (`checkAcceptAsset`), and — for domain offers — whether the account is a member of the referenced `PermissionedDEX` domain.

## Custom Consequences

Unlike `OfferCancel`, which uses `ConsequencesFactory{Normal}`, `OfferCreate` declares `ConsequencesFactory{Custom}` and provides `makeTxConsequences`. This matters for transaction queuing: the network needs to know the maximum XRP an offer could consume so it can calculate potential balance impacts. The implementation extracts `sfTakerGets`; if that amount is native (XRP), it reports that XRP value as the upper bound. If the offer is for IOUs, the XRP spend is zero. This avoids over-reserving slot capacity in the transaction queue for IOU-only offers.

## Dual Sandbox Pattern in `doApply`

`doApply` creates two `Sandbox` views over the apply context:

- **`sb`** — the primary working sandbox where the full transaction (crossing, offer placement, directory updates) is applied.
- **`sbCancel`** — a secondary sandbox used only for minimal cleanup: stale or expired offers encountered during crossing are removed here too, so they are purged even if the full offer cannot be placed.

`applyGuts` receives both and returns `{TER, bool}`. The boolean signals which sandbox to commit: `true` commits `sb` (the full result); `false` — used only for `tfFillOrKill` offers that couldn't fully cross — commits `sbCancel` instead. This ensures that when a Fill-or-Kill offer is killed, the ledger still benefits from the housekeeping work done during crossing (removing expired offers) without writing any new offer state.

## Offer Crossing via `flowCross`

The most architecturally significant decision in `OfferCreate` is that it does **not** contain its own offer-crossing loop. Instead, `flowCross` delegates entirely to the payment engine's `flow()` function — the same function used by `Payment` transactions. This is intentional: the payment path-finding code already knows how to walk order book directories, handle partial fills, apply gateway transfer rates, and enforce quality thresholds.

To integrate with `flow()`, `flowCross` inverts `TakerPays` and `TakerGets` (because from the crossing perspective, the offer creator is acting as a taker), computes a quality threshold to enforce the passive flag, and — for IOU-to-IOU offers — injects an XRP intermediate path to enable crossing through two separate books (IOU→XRP→IOU). For `tfSell` offers it passes `STAmount::cMaxNative` or `cMaxValue/2` as the delivery limit, signalling that the taker will accept any amount of the `Gets` asset.

After `flow()` returns, `flowCross` calculates the residual offer amount (what remains to be placed on the book) and adjusts it while preserving the original offer quality (exchange rate). Gateway transfer rates are factored out when computing the non-gateway-consumed amount, so the residual offer accurately reflects the remaining principal.

## `checkAcceptAsset` — Authorization Verification

This static helper determines whether the submitting account is permitted to hold the asset it would receive from a crossing. It handles three cases distinctly:

- **XRP**: asserted invalid at entry (XRP never needs an accept check).
- **IOU with `lsfRequireAuth`**: verifies that a trust line exists and carries the appropriate `lsfLowAuth`/`lsfHighAuth` flag based on the canonical `>` ordering of the account IDs. Also checks `lsfLowDeepFreeze`/`lsfHighDeepFreeze` on any existing trust line.
- **MPT**: delegates to `requireAuth` with `WeakAuth` semantics, meaning an `MPToken` entry need not already exist — it will be created on first receipt.

## Hybrid Offers and `applyHybrid`

The `applyHybrid` method supports the `tfHybrid` flag introduced with PermissionedDEX. A hybrid offer belongs to a domain but is simultaneously indexed in the open order book, allowing it to be crossed by either domain participants or open-market takers. Mechanically, `applyHybrid` sets `lsfHybrid` on the offer ledger entry, creates a second book directory entry (without the domain ID) for the open book, and stores the additional directory reference in `sfAdditionalBooks` on the offer itself. This dual-indexing means the order book walk from the open side can find and consume hybrid domain offers.

## Relationship to Sibling Transactors

Compared to `OfferCancel`, `OfferCreate` is dramatically more involved: `OfferCancel` has no custom consequences, no crossing, and no hybrid logic. The AMM transactors in the same directory (`AMMCreate`, `AMMDeposit`, etc.) serve the automated market maker half of the DEX, while `OfferCreate` and `OfferCancel` serve the classic central-limit-order-book half. The two subsystems share the same `PaymentSandbox` and `flow()` infrastructure for actual asset movement, keeping the execution model consistent across both DEX mechanisms.