# `XRPEndpointStep.cpp` — XRP Source/Destination in the Payment Path Engine

## Role in the System

Every payment through the XRPL pathfinding engine is represented as a **strand**: an ordered sequence of `Step` objects that transform one currency into another until the destination receives the intended asset. When XRP is either the currency being sent or the currency being received, the strand begins or ends with an `XRPEndpointStep`. This file implements that step — the bookend that connects a real account's XRP balance to the abstract flow graph used by the path engine (`Flow.cpp`, `StrandFlow.h`).

Unlike `DirectStep` (which handles IOU-to-IOU trust-line transfers) or `BookStep` (which traverses an order book), `XRPEndpointStep` does no currency conversion. Its sole job is to debit or credit XRP from a concrete account and hand the amount to (or receive it from) the virtual `xrpAccount()` sentinel that represents XRP as a fungible in-flight quantity.

## CRTP-Based Policy Split: Payments vs. Offer Crossing

The file defines one CRTP base class template, `XRPEndpointStep<TDerived>`, and two concrete subclasses that supply a single behavioral variation: `xrpLiquid()`.

```
XRPEndpointStep<TDerived>          (CRTP base, all logic lives here)
  ├─ XRPEndpointPaymentStep        (payments — full reserve applies)
  └─ XRPEndpointOfferCrossingStep  (offer crossing — reduced reserve)
```

The reason for the split is a long-standing ledger rule: when an offer crosses and the buyer doesn't yet hold a trust line (or MPT holding) for the delivered asset, the system knows a new ledger object will be created, consuming one reserve increment. So the buyer is allowed to spend one reserve unit more of XRP than normal. `XRPEndpointOfferCrossingStep` captures this by computing a `reserveReduction_` at construction time — calling `computeReserveReduction()` — and passing it into `xrpLiquidImpl()` as a negative reserve offset. The payment variant always passes `0`.

Rather than embedding this policy in a runtime `if`/`else`, the CRTP pattern makes the choice a compile-time dispatch through `static_cast<TDerived const*>(this)->xrpLiquid(sb)`. Both `revImp()` and `fwdImp()` call `xrpLiquid()` this way, so there is zero virtual overhead on the hot path.

## The Single-Cache Design

Most `Step` subclasses maintain separate `cachedIn` and `cachedOut` fields because their forward and reverse amounts differ (an order book step consumes offers at different rates depending on direction). For `XRPEndpointStep`, this is unnecessary: XRP moves 1:1, so input equals output always. The comment at the field declaration makes this explicit:

> *Since this step will always be an endpoint in a strand (either the first or last step) the same cache is used for cachedIn and cachedOut and only one will ever be used.*

`cache_` is a single `std::optional<XRPAmount>`. Both `cachedIn()` and `cachedOut()` return the same `cached()` helper, which wraps it in an `EitherAmount`. This is legitimate because a step that is `isFirst` only ever executes as a sender (only `cachedOut` is consumed by the next step), while a step that is `isLast` only ever executes as a receiver (only `cachedIn` is consumed by the previous step).

## Reverse and Forward Execution

The path engine first executes a **reverse pass** (`revImp`) — walking the strand from destination to source to find how much input is needed — and then a **forward pass** (`fwdImp`) that commits the actual transfer. The cache bridges these two passes.

In `revImp`, the direction of transfer is encoded directly in `isLast_`:
- If this is the **last step** (XRP receiver), it accepts the full requested `out` unconditionally — the engine has already established that the amount is valid, and a receiving account can always accept XRP.
- If this is the **first step** (XRP sender), it caps at `std::min(balance, out)` where `balance = xrpLiquid(sb)`. This is the spendable balance: total XRP minus the base reserve, minus the owner reserve for each ledger object.

`fwdImp` mirrors this structure. It asserts that `cache_` is populated (guaranteeing `revImp` ran first), then applies the same balance-capping logic before calling `accountSend()`. If `accountSend()` fails, both methods return `{zero, zero}` to signal the strand is dry.

The sender/receiver pair passed to `accountSend()` is selected at runtime using `isLast_`:

```cpp
auto& sender   = isLast_ ? xrpAccount() : acc_;
auto& receiver = isLast_ ? acc_         : xrpAccount();
```

`xrpAccount()` is the canonical sentinel representing the XRP network itself — passing it to `accountSend()` denotes a "burn" or "mint" in the abstract payment sandbox rather than a real account debit.

## Validation in `check()`

`check()` runs once at strand construction time (not during execution). It enforces four invariants:

1. **Account existence** — the `acc_` must resolve to a live ledger account object. Absent accounts return `terNO_ACCOUNT`.
2. **Endpoint-only constraint** — the step must be either first or last in the strand (`!ctx.isFirst && !ctx.isLast` returns `temBAD_PATH`). XRP cannot be an intermediate currency in a multi-hop path.
3. **Freeze check** — `checkFreeze()` inspects the source-to-destination direction for global account freeze, per-trustline directional freeze, and deep-freeze flags. An XRP transfer may still be blocked if the destination account itself is globally frozen.
4. **Loop detection** — `ctx.seenDirectAssets` is a two-element array tracking which currencies have already appeared on each side of the strand. Inserting `xrpIssue()` into the appropriate slot and failing if the insert returns `false` prevents a malformed path from looping through XRP twice.

## `qualityUpperBound()` and `debtDirection()`

Quality in the path engine is the ratio of output to input. Since `XRPEndpointStep` is 1:1, it always returns `Quality{STAmount::uRateOne}` (rate = 1.0). This is used by the engine to pre-filter strands with a worse overall quality than `limitQuality`.

`debtDirection()` always returns `DebtDirection::issues`. The debt direction concept (issues vs. redeems) is relevant to IOU trust lines where one party may be the issuer. XRP has no issuer, so it is always in the "issues" state — it never redeems toward a counterparty.

## `validFwd()` — Pre-Commit Sanity Check

Between the reverse and forward passes, the engine calls `validFwd()` to verify the incoming amount against the cached expectation. For a first-step sender, it re-checks that `balance >= xrpIn`, warning if balance has shifted since the reverse pass (which could happen if another strand modified the sandbox). If the amounts diverge from the cache, a warning is logged but the step still returns `true` — the discrepancy is noted but not fatal, deferring error handling to `fwdImp`.

## Factory and Test Helper

`make_XRPEndpointStep()` is the sole public entry point. It reads `ctx.offerCrossing` and instantiates the appropriate subclass, runs `check()`, and returns a `std::unique_ptr<Step>` on success or `{ter, nullptr}` on failure. This keeps the two-phase construction (allocate, then validate) predictable for callers.

The `test::xrpEndpointStepEqual()` function in the nested `test` namespace exists solely so unit tests can downcast a `Step*` to `XRPEndpointPaymentStep` and verify the account identity without exposing the private type hierarchy to test code.