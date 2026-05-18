# `MathUtilities.h` — Integer Percentage Utility

This header lives in the `xrpl::basics` utility layer and provides a single function, `calculatePercent`, for computing integer percentages with ceiling rounding and range clamping. It exists because this specific combination of behaviors — ceiling division, overflow-safe clamping, and compile-time verifiability — recurs in the XRPL codebase wherever a count-over-total ratio needs to be expressed as a human-readable integer percentage without floating point.

## `calculatePercent`

```cpp
constexpr std::size_t calculatePercent(std::size_t count, std::size_t total)
```

The function answers: "what percent of `total` is `count`, rounded up to the nearest whole percent, and never exceeding 100?" The ceiling rounding is intentional: a fraction like 1/99 (≈1.01%) rounds up to 2, signalling that *some* nonzero fraction is present. This is the conservative, safety-oriented direction for thresholding use cases — if you're checking whether at least N% of validators have seen something, you want to err on the side of reporting a higher fraction rather than losing signal in truncation.

The arithmetic `((std::min(count, total) * 100) + total - 1) / total` encodes three distinct behaviors in one expression. The `std::min(count, total)` clamp prevents the numerator from exceeding `total * 100`, which both enforces the 100% cap and guards against the case where `count > total` (e.g., a validator set that temporarily grows). The `+ total - 1` addend is the standard ceiling-division trick: it causes any non-zero remainder in the integer division to round up rather than truncate. The whole expression stays in unsigned integer arithmetic with no floating point, making it safe across all platforms and constexpr-eligible.

The `assert(total != 0)` guard catches division-by-zero in debug builds. The comment explains why `XRPL_ASSERT` is not used here: the function is `constexpr`, and `XRPL_ASSERT` is not constexpr-compatible, so the plain `assert` is the appropriate defence at runtime while still permitting compile-time evaluation.

## Real-World Use

The primary call site is in `LedgerMaster.cpp`, which uses `calculatePercent` to decide whether to warn operators about a potential software upgrade need. Two separate thresholds are evaluated: whether at least 90% of the UNL (Unique Node List) validators have sent validation messages, and whether at least 60% of xrpld-running validators are on a higher version. Both checks use `calculatePercent` with named constants (`reportingPercent`, `cutoffPercent`) rather than raw literals, and the thresholding via `>=` naturally benefits from ceiling rounding — a value just above a threshold boundary will not be falsely excluded by truncation.

## Compile-Time Tests

The `static_assert` block at namespace scope doubles as both documentation and zero-cost test coverage. Fourteen cases are checked at compile time, covering the zero numerator, full 100% match, over-100% input, boundary rounding cases (1/99, 1/64, near-100% fractions), and large-scale inputs up to 100 million. If the implementation is ever changed and any of these properties breaks, the build fails immediately — there is no need for a separate test binary or runtime fixture. This pattern is well-suited to a pure arithmetic utility that has no external dependencies.