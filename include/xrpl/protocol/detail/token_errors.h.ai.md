# `include/xrpl/protocol/detail/token_errors.h`

## Role in the System

This header defines the error type system for XRPL's Base58Check token codec — the encoding scheme used throughout the ledger for account addresses, node keys, seeds, and other typed tokens. It is a pure error-taxonomy header: it introduces no algorithms, only the vocabulary of failure modes that the codec can signal and the plumbing to make those failures composable via `std::error_code`.

## The `TokenCodecErrc` Enum

`TokenCodecErrc` is a scoped enum whose nine enumerators cover every distinct failure the codec can produce:

- **Size failures** (`inputTooLarge`, `inputTooSmall`, `outputTooSmall`): bounds violations detected before or during a conversion pass.
- **Alphabet / character failures** (`badB58Character`, `invalidEncodingChar`): invalid bytes encountered in a Base58 string, distinguished by context (character not in alphabet vs. invalid in the specific encoding context).
- **Semantic failures** (`mismatchedTokenType`, `mismatchedChecksum`): structurally valid input that fails XRPL-specific validation — wrong one-byte type prefix or a four-byte checksum mismatch.
- **Arithmetic failure** (`overflowAdd`): bignum addition overflow during the multi-precision Base58 decode, signaling that the encoded value would exceed the expected output width.
- **Sentinel values** (`success = 0`, `unknown`): `success` is zero deliberately — `std::error_code` treats a zero value as no-error, which is the contract `make_error_code` relies on.

## Integration with `std::error_code`

The header wires `TokenCodecErrc` into the standard error-code machinery through three pieces that work together:

1. **`std::is_error_code_enum` specialisation** (inside `namespace std`): by inheriting `true_type`, this marks `TokenCodecErrc` as a type that can be implicitly converted to `std::error_code`. Without this, callers would have to call `make_error_code` explicitly every time they stored or compared an error.

2. **`TokenCodecErrcCategory`** (in `xrpl::detail`): a concrete `std::error_category` subclass. Its `name()` returns the stable string `"TokenCodecError"`, and its `message()` translates each enumerator into a human-readable string. Both overrides are declared `final` to prevent further subclassing — there is exactly one category for this error domain.

3. **`make_error_code(TokenCodecErrc)`** and **`TokenCodecErrcCategory()`**: the two free functions complete the ADL-based protocol. `TokenCodecErrcCategory()` returns a reference to a function-local static, achieving thread-safe singleton semantics at zero cost after the first call (guaranteed by C++11 static initialisation). `make_error_code` pairs an integer value with that category into a `std::error_code`.

This pattern — enum + `is_error_code_enum` + category + `make_error_code` — is the idiomatic C++11 way to create a custom error domain compatible with `std::error_code` without the overhead of exceptions.

## Usage Context

`b58_utils.h` (a sibling `detail` header) directly returns `TokenCodecErrc` values from its inline bignum helpers (`inplace_bigint_add`, `inplace_bigint_mul`). The main codec in `tokens.cpp` wraps results in `Unexpected(TokenCodecErrc::...)`, pairing with the `B58Result<T>` alias (`Expected<T, std::error_code>`) declared in `tokens.h`. The implicit conversion enabled by the `is_error_code_enum` specialisation means a bare `TokenCodecErrc` enumerator flows transparently into an `std::error_code`-typed `Unexpected` wrapper without a cast. Callers then compare the code against specific enumerators — again using implicit conversion — to distinguish recoverable from fatal decode failures.

The `overflowAdd` error deserves special mention: it exists because the fast Base58 decoder in `b58_fast` represents intermediate values as fixed-size arrays of `uint64_t` words. If a caller passes an over-long encoded string, arithmetic on those words can overflow. This error code surfaces that hardware-level arithmetic condition as a clean protocol failure, preventing silent corruption.