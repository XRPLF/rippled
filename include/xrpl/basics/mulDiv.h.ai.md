# `include/xrpl/basics/mulDiv.h`

This header declares a single arithmetic utility, `mulDiv()`, that computes `value * mul / div` on unsigned 64-bit integers without intermediate overflow and without sacrificing precision. It also exposes `muldiv_max`, a `constexpr` alias for `std::numeric_limits<std::uint64_t>::max()`, which callers use as a natural overflow sentinel.

## The problem it solves

A direct evaluation of `value * mul / div` on `uint64_t` silently overflows whenever the product `value * mul` exceeds 2⁶⁴−1, which is common in ledger fee calculations where both operands can be near the full 64-bit range. Splitting the operation into two steps makes this worse, not better — intermediate truncation in integer division also discards precision. `mulDiv` fixes both issues at once.

## Implementation strategy

The implementation (`src/libxrpl/basics/mulDiv.cpp`) uses `boost::multiprecision::uint128_t` as the scratch type. The 64-bit inputs are multiplied into the 128-bit accumulator via Boost's `multiply()`, then divided in place. Only at the very end does the code check whether the 128-bit result fits back into a `uint64_t`; if it exceeds `muldiv_max` the function returns `std::nullopt`. This approach is exact — no floating-point rounding, no two-step integer approximation.

## Return convention and caller responsibility

The function is declared noexcept-by-convention (the header comment explicitly says "Throws: None") and returns `std::optional<std::uint64_t>`. This shifts the overflow decision to the caller, which is important because different callers have different policies:

- `LoadFeeTrack.cpp` treats overflow as a logic error and propagates an `std::overflow_error` via `Throw<>`.
- `TxQ.cpp` uses `value_or(xrpl::muldiv_max)` throughout — clamping to the maximum integer is acceptable for fee-level arithmetic where "astronomical fee" is a safe ceiling.

The `muldiv_max` constant is exported from this header precisely to support the `value_or` pattern consistently across call sites.

## Usage in practice

Every call site in the ledger involves proportional fee scaling: converting raw fees to fee levels, applying percentage adjustments, escalating fees based on queue depth, or scaling a base fee by a load factor. In all cases the multiplication would overflow `uint64_t` for large inputs, but the mathematical result still fits once divided — exactly the scenario `mulDiv` is built for.

The test suite (`src/tests/libxrpl/basics/mulDiv.cpp`) confirms correct results for values near `UINT64_MAX`, verifies commutativity of `value` and `mul`, checks zero-operand edge cases, and asserts that `std::nullopt` is returned when the division cannot bring the 128-bit product back under the 64-bit ceiling.