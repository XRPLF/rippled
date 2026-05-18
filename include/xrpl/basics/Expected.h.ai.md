# `include/xrpl/basics/Expected.h`

## Role and Motivation

This header provides `xrpl::Expected<T, E>`, a polyfill for the `std::expected<T, E>` type proposed for C++23 (P0323R10). At the time this code was written, `std::expected` was not yet available, so the implementation delegates all storage and state management to `boost::outcome_v2::result<T, E, Policy>` while exposing an API that closely mirrors the eventual standard — making a future migration straightforward.

`Expected<T, E>` represents a value that is *either* a success of type `T` or an error of type `E`. Unlike `std::optional`, which only signals absence, `Expected` carries diagnostic information about *why* a result is missing. Unlike exceptions, it forces callers to explicitly inspect the outcome. The `[[nodiscard]]` attribute on both the primary template and the `void` specialization guarantees at compile time that callers cannot silently drop a return value — a critical safety property in a financial ledger where ignored error returns could mean silent transaction corruption.

## Components

### `bad_expected_access`

A thin `std::runtime_error` subclass thrown whenever code tries to read the wrong half of an `Expected` — e.g., calling `value()` on an error-holding instance. It carries no additional data because the error value itself is available via `error()` and the point of failure is immediately clear from the stack trace. By inheriting from `std::runtime_error`, it integrates naturally with XRPL's existing exception hierarchy.

### `detail::throw_policy`

Boost.Outcome's policy mechanism controls what happens when the invariants of a `result` are violated. The default boost policy may assert or exhibit undefined behavior depending on build configuration; `throw_policy` replaces that with deterministic exception throwing. All three "wide" check entry points — `wide_value_check`, `wide_error_check`, and `wide_exception_check` — delegate to `Throw<bad_expected_access>()` (from `contract.h`) rather than a bare `throw`, so the violation is also logged before the exception propagates, consistent with XRPL's programming-by-contract philosophy.

### `Unexpected<E>`

A wrapper type that acts as an explicit tag for the error path. A function returning `Expected<T, E>` constructs the error branch by returning `Unexpected<E>(err)`, not a bare `E`. This prevents the implicit construction ambiguity that would arise if both `T` and `E` were, for example, `std::string`. The class provides all four value-category overloads of `value()` (lvalue/rvalue × const/non-const) for perfect forwarding into `Expected`'s constructor.

The deduction guide `Unexpected(E (&)[N]) -> Unexpected<E const*>` makes it ergonomic to pass string literals: `Unexpected("bad input")` deduces to `Unexpected<char const*>` rather than to a fixed-length array type, avoiding obscure template errors.

### `Expected<T, E>` (primary template)

Privately inherits from `boost::outcome_v2::result<T, E, detail::throw_policy>`. Private inheritance is intentional — it exposes only the `std::expected`-shaped API and hides the broader Outcome API (which includes channel-specific accessors and other facilities that would pollute the interface). The two constructors use `requires std::convertible_to` constraints so that implicit narrowing is rejected at compile time.

`operator bool`, `operator*`, and `operator->` map onto `has_value()` and `value()`, matching the pointer-like ergonomics of the standard proposal. Accessing `operator*` or `operator->` on an error-holding `Expected` triggers `throw_policy::wide_value_check`, which throws `bad_expected_access`. Similarly, calling `error()` on a value-holding instance triggers `wide_error_check`.

### `Expected<void, E>` (partial specialization)

Functions that either succeed (producing no value) or fail with a diagnostic use this specialization. Its default constructor calls `boost::outcome_v2::success()` to produce a successful instance — matching the proposed `std::expected<void, E>{}` default construction semantics. This is the pattern used in `STTx::checkSign()` and related signature-verification methods, which return `Expected<void, std::string>`: on success the caller simply checks `operator bool`; on failure the error string explains what went wrong.

## Usage Patterns in the Codebase

`tokens.h` defines a convenience alias `B58Result<T> = Expected<T, std::error_code>` for Base58Check encoding/decoding operations, where the error is a standard system error code. `base_uint.h` uses `Expected<decltype(data_), ParseResult>` for a `noexcept` hex-parsing path, capturing a per-character parse failure without throwing. `STTx.h` uses `Expected<void, std::string>` for all signature-check entry points — a natural fit because signature validation either passes silently or produces a human-readable error message.

## Design Trade-offs

Choosing `boost::outcome` over a hand-rolled type means the storage layout, move semantics, and triviality propagation are handled by a well-tested library, reducing the risk of subtle UB in low-level storage operations. The cost is a dependency on Boost and some mismatch between Outcome's three-state model (value / error / exception pointer) and `std::expected`'s two-state model; the `wide_exception_check` override in `throw_policy` handles the third state consistently by also throwing `bad_expected_access`, even though `Expected` itself never stores an exception pointer in practice. When C++23 `std::expected` becomes universally available, the migration path is clear: the public API is already a subset of the standard interface.