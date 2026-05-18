# `QualityFunction.cpp` — Path Quality as a Linear Function of Output

## Role in the System

During payment path optimization, the XRPL engine needs to determine how much output a given path strand can produce while still satisfying a caller-specified quality limit (minimum exchange rate). `QualityFunction` models the *average quality* of a path step — or a composed chain of steps — as a linear function of the output amount: `q(out) = m * out + b`. This file implements the three non-trivial methods of that class: the CLOB-like constructor, `combine()`, and `outFromAvgQ()`.

The mathematical need for this abstraction arises from AMM liquidity. A CLOB offer has a fixed rate regardless of fill size, but an AMM pool's effective exchange rate degrades as more output is drawn from it (price impact). Modeling quality as a linear function of output lets the path engine efficiently invert the relationship to find the exact output amount that hits a desired quality target, without iterative approximation.

## Two Construction Modes

The class uses tag-dispatch to distinguish two fundamentally different step types.

The `AMMTag` constructor (defined inline in the header) handles a true AMM liquidity step in a single-path scenario. It derives slope and intercept from the AMM's current pool balances and trading fee:

```
m_ = -fee / poolIn
b_ = poolOut * fee / poolIn
```

This follows from substituting the AMM swap-in formula (`in = (poolGets * poolPays) / (poolGets - out) - poolPays`, adjusted for fees) into `q = out / in`, yielding a linear approximation over `out`.

The `CLOBLikeTag` constructor — the one implemented in this `.cpp` — handles two logically different cases that share the same math: a CLOB offer (constant quality at any fill size) and a multi-path AMM offer. In both cases, quality does not vary with output, so `m_ = 0` and `b_ = 1 / quality.rate()`. The intercept stores the reciprocal of the quality rate because the composition arithmetic works in reciprocal-rate space. The constructor guards against a zero rate with a `std::runtime_error`, since `b_` is computed by inversion and a zero rate would be meaningless.

The reason AMM offers in multi-path mode are treated as CLOB-like is deliberate: when an AMM participates alongside other paths, its per-path allocation is fixed proportionally, so its quality is effectively constant from the perspective of that sub-path. The `AMMOffer::getQualityFunc()` makes this dispatch explicitly, returning a `CLOBLikeTag`-constructed function when `ammLiquidity_.multiPath()` is true.

## `combine()`: Composing Steps Across Hops

```cpp
void QualityFunction::combine(QualityFunction const& qf)
{
    m_ += b_ * qf.m_;
    b_ *= qf.b_;
    if (m_ != 0)
        quality_ = std::nullopt;
}
```

When a payment strand has multiple steps (e.g., XRP → USD → EUR), each step contributes its own quality function. `combine()` chains the calling object (the accumulated function so far) with the next step's function. The update rules implement linear function composition in reciprocal-rate space: the combined slope accumulates the product of the prior intercept and the new step's slope, while the combined intercept multiplies. 

The `quality_` field acts as a cached constant-quality marker: it is only populated for pure CLOB-like steps where `m_ = 0`. Once `combine()` is called with an AMM step (where `qf.m_ != 0`), the combined slope becomes nonzero and `quality_` is cleared to `std::nullopt`. This invalidation is checked by `isConst()`, which in turn signals to the calling code in `StrandFlow.h` that the non-trivial `outFromAvgQ()` computation is needed.

## `outFromAvgQ()`: Inverting the Quality Function

```cpp
std::optional<Number> QualityFunction::outFromAvgQ(Quality const& quality)
{
    if (m_ != 0 && quality.rate() != beast::zero)
    {
        saveNumberRoundMode const rm(Number::setround(Number::rounding_mode::upward));
        auto const out = (1 / quality.rate() - b_) / m_;
        if (out <= 0)
            return std::nullopt;
        return out;
    }
    return std::nullopt;
}
```

Given a quality limit (the minimum acceptable average exchange rate), this solves for the output amount `out` at which the path's average quality equals exactly that limit. Algebraically, setting `q(out) = 1/rate` and solving: `out = (1/rate - b_) / m_`. The caller in `StrandFlow.h` uses the result to cap `remainingOut`, ensuring the strand does not produce more output than would violate the quality constraint.

Three guard conditions all return `std::nullopt`:

1. **`m_ == 0`**: The function is constant (CLOB-like), meaning quality doesn't depend on output. There's no meaningful `out` to solve for; the limit either passes or fails uniformly, so no capping is needed.
2. **`quality.rate() == zero`**: Guards against division by zero when forming `1/rate`.  
3. **`out <= 0`**: A non-positive result means the quality limit is already unachievable at any positive output — the path is effectively dead for this quality constraint.

The rounding mode is deliberately set to `upward` during the calculation. Because `out` represents an upper bound on how much output to request, rounding it upward is conservative: the actual quality achieved will be at or *better* than the limit, which is the safe direction. Rounding down would risk requesting slightly more than the path can deliver at the required quality, potentially creating rounding-induced money imbalances in ledger accounting.

## Design Observations

The `saveNumberRoundMode` RAII guard in `outFromAvgQ()` scopes the rounding-mode change to just the single computation that needs it, restoring the previous mode on exit. This is a defensive pattern that prevents the upward-rounding mode from leaking into unrelated arithmetic elsewhere in the call stack.

The deliberate separation of `m_` and `b_` as raw `Number` fields (rather than, say, a `std::pair`) reflects their distinct roles in the linear model: `m_` is the AMM price-impact slope (zero for CLOB steps) and `b_` is the baseline reciprocal-rate intercept. The `quality_` optional serves double duty as both a cached quality value for the CLOB fast-path and a boolean flag (`isConst()` uses `has_value()`), avoiding a separate boolean member.