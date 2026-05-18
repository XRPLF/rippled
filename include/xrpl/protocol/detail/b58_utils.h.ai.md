# `include/xrpl/protocol/detail/b58_utils.h`

## Role in the System

This file is the arithmetic foundation for XRPL's fast base58 codec. It lives in the `xrpl::b58_fast::detail` namespace and provides the multi-precision integer operations that make the optimized encoding and decoding path 10–15× faster than the reference Bitcoin-derived algorithm in `b58_ref`.

The fundamental speed trick is explained at length in `tokens.cpp`: instead of working digit-by-digit between base-58 and base-256, the fast path uses base 58¹⁰ as an intermediate representation. Because 58¹⁰ = 430,804,206,899,405,824 — just under 2⁵⁹ — one coefficient of base 58¹⁰ fits comfortably in a `uint64_t`. Multi-precision arithmetic then operates on far fewer, much larger "digits", slashing the number of inner-loop iterations. The utilities here implement precisely the arithmetic needed: scalar multiply/add for decoding (base 58 → base 58¹⁰ → base 2⁶⁴) and divide/mod for encoding (base 2⁶⁴ → base 58¹⁰ → base 58).

## Platform Guard

The entire `b58_fast::detail` namespace is wrapped in `#ifndef _MSC_VER`. Every function in the namespace relies on GCC/Clang's `unsigned __int128` extension to perform 128-bit intermediate arithmetic without overflow. MSVC does not expose this type, so the fast path is silently absent on Windows builds; `tokens.cpp` falls back to the reference implementation in that case.

## The `Result<T>` Alias

At the outer `xrpl` namespace scope (not gated by the platform check), the file defines:

```cpp
template <class T>
using Result = boost::outcome_v2::result<T, std::error_code>;
```

This is the codec's idiomatic return type throughout `tokens.cpp`, pairing a success value with a `std::error_code` drawn from the `TokenCodecErrc` enum (defined in `token_errors.h`). Using `boost::outcome` rather than exceptions keeps the codec allocation-free and gives callers fine-grained error inspection without the cost of try/catch overhead.

## Primitive Arithmetic Helpers

`div_rem(a, b)` is a trivial wrapper that returns `{a/b, a%b}` as a tuple. The comment is telling: the compiler optimizes a single C++ `div_rem` call into one hardware divide instruction (which produces both quotient and remainder), avoiding two separate divisions.

`carrying_mul(a, b, carry)` multiplies two `uint64_t` values and adds a carry term using a `unsigned __int128` intermediate, then splits the 128-bit result into the low 64 bits (the new coefficient) and the high 64 bits (the new carry). This is exactly the operation hardware long-multiplication performs in each step, and GCC fuses the `__int128` arithmetic into a single `mulq` instruction.

`carrying_add(a, b)` follows the same pattern for addition, also using `__int128` to detect overflow into the 65th bit, which becomes the carry propagated upward through the big-integer limbs.

## Multi-Precision Big Integer Operations

Big integers are represented as `std::span<std::uint64_t>` with the **least significant limb first** (limb 0 holds the 2⁰ coefficient, limb n holds the 2⁶⁴ⁿ coefficient). This little-endian-limb layout lets carry propagation walk forward through the span.

**`inplace_bigint_mul(a, b)`** multiplies the big integer `a` by the scalar `b` in place, writing the carry into `a[last_index]`. The invariant it enforces is that `a[last_index]` must be zero on entry — this acts as a reserved overflow slot. If it is non-zero, the operation returns `TokenCodecErrc::inputTooLarge` rather than silently truncating. The caller in `tokens.cpp` always passes a span one element larger than the "live" portion, ensuring this slot is available.

**`inplace_bigint_add(a, b)`** adds a scalar `b` to the big integer `a` by first updating `a[0]`, then ripple-propagating carries through subsequent limbs. The function returns early as soon as the carry becomes zero, which is the common case after the first or second limb. A minimum span size of 2 is required (`inputTooSmall` if violated) to ensure there is at least one carry limb beyond the first. The `overflowAdd` error code is returned if carry propagates past the end — this is defined as a programming invariant violation (it cannot happen for valid-length base58 inputs), hence the comment in `token_errors.h` and the dedicated error code rather than an assertion that terminates the process.

**`inplace_bigint_div_rem(numerator, divisor)`** performs in-place long division of the big integer by a `uint64_t` scalar. It works from the most significant limb down, maintaining a running remainder `prev_rem`. At each step it forms a 128-bit value `(prev_rem << 64) | numerator[i]` and divides it by `divisor`, using inner lambdas `to_u128` and `div_rem_64` that keep the `__int128` logic localized and let the compiler verify (via `XRPL_ASSERT`) that neither the quotient nor the remainder overflows 64 bits. The return value is the final remainder; `numerator` is overwritten in place with the quotient. This is the workhorse of the encoding path: `tokens.cpp` calls it in a loop to extract successive base-58¹⁰ coefficients from the binary representation.

**`b58_10_to_b58_be(input)`** decomposes a single base-58¹⁰ coefficient (a `uint64_t` less than 58¹⁰) into exactly 10 individual base-58 digits stored most-significant-first (big-endian). It repeatedly calls `div_rem(input, 58)`, filling the result array from the back. The caller (`tokens.cpp`) then maps each digit through `alphabetForward[]` to get the final character. The `XRPL_ASSERT` guards the precondition that `input < 58¹⁰`; values at or above that bound indicate a logic error in the preceding division step.

## Error Handling Philosophy

Rather than throwing exceptions or calling `std::terminate`, every fallible operation returns a `TokenCodecErrc` value checked by the caller in `tokens.cpp`. The `[[nodiscard]]` attribute on every function ensures callers cannot silently discard failure. The `overflowAdd` case is the only one that represents an internal logic error (as opposed to malformed user input); the choice to report it as an error code rather than an assertion reflects the general XRPL preference for recoverable, testable error paths even for conditions that should never occur in correct usage.