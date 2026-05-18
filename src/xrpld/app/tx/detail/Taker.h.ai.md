# `src/xrpld/app/tx/detail/Taker.h` — Retired Offer Crossing Header

## Current State

This file is currently **empty** (zero bytes). It is a vacant placeholder at a historically significant path in the XRPL offer-crossing pipeline. The content was removed in commit `433eeabfa` under the message *"chore: Remove unused code after flow cross retirement (#5575)"* — a cleanup pass that followed the retirement of the `FlowCross` amendment.

## What the File Previously Contained

Before removal, `Taker.h` declared two classes and a supporting enum that together formed the original offer-crossing engine for `OfferCreate` transactions.

**`CrossType`** was a scoped enum with three values — `XrpToIou`, `IouToXrp`, and `IouToIou` — classifying the direction of an individual crossing to drive the correct flow calculation branch.

**`BasicTaker`** was an abstract base class that tracked the full state of the active party (the "taker") as it consumed offers from an order book. Its private members included the taker's `AccountID`, a `Quality` representing the submitted offer's exchange rate, a `threshold_` quality below which offers would be rejected, the original and remaining `Amounts`, the input and output `Issue` references, cached transfer `Rate` values for both currency legs, and the `CrossType`. The protected inner struct `BasicTaker::Flow` bundled two `Amounts` — `order` (the amounts transacted between counterparties) and `issuers` (the gross amounts including gateway transfer fees) — with a `sanity_check()` that enforced neither leg could be XRP-to-XRP and all values had to be non-negative.

`BasicTaker` exposed the core crossing logic through three private `flow_*` methods: `flow_xrp_to_iou`, `flow_iou_to_xrp`, and `flow_iou_to_iou`. Each computed the precise flow achievable given the offer's stated amounts, the taker's remaining quantity, the owner's available funds, and the applicable gateway rates. A static `effective_rate()` helper produced the actual rate for a given currency transfer between two accounts, collapsing to `parityRate` when either participant was the issuer. Public methods included `remaining_offer()` (the amount still to be placed after crossing), `reject(Quality)` (quality threshold check), `unfunded()` (taker has run out of input), `done()` (order fully satisfied or unfunded), and the two overloads of `do_cross` — one for direct single-offer crossing and one for bridged crossing through a two-offer XRP intermediate.

**`Taker`** was the concrete subclass that bound `BasicTaker` to an actual `ApplyView` and executed ledger mutations. Its `cross(Offer&)` and `cross(Offer& leg1, Offer& leg2)` methods drove the crossing loop; the private `fill()` overloads translated computed `Flow` values into real ledger operations via `transferXRP`, `redeemIOU`, and `issueIOU`. Alongside this, `Taker` tracked XRP flow through autobridging (`xrp_flow_`) and maintained counters for direct and bridge crossings for diagnostic purposes. The `get_funds()` override queried the ledger view for the actual spendable balance.

## Why It Was Deleted

The `Taker` approach predates the `FlowCross` amendment. It maintained a bespoke offer-matching loop: for each offer consumed from the book, the `Taker` object calculated flows independently and applied transfers directly. This logic existed in parallel with the payment engine's `flow()` function, which could already handle offer crossing as a special case of payment path evaluation.

The `FlowCross` amendment unified offer crossing with the payment engine. Once that amendment was retired (made always-active as of consensus in the codebase, via commit `#5562`), the `Taker` class and its 1,394-line test file (`Taker_test.cpp`) became unreachable dead code. Commit `#5575` removed all of it — `Taker.h`, `Taker.cpp`, the tests, and the call sites in `CreateOffer.cpp`.

## What Replaced It

Offer crossing now delegates entirely to `flowCross()` in `OfferCreate.cpp`, which calls the `flow()` payment engine from `src/libxrpl/tx/paths/Flow.cpp`. The order-book cursor role previously served by `Taker`'s inner loop is now handled by `FlowOfferStream` in `src/libxrpl/tx/paths/OfferStream.cpp`. This consolidation eliminated the duplicated quality-matching, transfer-rate accounting, and XRP autobridging logic that `BasicTaker` previously maintained separately.