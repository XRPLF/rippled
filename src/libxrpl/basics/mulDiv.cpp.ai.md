## `mulDiv.cpp` — Overflow-Safe Multiply-Then-Divide

This file implements a single arithmetic utility: `mulDiv(value, mul, div)`, which computes `(value * mul) / div` over three `std::uint64_t` operands without losing precision or silently producing a wrong answer due to integer overflow.

### The Problem It Solves

Naive computation of `(value * mul) / div` in 64-bit arithmetic is unsafe when `value * mul` exceeds `UINT64_MAX` (~1.8 × 10¹⁹). This happens routinely in XRPL fee scaling: `LoadFeeTrack.cpp` calls `mulDiv(fee, feeFactor, feeBase)` to scale a base fee by a load factor, and the intermediate product can easily overflow. A silent 64-bit overflow would produce a wildly incorrect fee — a class of bug that is very hard to detect at runtime and dangerous in a financial system.

The naive two-step alternative — dividing first to keep numbers small — is equally flawed because it introduces rounding error proportional to `mul / div`, which violates the ledger's need for exact integer arithmetic.

### The 128-bit Intermediate Trick

The implementation uses `boost::multiprecision::uint128_t` to hold the intermediate product:

```cpp
boost::multiprecision::uint128_t result;
result = multiply(result, value, mul);   // 64×64 → 128 bits, no overflow
result /= div;                           // back toward range
```

By widening to 128 bits before multiplying, any product of two `uint64_t` values fits without overflow (max product ≈ 3.4 × 10³⁸, well within 128 bits). Division is then performed on the full-precision 128-bit value, so no precision is lost before the final narrowing.

Boost multiprecision is used rather than the `__uint128_t` GCC extension because the latter is not available on MSVC — this keeps the code portable across the compilers used in XRPL builds.

### Overflow Detection and the `std::optional` Return

After division, the 128-bit result may still exceed `uint64_t` range. The check:

```cpp
if (result > xrpl::muldiv_max)
    return std::nullopt;
```

`muldiv_max` is `std::numeric_limits<std::uint64_t>::max()`, defined in the accompanying header. Returning `std::nullopt` rather than throwing communicates overflow to callers that should handle it as a control-flow condition rather than an exceptional event. Callers that need an exception — like `LoadFeeTrack.cpp`, which throws `std::overflow_error("scaleFeeLoad")` — wrap the call and throw themselves, keeping policy out of this utility.

### Test Coverage

The test file (`src/tests/libxrpl/basics/mulDiv.cpp`) validates commutativity of the input arguments, zero operands, large near-`max` inputs that should fit, and a case that intentionally overflows. A representative stress case, `mulDiv(max, 1000, max / 1000)`, confirms that an intermediate product roughly 1000× larger than `UINT64_MAX` is handled correctly and produces the exact integer `1,000,000`.

### Callers and Context

Beyond fee scaling, `mulDiv` appears in `TxQ.cpp` (transaction queue fee escalation) and `TransactionSign.cpp` (fee calculations during signing). In all cases the pattern is the same: a ratio `a/b` must be applied to a `uint64_t` value where the intermediate product is untrustworthy in 64-bit arithmetic. The function's simplicity — four lines of logic — belies how frequently 64-bit overflow quietly breaks financial arithmetic in less carefully written code.