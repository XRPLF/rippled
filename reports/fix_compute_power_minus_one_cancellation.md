# Fix: Near-zero-rate catastrophic cancellation in `computePaymentFactor`

Accompanies the original bug report
`compute_payment_components_interest_due_delta.pdf` in this directory. That
report documented an assertion abort in `computePaymentComponents`; the
analysis below traces the bug to its root in `loanPrincipalFromPeriodicPayment`
/ `computePaymentFactor` and describes the fix.

## Summary

`computePaymentFactor(r, n)` evaluates the amortization factor
`r · (1+r)^n / ((1+r)^n - 1)`. The subexpression `(1+r)^n - 1` was being
computed as the naive subtraction
`power(1 + r, n) - 1`, which suffers **catastrophic cancellation** at
near-zero `r·n`: `(1+r)^n` rounds to a value very close to 1, and subtracting
1 discards most of Number's significant digits. At the assertion-triggering
test case (`r ≈ 1.9e-10`, `n = 2`) the helper returned a principal
**larger** than `periodicPayment × paymentsRemaining`, which propagated into
`computeTheoreticalLoanState` as a **negative** `interestDue` — a
physically nonsensical state that fired the assertion at
`LendingHelpers.cpp:985` (pre-fix).

## Root cause

For small `r`, the closed-form expansion
`(1+r)^n - 1 ≈ n·r + C(n,2)·r² + ...`
is well-approximated by `n·r`. But the direct evaluation:

1. Computes `raisedRate = (1+r)^n` — a value of the form `1 + small`.
2. Subtracts 1 from `raisedRate` — a classic cancellation.

Number's large-mantissa range holds 19 significant digits. After step 1, the
leading "1" consumes roughly `log10(1 / (r·n))` of those digits, leaving
`19 − log10(1 / (r·n))` digits of `r·n` precision. Repeated squaring in
`power` contributes an additional `~log2(n)` ULPs of relative error.

At `r·n ≈ 3.8e-10`, only ~9 significant digits of `(1+r)^n - 1` survive the
subtraction, and downstream algebra in `computePaymentFactor` amplifies the
residual error to a relative overshoot of ~2.7e-8 on `principal`. That was
enough to push `principal > value` by 8.77e-9 and flip `interestDue`
negative.

## Fix

Introduce a numerically-stable evaluator of `(1+r)^n - 1`:

```cpp
Number
computePowerMinusOne(Number const& r, std::uint32_t n)
{
    // Binomial expansion:
    //   (1+r)^n - 1 = Σ_{k=1..n} C(n,k) · r^k = n·r + C(n,2)·r² + ...
    // All positive terms — no cancellation. Early-terminate once a term
    // falls below Number precision (typically < 10 iterations for the
    // regime where this path is used).
    ...
}
```

and a hybrid dispatcher that routes based on `r·n`:

```cpp
Number
computePowerMinusOneHybrid(Number const& r, std::uint32_t n)
{
    static Number const cancellationThreshold{1, -9};  // 1e-9
    if (Number{n} * r >= cancellationThreshold)
        return power(Number{1} + r, n) - Number{1};
    return computePowerMinusOne(r, n);
}
```

`computePaymentFactor` was changed to call the hybrid instead of computing
`raisedRate - 1` directly.

### Why 1e-9

- Number's large-mantissa holds 19 sig digits.
- Post-subtract, `(1+r)^n - 1` retains roughly `19 − log10(1/(r·n))`
  digits.
- `power` via repeated squaring adds `~log2(n)/log10(2)` ULPs of noise.
- To retain `~10` digits of precision across realistic `n` up to `~10^6`,
  the threshold lands at `r·n ≈ 1e-9`. Above it, the closed form is both
  accurate and ~30–500× faster than the binomial expansion.

### Why not always binomial

At non-tiny `r·n`, the binomial series needs many terms before early
termination — each term involves several Number multiplications/divisions.
For a stress-test input of `r = 0.1, n = 100_000`, the pure-binomial path
takes ~19 ms to converge, versus ~40 µs for the closed form on the same
inputs. Since that performance ratio worsens with `n` (and the closed form
is perfectly accurate at such rates), the hybrid preserves closed-form's
speed wherever cancellation isn't a problem.

## Empirical validation

Stress test `testComputePowerMinusOnePerformance` in
`LendingHelpers_test.cpp` compares all three paths across the
`(periodicRate, paymentsRemaining)` parameter space:

| Scenario               |   `r·n` |        Taylor |              **Hybrid** | ClosedForm |
| ---------------------- | ------: | ------------: | ----------------------: | ---------: |
| near-zero (bug regime) | 3.8e-10 |          7 µs | **11 µs** (Taylor path) |       5 µs |
| very small             |  2.3e-8 |         52 µs | **42 µs** (Taylor path) |      19 µs |
| moderate 0.228%, n=12  |   0.027 |         84 µs | **18 µs** (Closed path) |      13 µs |
| moderate 0.1%, n=1000  |       1 |        199 µs |      **49 µs** (Closed) |      44 µs |
| moderate 0.1%, n=100k  |     100 |      1,095 µs |      **19 µs** (Closed) |      14 µs |
| small 0.01%, n=1M      |     100 |        409 µs |      **19 µs** (Closed) |      18 µs |
| moderate 0.1%, n=1M    |    1000 |      2,690 µs |      **21 µs** (Closed) |      21 µs |
| high 10%, n=1000       |     100 |        381 µs |      **11 µs** (Closed) |      10 µs |
| **high 10%, n=100k**   |   10000 | **19,267 µs** |      **44 µs** (Closed) |      40 µs |

Observations:

- Hybrid picks Taylor only in the buggy regime (`r·n < 1e-9`); picks
  closed-form elsewhere.
- At the pathological high-rate / large-n corner, hybrid is **~440× faster**
  than the pure-Taylor implementation.
- Closed-form path in hybrid produces **bit-exact identical** output to
  the original closed-form evaluation — no fixture drift anywhere.

## Test coverage added

- `testLoanPrincipalFromPeriodicPaymentNearZeroRate` — regression guard
  asserting `principal ≤ payment × n` across the schedule at the bug's
  parameters (previously this bound was violated).
- `testComputeTheoreticalLoanStateNearZeroRate` — regression guard
  asserting `interestDue ≥ 0` and `principalOutstanding ≤ valueOutstanding`
  at the bug's parameters.
- `testComputePowerMinusOnePerformance` — stress benchmark and sanity
  check across the `(r, n)` envelope.
- `testBugInterestDueDeltaCrash` in `Loan_test.cpp` (the full end-to-end
  reproduction) — previously aborted, now passes cleanly.

## Files changed

- `include/xrpl/tx/transactors/lending/LendingHelpers.h` — declarations of
  `computePowerMinusOne` and `computePowerMinusOneHybrid`; removed dead
  `computeRaisedRate`.
- `src/libxrpl/tx/transactors/lending/LendingHelpers.cpp` —
  implementations, threshold derivation, `computePaymentFactor` updated to
  use the hybrid; removed dead `computeRaisedRate`.
- `src/test/app/LendingHelpers_test.cpp` — new regression tests, stress
  benchmark; removed the now-redundant `testComputeRaisedRate`.

## Related bugs not addressed

The same cancellation pattern exists in two other call sites that compute
`(1+r)^n - 1` indirectly via subtraction:

- `tryOverpayment` (`LendingHelpers.cpp:~406, ~439`) — calls
  `computeTheoreticalLoanState` in the overpayment re-amortization path.
  Benefits automatically from the fix since `computeTheoreticalLoanState`
  calls `computePaymentFactor` transitively.
- `computeOverpaymentComponents` (bugs 6 and 7 per
  `BUG_ROOT_CAUSES.md`) — these are separate defects (wrong assertion
  formula and missing input rounding respectively) and are **not**
  addressed by this fix.
