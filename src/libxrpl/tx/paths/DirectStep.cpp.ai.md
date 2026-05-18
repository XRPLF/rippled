# `DirectStep.cpp` — IOU-to-IOU Direct Transfer Step

## Role in the System

The XRPL payment engine decomposes a payment into a *strand* — an ordered sequence of `Step` objects, each representing one segment of value flow. `DirectStep.cpp` implements the **direct IOU transfer step**: the case where two accounts that share a trust line exchange the same-currency IOU without routing through an offer book. This is the fundamental rippling operation of the XRPL ledger.

The file lives alongside `BookStep.cpp`, `XRPEndpointStep.cpp`, and `MPTEndpointStep.cpp` in the paths engine. Together these cover every possible segment type in a payment or offer-crossing strand. `DirectStep.cpp` is specifically the IOU↔IOU path between gateway accounts.

## Class Hierarchy

The design applies **CRTP** (Curiously Recurring Template Pattern) to share the bulk of the logic while allowing two divergent behaviours — one for ordinary payments and one for offer crossing — to override specific policy points without virtual dispatch overhead.

`DirectStepI<TDerived>` extends `StepImp<IOUAmount, IOUAmount, DirectStepI<TDerived>>`, which is itself an adapter that bridges the type-erased `Step` interface (accepting and returning `EitherAmount` unions) to typed `revImp` / `fwdImp` methods operating on `IOUAmount` directly. The two concrete subtypes are:

- **`DirectIPaymentStep`**: used for payment transactions. It reads trust-line quality fields, requires a pre-existing trust line, and enforces authorization and dry-path limits.
- **`DirectIOfferCrossingStep`**: used when an offer crosses. It ignores trust-line quality fields entirely ("a long-standing tradition"), does not require a pre-existing trust line, and can exceed the trust-line limit on the final step of the strand.

## Debt Direction: The Core Bookkeeping Concept

A central concept throughout is `DebtDirection`. When account A holds a positive balance on a trust line with account B, A **redeems** — it holds IOUs issued by B and is sending value back toward the issuer. When A holds a negative balance (owes B), A **issues** — it is creating new IOU obligations.

This matters for transfer fees: a transfer fee is charged when the issuer sends their own currency (issues). It is *not* charged when a holder returns IOUs to the issuer (redeems). `debtDirection()` determines this by calling `accountHolds()`: a positive signum means `redeems`, otherwise `issues`. The forward pass can read the direction from the cache when available, saving the ledger lookup.

## Reverse and Forward Passes

The pathfinding engine runs two passes per candidate strand: a **reverse pass** (working backward from the desired output) and a **forward pass** (working forward from the available input). Each step implements `revImp` and `fwdImp` accordingly.

**`revImp`** receives a requested `out` amount and must compute how much `in` is required. It calls the CRTP-derived `maxFlow()` to find the maximum that can flow given current ledger state, then calls `qualities()` to get `srcQOut` (the outgoing quality multiplier on the source side) and `dstQIn` (the incoming quality multiplier on the destination side). The intermediate value `srcToDst` is the amount that actually moves on the trust line — it can differ from `out` if `dstQIn` applies a discount. The result is stored in `cache_`.

**`fwdImp`** receives an actual `in` amount and recomputes the flow, consulting `cache_->srcToDst` as the reference for `maxFlow()`. The twist is that rounding in fixed-point arithmetic can cause the forward pass to derive *slightly larger* amounts than the reverse pass established. The `setCacheLimiting()` function reconciles the two: it takes the minimum of the forward-computed values and the cached values, preserving the invariant that the forward pass never delivers more liquidity than the reverse pass authorised. If the discrepancy exceeds a 1% mantissa ratio threshold, the function logs a warning and accepts the forward values rather than silently clamping them — a defensive choice that prioritises visibility of unexpected behaviour over silent correction.

## Quality Computation

The `qualities()` dispatch method routes to one of two implementations depending on debt direction:

- **`qualitiesSrcRedeems()`**: When the source redeems, `srcQOut` is the max of the trust-line quality-out and the previous step's `lineQualityIn`. This handles the case where the prior step's inbound quality is worse than what the current step's trust line advertises, taking the more conservative figure. `dstQIn` is always `QUALITY_ONE` here.
- **`qualitiesSrcIssues()`**: When the source issues, `srcQOut` becomes the **transfer rate** of the source account if the previous step redeemed (meaning value is transitioning from a redeem step to an issue step, which triggers the transfer fee). `dstQIn` is the trust-line quality-in of the destination, capped at `QUALITY_ONE` on the last step to avoid over-charging the final recipient.

For `DirectIOfferCrossingStep`, the `quality()` override returns `QUALITY_ONE` unconditionally. This is a protocol-level decision: offer quality fields on trust lines are irrelevant during offer crossing.

## Validation and the Two-Phase Check

`check()` operates in two layers. The base `DirectStepI<TDerived>::check()` handles common constraints applicable to both payments and offer crossing:

- Both accounts must be non-null and distinct.
- The source account must exist in the ledger.
- Freeze constraints are checked (with a special carve-out: a single-hop path that is both `isFirst` and `isLast` cannot be frozen, since it represents pure self-issue/redemption).
- When the previous step was also a `DirectStep`, `checkNoRipple` is invoked to enforce the NoRipple flag. An account that sets NoRipple on both sides of a trust line cannot be traversed as an intermediate node.
- Loop detection uses `seenDirectAssets`, a two-element array of flat sets (one for src issues, one for dst issues). The two slots permit the same account to appear as both the source in one step and the destination in another — legitimate in a two-hop path — but not more.

The CRTP-dispatched `check(ctx, sleSrc)` then adds context-specific checks. For payments, `DirectIPaymentStep::check()` verifies the trust line exists (`terNO_LINE`), checks `lsfRequireAuth` against the trust-line auth flag, enforces the NoRipple flag when the previous step was a book step, and confirms the path isn't dry (destination balance at limit). For offer crossing, the override is a no-op: none of these constraints apply.

## Offer Crossing: Intentional Relaxation of Rules

`DirectIOfferCrossingStep::maxFlow()` with `isLast_` set returns `{desired, DebtDirection::issues}` unconditionally, entirely bypassing `maxPaymentFlow()`. This allows an offer crossing to deliver the full desired amount even if it exceeds the trust-line limit — the assumption being that creating an offer signals willingness to receive IOUs at any balance. The `verifyPrevStepDebtDirection()` assert documents an important structural invariant: when a direct step follows a book step during offer crossing, the book step always *issues* (never redeems), and the assert flags any future deviation in that behaviour.

## Factory and Test Interface

`make_DirectStepI()` is the factory entry point called by the strand builder (`toStrand()`). It reads `ctx.offerCrossing` to select the correct concrete type, constructs the step, runs `check()`, and returns the step polymorphically as `std::unique_ptr<Step>`. The `test::directStepEqual()` function in the `test` namespace exposes an introspection hook that downcasts to `DirectStepI<DirectIPaymentStep>` for unit test assertions — a deliberate test-only seam that avoids adding virtual methods to the production interface.