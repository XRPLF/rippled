# `QualityFunction.h` — Path Quality as a Linear Function of Output

## Role in the System

When the XRPL payment engine routes a payment through a strand that includes an AMM pool, the effective exchange rate (quality) is not constant — it degrades as more liquidity is consumed from the pool. `QualityFunction` models this relationship analytically, expressing the *average quality* of a path as a linear function of its output amount: `q(out) = m * out + b`. This model lets the engine compute, without simulation, exactly how much output a path can deliver while still satisfying a minimum quality (rate) requirement.

The class lives at the boundary between AMM mathematics and path optimization. It is not a generic utility; it exists specifically to power the `limitOut()` optimization in `StrandFlow.h`, which caps strand output at the point where further consumption would violate the quality limit.

## Mathematical Foundation

For a single AMM step with current pool balances `poolPays` (output side) and `poolGets` (input side) and a trading fee factor `cfee = 1 - tfee`, the constant-product swap formula gives:

```
in = [(poolGets * poolPays) / (poolGets - out) - poolPays] / cfee
```

Substituting into `q = out / in` and linearising yields the slope and intercept stored in `m_` and `b_`:

```
m = -cfee / poolGets      (always negative for a valid AMM step)
b = poolGets * cfee / poolGets  =  cfee * poolPays / poolGets
```

The template constructor (`AMMTag`) computes this directly from the `TAmounts<TIn, TOut>` pool snapshot at the moment the step is evaluated, guarded by an explicit throw if either balance is zero — a degenerate pool would produce division-by-zero in downstream arithmetic.

## Construction: Two Modes via Tag Dispatch

Two empty tag structs, `AMMTag` and `CLOBLikeTag`, select construction semantics without relying on parameter-type overloading:

- **`CLOBLikeTag`** produces a *constant* quality function (`m_ = 0`, `b_ = 1 / quality.rate()`), with `quality_` set. This covers two cases: a plain CLOB order whose rate doesn't change with output size, and a multi-path AMM offer — where the AMM offer size scales proportionally with quality just like a CLOB, so the function is effectively flat. Constructing with a zero-rate quality throws immediately, since a zero rate is undefined as a divisor.

- **`AMMTag`** produces a *variable* quality function with the slope and intercept derived from pool balances. `quality_` is left empty, marking the function as non-constant.

Tag dispatch here is idiomatic: call sites read clearly (`QualityFunction{q, QualityFunction::CLOBLikeTag{}}`) without needing to inspect argument types.

## Combining Across Path Steps

A payment strand can have multiple sequential steps (e.g., a transfer fee step preceding an AMM step). `combine()` composes two quality functions using the linear chain rule:

```cpp
m_ += b_ * qf.m_;
b_ *= qf.b_;
if (m_ != 0)
    quality_ = std::nullopt;
```

This is the analytic composition of `q1(out)` and `q2(out)` — the combined function represents the average quality across both steps as a function of the final output. If the incoming QF was constant but the new step introduces a slope, `quality_` is cleared to correctly reflect that the combined function is no longer flat. `StrandFlow.h`'s `limitOut()` calls this in a loop over all strand steps, building up a single QF that represents the entire strand.

## Inverting the Function: `outFromAvgQ()`

Given a quality limit `qlim`, the maximum output that still satisfies `q(out) >= qlim` is found by inverting the linear model:

```
out = (1 / qlim.rate() - b_) / m_
```

The implementation sets the rounding mode to `upward` before computing the expression. Because `m_` is negative for AMM steps, dividing by it converts an upward-rounded numerator into a downward-rounded result, ensuring the computed output doesn't slightly exceed the limit — a defensive choice to prevent the engine from requesting marginally more output than the quality constraint allows.

If `m_` is zero (constant quality function) or the result is non-positive (quality limit cannot be reached with positive output), `std::nullopt` is returned. `StrandFlow.h` treats `std::nullopt` or `isConst() == true` as a signal to skip the cap and pass `remainingOut` unchanged.

## Integration with `StrandFlow.h`

`limitOut()` in `StrandFlow.h` is the primary consumer:

```cpp
for (auto const& step : strand)
    if (auto [stepQF, dir] = step->getQualityFunc(v, dir); stepQF)
        qf ? qf->combine(*stepQF) : qf = stepQF;

if (!qf || qf->isConst())
    return remainingOut;

auto out = qf->outFromAvgQ(limitQuality);
return std::min(out, remainingOut);
```

Each `BookStep` delegates to `tipOfferQualityF()`, which dispatches to `AMMOffer::getQualityFunc()` or wraps a CLOB offer in a `CLOBLikeTag` function. If any step returns no quality function (e.g., a non-offer step), the optimization is abandoned entirely and `remainingOut` is used unchanged. A tiny tolerance check (`withinRelativeDistance` to 1e-9) guards against floating-point noise producing spurious clipping.

## Design Tradeoffs

The linear approximation of AMM quality is exact for the *average* quality (`out / in`) but not for the *marginal* (instantaneous) quality, which is quadratic. The comment in `StrandFlow.h` is explicit about this: "average quality is linear and instant quality is quadratic function of output." Using the average keeps the composition rule simple (linear × linear = linear after algebraic manipulation) and avoids solving quadratics during path selection. The result is a conservative, analytically tractable bound that the engine then enforces precisely when it executes the actual swap.