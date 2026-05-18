# `src/libxrpl/conditions/error.cpp`

## Role in the System

This file wires the cryptoconditions subsystem into C++'s standard `<system_error>` machinery. Its sole job is to define a custom `std::error_category` for the `xrpl::cryptoconditions::error` enum declared in `detail/error.h`, and to expose the `make_error_code()` factory that bridges the enum to `std::error_code`. The companion header also specializes `std::is_error_code_enum<error>`, which allows `error` enumerators to implicitly convert to `std::error_code` in function call and comparison contexts — a pattern that makes error propagation throughout the conditions library idiomatic C++11 error handling without exceptions.

## The Error Enum and Its Categories

The `error` enum (defined in `include/xrpl/conditions/detail/error.h`) starts at value `1` — deliberately skipping `0` to avoid collisions with the "no error" sentinel that `std::error_code` uses. Its seventeen enumerators fall into four logical groups:

- **Specification errors** (`unsupported_type`, `unsupported_subtype`, `unknown_type`, `unknown_subtype`, `fingerprint_size`, `incorrect_encoding`) — the request or data violates the Crypto-Conditions specification.
- **Buffer errors** (`trailing_garbage`, `buffer_empty`, `buffer_overfull`, `buffer_underfull`) — the raw byte buffer is malformed or incorrectly sized.
- **DER encoding errors** (`malformed_encoding`, `unexpected_tag`, `short_preamble`) — the DER/ASN.1 structure of an encoded condition or fulfillment is invalid.
- **Implementation limits** (`long_tag`, `large_size`, `preimage_too_long`) — valid-by-spec inputs that exceed what this implementation is willing to process.

This grouping is visible in the message strings returned by `message()`: each prefix (`"Specification:"`, `"Bad buffer:"`, `"Malformed DER encoding:"`, `"Implementation limit:"`) signals to a caller not just what failed, but which layer of the stack is responsible.

## `cryptoconditions_error_category`

The class is a private implementation detail confined to `namespace xrpl::cryptoconditions::detail`. It overrides the three `std::error_category` virtual functions:

- `name()` returns the static string `"cryptoconditions"`, used when printing or logging error codes.
- `message(int ev)` converts an integer back to a human-readable string via `safe_cast<error>(ev)`. The use of `safe_cast` here is intentional: because the `<system_error>` interface passes error values as raw `int`, this cast performs compile-time width and sign checks (verified in `include/xrpl/basics/safe_cast.h`) before delegating to a `static_cast`. If an integer falls through the switch, `default` returns `"generic error"`, matching the `error::generic` fallback.
- `equivalent()` uses strict identity comparison on the category pointer (`&condition.category() == this`) rather than name-string comparison. This is the recommended pattern in the standard — name strings are for human consumption, and pointer identity guarantees that two codes only compare equal if they come from the same category instance.

The singleton is delivered by `get_cryptoconditions_error_category()`, which constructs the category object exactly once as a function-local `static const`. This is the canonical way to implement `std::error_category` singletons: it avoids the static-initialization-order fiasco, is thread-safe under C++11's rules for local statics, and guarantees the single address that pointer-identity comparison depends on.

## `make_error_code()`

This free function is the public seam between the enum and the standard error infrastructure. It uses `safe_cast<std::underlying_type<error>::type>(ev)` to convert the enum to its underlying integer type before constructing the `std::error_code`. Because `safe_cast` operates at compile time for enum-to-integer conversions, there is no runtime overhead. The `std::is_error_code_enum` specialization in the header means callers can write `std::error_code ec = error::buffer_empty;` and the compiler silently calls `make_error_code` — this is the ADL-based implicit-conversion protocol defined by the `<system_error>` standard.

## Design Notes

The entire implementation is intentionally minimal. There are no exceptions, no heap allocations, and no mutable state — the category object is `const` and the message strings are string literals. The `default_error_condition` override simply maps every code back to itself (no cross-category equivalence is claimed), which is the correct choice: cryptoconditions errors are a domain-specific vocabulary and there is no meaningful mapping to `std::errc` POSIX codes.