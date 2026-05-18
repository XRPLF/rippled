# `include/xrpl/protocol/tokens.h`

This header is the public interface for Base58Check encoding and decoding of XRPL cryptographic identifiers. It is the mechanism by which raw byte sequences — account IDs, node keys, seeds — are converted to and from the human-readable strings that appear in XRPL transactions and client APIs (e.g., `rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh` for an account).

## Token Types and the Version Prefix

The `TokenType` enum assigns each category of XRPL identifier a specific byte value that is prepended to the payload before Base58Check encoding. These values match the XRPL standard defined at [xrpl.org/base58-encodings.html](https://xrpl.org/base58-encodings.html):

| Token type        | Prefix byte | Typical use                  |
|-------------------|-------------|------------------------------|
| `AccountID`       | 0           | Classic account addresses    |
| `AccountPublic`   | 35          | Account public keys          |
| `AccountSecret`   | 34          | Account private keys         |
| `NodePublic`      | 28          | Validator/peer public keys   |
| `NodePrivate`     | 32          | Validator/peer private keys  |
| `FamilySeed`      | 33          | Key-generation seeds         |

Two entries — `None` and `FamilyGenerator` — are marked unused; they exist to reserve their numeric values and prevent accidental reuse.

The prefix byte is critical: it causes the Base58Check-encoded output to begin with a recognizable letter in the XRPL alphabet, providing a visual cue to the token's type. The 4-byte SHA256-double-hash checksum appended after the payload allows recipients to detect transcription errors before attempting to use a key.

## Dual-Implementation Architecture

The header exposes the same encode/decode surface three times under different namespaces, reflecting an intentional portability vs. performance trade-off:

**Top-level (`xrpl::`)**: `encodeBase58Token` and `decodeBase58Token` are the functions callers should use. Internally they dispatch: on non-MSVC platforms they call `b58_fast`, on MSVC they fall back to `b58_ref`. This dispatch is a compile-time `#ifndef _MSC_VER` guard because the fast path relies on GCC/Clang's `unsigned __int128` extension.

**`xrpl::b58_ref`**: The reference implementation, adapted from Bitcoin Core. It performs direct base-conversion digit-by-digit (256 → 58 for encoding, 58 → 256 for decoding). While correct and portable, each input byte requires iterating over the entire accumulator buffer, giving O(n²) behaviour that becomes measurable for long keys.

**`xrpl::b58_fast`** (non-MSVC only): A redesigned algorithm described in detail in the implementation's comment block. Instead of converting directly from base 256 to base 58, it routes through an intermediate base 58^10 representation. The key insight is that 58^10 = 430,804,206,899,405,824 fits in a 64-bit register, so groups of 10 base-58 digits can be processed as a single 64-bit word using native multiplication. Conversions between bases that are powers of one another are trivial concatenations; the expensive multi-precision arithmetic is then performed on far fewer, larger coefficients. This achieves the 10–15× speedup cited in the header comment.

The fast path's functions take `std::span<std::uint8_t>` parameters and caller-supplied output buffers, avoiding heap allocation in the hot path. The legacy `std::string`-returning overloads (which match the `b58_ref` API) are provided for compatibility but do involve one allocation.

## Error Handling

The fast implementation returns `B58Result<T>`, a type alias defined in this header:

```cpp
template <class T>
using B58Result = Expected<T, std::error_code>;
```

`Expected<T, E>` is XRPL's pre-C++23 approximation of `std::expected`, backed by `boost::outcome`. It holds either a value of type `T` or an error of type `E`, and it is marked `[[nodiscard]]` so callers cannot silently drop failure information.

The error codes are defined in `detail/token_errors.h` via the `TokenCodecErrc` enum, which integrates into the standard `<system_error>` machinery through a `std::is_error_code_enum` specialization. Relevant codes include `badB58Character` (invalid character in encoded input), `mismatchedTokenType` (prefix byte doesn't match the expected `TokenType`), `mismatchedChecksum` (data corrupted or wrong key), and `outputTooSmall` (caller-supplied buffer insufficient).

The reference implementation follows the older convention of returning an empty `std::string` on failure, which loses error detail. New call sites should prefer the span-based fast API that propagates typed errors.

## Low-Level Detail Functions

Both namespaces expose `detail` sub-namespaces containing the raw base-conversion primitives (`encodeBase58`/`decodeBase58` in `b58_ref::detail`, and `b256_to_b58_be`/`b58_to_b256_be` in `b58_fast::detail`). These are deliberately exposed for unit testing only — they operate on raw byte spans without any token-type prefix or checksum logic, allowing the numeric conversion to be tested independently of the XRPL protocol framing.

The multi-precision arithmetic helpers in `detail/b58_utils.h` (`carrying_mul`, `inplace_bigint_mul`, `inplace_bigint_div_rem`) are implemented using `unsigned __int128` and are also guarded by `#ifndef _MSC_VER`. Their inline definitions in a header allow the compiler to produce single-instruction multiply/divide sequences on x86-64.

## Template `parseBase58`

The header declares two `parseBase58<T>` overloads — one taking only a string, one also taking an explicit `TokenType` — but provides no definition. Definitions exist elsewhere in the codebase as explicit template specializations for concrete XRPL types (such as `AccountID` and public key types). This design keeps the generic interface in a shared header while allowing type-specific parsing logic to live alongside each type's own implementation.