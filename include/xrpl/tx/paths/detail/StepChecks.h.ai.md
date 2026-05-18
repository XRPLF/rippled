# `StepChecks.h` — Freeze and NoRipple Guards for Payment Path Steps

This header defines two inline validation predicates that form the front-line compliance checks during payment pathfinding. Every candidate path step through the XRPL ledger must clear both tests before a payment engine can use it; failing either one aborts path evaluation for that step with a retriable (`ter`-family) error code.

## Role in the Payment Engine

The XRPL payment engine represents multi-hop paths as a sequence of `Step` objects (defined in `Steps.h`). Each concrete step type — `DirectStepI` for IOU-to-IOU hops, `XRPEndpointStep` for terminal XRP legs, and so on — calls into these two functions from its `check()` method during path validation. The functions are inlined here rather than compiled into a translation unit because every step type includes this header directly, and the logic is short enough that the call overhead would be non-trivial relative to the body.

## `checkFreeze`

```cpp
TER checkFreeze(ReadView const& view, AccountID const& src,
                AccountID const& dst, Currency const& currency)
```

This function answers: *is this trust line currently blocked by any freeze mechanism?* It enforces three distinct freeze layers in order:

**Global freeze.** If the destination account has the `lsfGlobalFreeze` flag set, every IOU it issues is inaccessible. This is the nuclear option issuers use to halt all transfers during a crisis. The check is on `dst` rather than `src` because it is the issuer who declares a global freeze, and payments routed *toward* a globally-frozen issuer must be blocked.

**Per-trust-line directional freeze.** A trust line between two accounts has a "high" side (the account whose `AccountID` is numerically larger) and a "low" side. Each side may independently freeze the line with `lsfHighFreeze` or `lsfLowFreeze`. The comparison `(dst > src) ? lsfHighFreeze : lsfLowFreeze` selects the correct flag based on which side `dst` occupies in the canonical ordering. This asymmetry is intentional: an issuer can freeze a customer's trust line without affecting their own ability to redeem from the customer.

**Deep freeze.** Introduced more recently, `lsfHighDeepFreeze` and `lsfLowDeepFreeze` are checked unconditionally — the function returns `terNO_LINE` if *either* side's deep-freeze bit is set, regardless of directionality. Deep freeze is intended for scenarios where the issuer wants to completely prohibit any movement on the line from either direction, unlike a regular freeze which still permits outbound transfers from the freezing side.

**AMM pool freeze via `fixFrozenLPTokenTransfer`.** When this amendment is active, there is a fourth check: if `dst` is itself an AMM account (identified by the presence of `sfAMMID` on its ledger entry), the function reads the corresponding AMM object and calls `isLPTokenFrozen()` to test whether the underlying pool assets are frozen. This check was added to close a gap where LP tokens representing a frozen pool could still be transferred by routing through the AMM account directly. A missing AMM ledger entry causes `tecINTERNAL` (marked `LCOV_EXCL_LINE` because it would indicate a ledger corruption invariant violation).

The assertion `src != dst` at the top catches programmer error — a self-loop would make the flag-selection arithmetic meaningless and should never reach this function in a valid path.

In `DirectStep.cpp`, the freeze check is intentionally skipped when `ctx.isFirst && ctx.isLast` are both true, meaning the step is simultaneously the first and last hop — a pure issue or redeem between the transaction's ultimate source and destination. That bilateral relationship is inherently authorized and cannot be frozen.

## `checkNoRipple`

```cpp
TER checkNoRipple(ReadView const& view, AccountID const& prev,
                  AccountID const& cur, AccountID const& next,
                  Currency const& currency, beast::Journal j)
```

NoRipple is an account-level setting that says "do not let payments pass through my trust lines without my explicit blessing." `checkNoRipple` evaluates whether the intermediate account `cur` in the triple (`prev` → `cur` → `next`) has blocked transit.

The key insight is that NoRipple only prevents transit when **both** the incoming and outgoing trust lines for `cur` have the flag set from `cur`'s perspective. The flag is checked directionally, again using the `high`/`low` convention: `(cur > prev) ? lsfHighNoRipple : lsfLowNoRipple` extracts the NoRipple bit from the `prev`↔`cur` line, and similarly for `cur`↔`next`. If both lines block rippling, the path through `cur` is rejected with `terNO_RIPPLE`.

This AND semantics is deliberate. A user may have one trust line to a major exchange (with NoRipple off, to participate in the payment network) and other lines to counterparties they wish to keep isolated (with NoRipple on). Transit is still permitted as long as at least one side of the intermediate account's path is "open." This lets intermediate accounts selectively participate in payment routing.

Both trust lines must actually exist; if either is missing, `terNO_LINE` is returned — there is no line to route through in the first place. A diagnostic log at `info` level records which three accounts violated the constraint, which is useful for debugging path-finding failures.

The function takes a `beast::Journal` precisely for this logging; unlike `checkFreeze`, which is a pure state query, `checkNoRipple` has an observational side-effect that helps operators trace why a seemingly valid path was rejected.

## Design Notes

Both functions take `ReadView const&` rather than a mutable view, signaling that path validation is a read-only probe — no ledger state is modified during the check phase. The `inline` linkage means each calling translation unit embeds its own copy, avoiding a shared-library dependency for two small functions that are on the hot path of payment execution.

The `ter`-prefixed return codes (`terNO_LINE`, `terNO_RIPPLE`) are retriable transaction errors rather than fatal ones (`tec`/`tef`), reflecting that a frozen or noRipple-blocked path is not an error in the transaction itself — the engine should simply try the next candidate path before giving up.