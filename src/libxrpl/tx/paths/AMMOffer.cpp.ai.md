# `AMMOffer.cpp` — Synthetic AMM Offer Adapter for XRPL's Payment Engine

## Role in the System

The XRPL payment engine routes payments through a sequence of `BookStep` objects, each of which consumes offers from an order book. Prior to AMM integration, every offer was a `TOffer` backed by a real ledger entry. `AMMOffer` is the bridge that allows an Automated Market Maker pool to participate in that same flow without being a real order-book entry.

The class is intentionally designed as a **structural mirror** of `TOffer`: it exposes the same methods (`assetIn`, `assetOut`, `owner`, `amount`, `consume`, `limitOut`, `limitIn`, `fully_consumed`, `isFunded`, `adjustRates`, `send`, `checkInvariant`) so that `BookStep`'s generic, template-based inner loop handles AMM and CLOB liquidity through the same code paths. The design is duck-typed at the C++ template level — `BookStep` is parameterized on an offer type and calls those methods without knowing which kind of offer it holds.

## Template Parameterization

`AMMOffer<TIn, TOut>` is constrained to types satisfying the `StepAmount` concept, and the file ends with eight explicit instantiations covering every legal pairing of `XRPAmount`, `IOUAmount`, and `MPTAmount`. This approach keeps the bulk of the implementation in `.cpp` rather than headers while still allowing all token-type combinations the ledger supports.

## Core State

The constructor receives four immutable pieces of state:

- **`ammLiquidity_`** — a reference to the `AMMLiquidity` manager that vends offers and holds the pool's `AMMContext`. This reference is the lifeline back to the pool and to the transaction's execution context.
- **`amounts_`** — the initial offer size as seen by `BookStep`. In single-path mode this is set to either a quality-matched size or a pool-draining maximum; in multi-path mode it is a Fibonacci-sequence-scaled amount. After construction, `amounts_` is read-only.
- **`balances_`** — a snapshot of the live pool token balances at the moment the offer was generated. These are used in single-path mode to compute exact swap amounts via the constant-product formula.
- **`quality_`** — either the spot-price quality (when `balances_ != amounts_`) or the amounts quality. In multi-path mode this becomes the fixed proportional rate used when the offer is partially consumed.

## The Single-Path / Multi-Path Duality

The most architecturally significant design choice in this file is the bifurcation inside `limitOut`, `limitIn`, and `getQualityFunc` based on `ammLiquidity_.multiPath()`.

**Single-path mode** (one payment path, no competing paths): the offer can be resized according to the AMM's own conservation function. `limitOut` calls `swapAssetOut(balances_, limit, tradingFee())` and `limitIn` calls `swapAssetIn(balances_, limit, tradingFee())`, which apply the constant-product formula `(x + Δx)(y − Δy) = xy` (adjusted for the trading fee) to compute the exact input or output the pool would require. Because there is only one path, changing the offer's effective quality does not disturb any ordering among strands.

**Multi-path mode** (multiple strands, Fibonacci-sized offers): the AMM offer is deliberately made to behave like a CLOB offer. Resizing is done proportionally to the original quality using `Quality::ceil_out_strict` or `Quality::ceil_in_strict`, which means the taker pays slightly more than the AMM formula alone would demand. The comment in the source explains why: this overshooting causes the post-trade pool product `(poolPays − assetOut)(poolGets + assetIn)` to exceed the original `poolPays × poolGets`, preserving the constant-product invariant even when rounding introduces small errors. If the offer quality were instead recomputed from the live formula at each limiting step, it could shift the relative quality ordering of strands and corrupt the path optimization.

`getQualityFunc` mirrors the same split: for multi-path it returns a constant `QualityFunction` (slope = 0, intercept = quality), just like a CLOB offer; for single-path it returns a proper AMM quality function with a negative slope derived from the pool depth, which the path optimizer uses to find the output amount that meets the payment's requested quality limit.

The `fixReducedOffersV2` amendment gate inside `limitIn` is a precision refinement: when active, it uses the stricter `ceil_in_strict` variant (which removes a small rounding slop) instead of `ceil_in`. The older code path is preserved for ledger replay of historical transactions.

## `consume()` — Intentionally Thin

`consume(view, consumed)` validates that the consumed pair does not exceed the initial offer size, sets the `consumed_` flag, and calls `ammLiquidity_.context().setAMMUsed()` to inform the outer execution context that AMM liquidity was touched in this iteration. Critically, it does **not** modify the pool balances itself. The comment in the source is explicit: actual pool updates are performed in `BookStep::consumeOffer()`, which calls `accountSend` on the AMM account. This keeps the ledger mutation in one place and avoids double-application. The `ApplyView&` parameter is accepted for interface compatibility with `TOffer::consume` but is not used.

The `key()` method returns `std::nullopt` because AMM offers have no ledger object key — they are ephemeral, synthesized per payment iteration.

## The `checkInvariant()` Constant-Product Guard

After each offer execution, `BookStep` calls `checkInvariant`. The method recomputes the pre-trade pool product `k = balances_.in * balances_.out` and the post-trade product `k' = (balances_.in + consumed.in) * (balances_.out − consumed.out)`. The invariant requires `k' >= k`, which should hold exactly for a constant-product AMM. However, finite-precision arithmetic can cause `k'` to fall just below `k`, so the check also passes if the relative deviation is within `1e-7` (`withinRelativeDistance(product, newProduct, Number{1, -7})`). Violations are logged at error level with full balance and product details, enabling post-mortem analysis without aborting a ledger.

## Fee and Transfer-Rate Handling

`adjustRates()` always returns the output transfer rate as `QUALITY_ONE` (no transfer fee), because AMM accounts are exempt from transfer fees on payment transactions. The static `send()` method wraps `accountSend` with `WaiveTransferFee::Yes` and `AllowMPTOverflow::Yes`, reflecting the same policy: AMM funds move at face value regardless of issuer-specified transfer rates, and MPT balance overflow is permitted because the pool is expected to hold enough reserves.

## Relationship to Surrounding Files

`AMMOffer` is constructed exclusively by `AMMLiquidity::getOffer`, which decides the sizing strategy (Fibonacci sequence or quality-matched) and passes the resulting amounts and current pool balances. `BookStep` holds an `std::optional<std::variant<Quality, AMMOffer<TIn, TOut>>>` as its tip and decides at each payment engine iteration whether AMM or CLOB liquidity is better quality. `QualityFunction` encodes the AMM's average quality as a linear function of output, enabling the path optimizer to solve for the optimal output amount in closed form rather than iterating.