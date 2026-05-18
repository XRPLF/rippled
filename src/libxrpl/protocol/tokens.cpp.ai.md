# `src/libxrpl/protocol/tokens.cpp`

## Role in the System

This file is the single source of truth for XRPL's Base58Check encoding and decoding — the scheme that turns raw binary account IDs, key pairs, and seeds into the human-readable strings that XRPL users interact with every day (`rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh`, `aKEusmsH9dJvjfeEg8XhDfpEgmhkK1VywK`, etc.). It is derived from Bitcoin Core's reference implementation but diverges in alphabet, version byte values, and — crucially — introduces a 10-15× faster encoding path that avoids the O(n²) big-integer arithmetic of the original.

## Token Format

Every encoded XRPL identifier follows the same wire layout before Base58 encoding:

```
[ type byte (1) ][ raw payload (N) ][ checksum (4) ]
```

The type byte is drawn from the `TokenType` enum: `AccountID = 0`, `NodePublic = 28`, `FamilySeed = 33`, etc. The checksum is the first four bytes of SHA-256(SHA-256(type || payload)) — identical to Bitcoin's checksum design. On decode, all three components are verified before the raw payload is returned. Returning an empty string (reference path) or a `TokenCodecErrc` error (fast path) signals any mismatch.

## XRPL Alphabet

The Base58 alphabet `"rpshnaf39wBUDNEGHJKLM4PQRST7VWXYZ2bcdeCg65jkm8oFqi1tuvAxyz"` is deliberately crafted so that `AccountID = 0` encoded addresses begin with the letter `r`. This is not cosmetic — it lets users and validators instantly recognise a classic XRPL account address before any decoding work occurs. The `alphabetReverse` lookup table is built as a `constexpr` 256-element array at compile time, mapping each ASCII code to its base-58 digit value or `-1` for invalid characters.

## Two Implementations, One Interface

The file exposes a single top-level pair of functions (`encodeBase58Token` / `decodeBase58Token` in namespace `xrpl`) that dispatch at compile time between two internal namespaces:

**`b58_ref`** — A portable implementation adapted from Bitcoin Core. Encoding iterates over each input byte and repeatedly multiplies a base-58 work buffer by 256, adding the new byte as carry. Decoding does the inverse: for each input character it multiplies a base-256 buffer by 58 and adds the digit. Both are O(input × work-buffer) in time, which is O(n²) in the number of bytes.

**`b58_fast`** — Available on all non-MSVC compilers (guarded by `#ifndef _MSC_VER`) because it relies on GCC's `unsigned __int128`. This implementation achieves 10-15× speedup by staging the base conversion through an intermediate representation.

## The Fast Algorithm's Key Insight

The file's opening block comment explains the mathematical foundations in detail. The core idea is that converting directly from base 58 to base 256 requires handling an O(n) multi-precision number for every input character. The fast algorithm avoids this by adding an intermediate step:

```
base 58 → base 58^10 → base 2^64 → base 2^8
```

`58^10 = 430804206899405824`, which is the largest power of 58 that fits in a 64-bit register. This choice is strategic: ten consecutive base-58 digits can be accumulated into a single `uint64_t` without overflow, converting the first "hop" from O(n) big-integer operations down to simple 64-bit arithmetic. The second hop (base 58^10 → base 2^64) still requires multi-precision work, but operates on far fewer, much larger coefficients — for a 38-byte payload (the maximum: 1 type + 33 public key + 4 checksum) only 5 `uint64_t` coefficients are needed. The final hop (base 2^64 → base 2^8) is just big-endian byte extraction.

The multi-precision helpers in `b58_utils.h` — `carrying_mul`, `carrying_add`, `inplace_bigint_div_rem`, `inplace_bigint_mul`, `inplace_bigint_add` — use `unsigned __int128` to handle the carry out of each 64-bit word cleanly. The key insight in `carrying_mul` is that `uint64_t × uint64_t` can overflow a 64-bit register, but `uint128_t × uint128_t + carry` does not overflow 128 bits for reasonable inputs, making the carry extraction reliable.

## Encoding Path (`b256_to_b58_be`)

The fast encoder works in three stages: it interprets the input bytes as a big-endian number represented in 64-bit limbs, repeatedly divides by `58^10` using `inplace_bigint_div_rem` to extract base-58^10 coefficients, then converts each coefficient to 10 base-58 digits using `b58_10_to_b58_be`, and finally maps those digits through `alphabetForward`. Leading zero bytes are handled separately — each maps to the first alphabet character ('r') to preserve the encoding's bijectivity for inputs with significant leading zeros.

## Decoding Path (`b58_to_b256_be`)

The fast decoder reverses the process. It groups the input string into chunks of 10 characters (with a possible partial chunk at the start), accumulates each group into a `uint64_t` coefficient, then synthesises the full big-integer value by iterating through the base-58^10 coefficients and using `inplace_bigint_mul` + `inplace_bigint_add`. The resulting array of `uint64_t` limbs is then written out as big-endian bytes.

After the base conversion, `decodeBase58Token` applies the three-layer validation: it checks the decoded length is at least 6 bytes, verifies the leading type byte matches the expected `TokenType`, and recomputes the 4-byte checksum to confirm it matches the trailing bytes. Only if all three checks pass is the interior payload returned.

## Error Handling and API Design

The fast implementation uses `B58Result<std::span<uint8_t>>` — an alias for `Expected<T, std::error_code>` backed by `TokenCodecErrc` — to communicate typed errors (`inputTooLarge`, `mismatchedTokenType`, `mismatchedChecksum`, `invalidEncodingChar`, etc.) without exceptions. The reference implementation signals failure by returning an empty `std::string`.

The top-level `encodeBase58Token` / `decodeBase58Token` string-returning overloads in the `b58_fast` namespace bridge between the span-based zero-allocation API and the legacy `std::string` interface, pre-allocating 128 bytes for encode and 64 bytes for decode — both sized generously above the theoretical maximum (≈46 base-58 characters for a 33-byte key) to avoid re-allocation while keeping stack usage bounded. Both overloads simply return an empty string on any error, preserving backward compatibility with callers that tested for an empty result.

## Relationship to Other Files

`tokens.h` declares all public entry points and the `TokenType` enum. `b58_utils.h` provides the inline arithmetic primitives behind the fast path; keeping them header-inline allows the compiler to aggressively inline and optimise the inner loops, which is important given how frequently address encoding runs. `token_errors.h` defines `TokenCodecErrc` and registers it as a `std::error_code` category, enabling integration with standard error-propagation machinery. The checksum uses `sha256_hasher` from `digest.h`, computed as a double-SHA256 — the same algorithm Bitcoin uses, ensuring the design remains auditable against well-understood prior art.