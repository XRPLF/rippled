# `include/xrpl/conditions/detail/error.h`

This header defines the error vocabulary for the `xrpl::cryptoconditions` module, which implements the [Crypto-Conditions](https://tools.ietf.org/html/draft-thomas-crypto-conditions) specification used by XRPL's `EscrowFinish` transaction type.

## Error Enum

The `error` scoped enum establishes a typed set of failure codes that callers receive when parsing or validating a cryptocondition or its fulfillment. The values fall into three natural groups:

- **Specification violations** — `unsupported_type`, `unsupported_subtype`, `unknown_type`, `unknown_subtype`, `fingerprint_size`, `incorrect_encoding`: the input doesn't conform to a recognized or supported condition type.
- **Buffer / parse failures** — `trailing_garbage`, `buffer_empty`, `buffer_overfull`, `buffer_underfull`, `malformed_encoding`, `short_preamble`, `unexpected_tag`: the raw byte buffer is structurally invalid. These map closely to the X.690 Distinguished Encoding Rules (DER) decoder in the sibling file `detail/utils.h`, which decodes the binary preamble and length fields that wrap condition data.
- **Implementation limits** — `long_tag`, `large_size`, `preimage_too_long`: the input is technically well-formed but exceeds what this implementation is willing to handle.

The catch-all `generic = 1` exists as a fallback; starting the enum at `1` (rather than `0`) is intentional — `0` is reserved by the `std::error_code` convention to mean "no error."

## `std::error_code` Integration

The two additions beyond the enum itself — the `make_error_code()` declaration and the `std::is_error_code_enum` specialization — wire `error` into the standard C++ error system. Specializing `is_error_code_enum<xrpl::cryptoconditions::error>` to `true` allows values of the enum to implicitly convert to `std::error_code` wherever one is expected, so callers throughout the conditions subsystem can write idiomatic C++ like `return error::buffer_empty` from a function returning `std::error_code`.

The actual `make_error_code()` implementation lives in `src/libxrpl/conditions/error.cpp`, where it constructs a `std::error_code` backed by a Meyers-singleton `cryptoconditions_error_category` — a custom `std::error_category` subclass whose `message()` method maps each enum value to a human-readable diagnostic string. Keeping the category as a function-local static guarantees initialization-order safety while remaining allocation-free after first use.